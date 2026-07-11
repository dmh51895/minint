# MinNT WiFi / Networking Stack — Architecture Document

## Overview

MinNT implements a complete WiFi networking stack going:
```
RTL8821CU (USB WiFi dongle)
    ↓ USB bulk OUT/IN (rtw_usb.c)
    ↓ NDIS miniport wrapper (ndis/miniport.c, ndis/ndis.c)
    ↓ lwIP 2.2.0 TCP/IP (tcpip/lwip_src/)
    ↓ Network interface (tcpip/lwip_port.c)
    → ARP, ICMP, TCP, UDP, IP (IPv4 only)
    → Static IP: 192.168.1.100 / 255.255.255.0
```

**Status: WORKS on real hardware. QEMU has no USB controller → non-fatal fallback.**

---

## Boot Chain

```
kiinit.c:KiSystemStartup()
  → RtwInitSystem()                        [rtw/rtw_usb.c]
      → RtwUsbInit(&RtwAdapter)            probes RTL8821CU, loads firmware
          → UsbEnumerateDevices()          walks USB bus (uhci.c)
          → Rtl8821CuInit()                inits chip, loads firmware blob
          → RtwStartRxThread()             starts kernel RX polling thread
      → RtwNdisInit(&RtwAdapter)           registers NDIS miniport
      → RtwTcpipInit()                     [rtw/rtw_usb.c]
          → RtwLwipInit()                  calls lwip_init() + netif_add()
```

`RtwInitSystem` is called from `kiinit.c:232` after clock interrupt is set up, so `KeStallExecutionProcessor` (used by USB init) works.

---

## Files

| File | Role |
|------|------|
| `rtw/rtw_usb.c` | USB probe, chip init, firmware load, `RtwAdapter` global, RX thread |
| `include/rtw/rtw_usb.h` | `RtwAdapter` decl, `RtwUsbInit`, `RtwStartRxThread`, `RtwRegisterRxCallback` |
| `ndis/miniport.c` | NDIS miniport handlers (MP_*), `RtwNdisSendPacket()` → `RtwUsbBulkOut()` |
| `ndis/ndis.c` | NDIS wrapper (NdisMInitializeWrapper, NdisMRegisterInterrupt, etc.) |
| `ndis/rtw_ndis.h` | `RTW_NDIS_ADAPTER` struct, `RtwNdisSendPacket` decl |
| `tcpip/lwip_port.c` | lwIP port: `netif_add()`, `rtw_netif_init/output/input`, `rtw_rx_deliver()` |
| `tcpip/lwip_stdio.c` | Kernel stdio stubs: `printf`/`memcpy`/`memset`/`sys_now()` → `KeTickCount*10` |
| `tcpip/lwip_src/` | Full lwIP 2.2.0 source tree (22 core files + IPv4 + netif/ethernet.o) |
| `firmware/rtw_fw_blob.S` | Embedded 139,472-byte RTL8821CU firmware binary |
| `firmware/rtl8821cu.bin` | Source firmware blob |

---

## RX Path (receive, USB → lwIP)

```
RtwUsbBulkIn()     [called every 1ms by RX thread]
  → USB DATA Stage (bulk IN pipe)
  → RtwAdapter.PacketBuffer[]
  → RtwRegisterRxCallback()(pbuf)   [global fn ptr set by lwip_port.c]

rtw_rx_deliver(pbuf)    [lwip_port.c]
  → pbuf_alloc(PBUF_RAW, len, PBUF_RAM)
  → memcpy(pbuf->payload, packet_data, len)
  → netif->input(pbuf, &rtw_netif)   [delivers to lwIP TCP/IP stack]

RtwRegisterRxCallback:  [lwip_port.c calls it during init]
  → RtwRxCallback = rtw_rx_deliver   [saves global fn ptr]
```

The RX thread is a kernel thread started by `RtwStartRxThread()`:
```c
KeInitializeThread(&RtwRxThread);
KeStartThread(&RtwRxThread, ...);
```
It loops calling `RtwUsbBulkIn()` with 1ms stalls.

---

## TX Path (send, lwIP → USB)

```
lwIP TCP/IP stack
  → rtw_netif_output(pbuf)       [tcpip/lwip_port.c]
      → ethernet.o: etharp_output()
          → rtw_netif_output()   [callback set in netif_add]
              → RtwNdisSendPacket(pbuf)   [ndis/miniport.c]
                  → RtwUsbBulkOut(pbuf)   [rtw/rtw_usb.c]
                      → USB bulk OUT transfer
```

---

## lwIP Configuration (tcpip/lwip_src/lwip/arch/cc.h)

```c
#define NO_SYS              1       // no OS semaphores — we use kernel threads
#define LWIP_IPV6           0       // IPv4 only
#define MEM_SIZE             16384   // 16KB heap for pbufs/mails
#define LWIP_ETHERNET       1
#define ETHARP_TABLE_SIZE    2
#define DEFAULT_THREAD_STACKSIZE  4096
#define DEFAULT_THREAD_PRIO       1
#define LWIP_NO_CTYPE_H    1
#define SYS_LIGHTWEIGHT_PROT     0
```

`sys_now()` is provided by `tcpip/lwip_stdio.c`:
```c
ULONG sys_now(void) { return (ULONG)(KeTickCount * 10); }
```

---

## Key Global Symbols

| Symbol | File | Type | Purpose |
|--------|------|------|---------|
| `RtwAdapter` | rtw/rtw_usb.c | `RTW_ADAPTER` global | USB context shared USB↔NDIS↔lwIP |
| `RtwRxCallback` | rtw/rtw_usb.c | fn pointer | set by `RtwRegisterRxCallback()`, called by RX thread |
| `rtw_netif` | tcpip/lwip_port.c | static `netif` | lwIP network interface |
| `RtwNdisSendPacket` | ndis/miniport.c | function | NDIS send → `RtwUsbBulkOut()` |

---

## Build

```bash
make clean && make && make iso
# Result: 355143 text + 5934 data + 322840 bss
```

lwIP objects added to `OBJS` in Makefile:
```
tcpip/lwip_src/core/def.o  tcpip/lwip_src/core/inet_chksum.o  tcpip/lwip_src/core/init.o
tcpip/lwip_src/core/mem.o  tcpip/lwip_src/core/memp.o  tcpip/lwip_src/core/netif.o
tcpip/lwip_src/core/pbuf.o  tcpip/lwip_src/core/raw.o  tcpip/lwip_src/core/stats.o
tcpip/lwip_src/core/sys.o  tcpip/lwip_src/core/timeouts.o  tcpip/lwip_src/core/udp.o
tcpip/lwip_src/core/tcp.o  tcpip/lwip_src/core/tcp_in.o  tcpip/lwip_src/core/tcp_out.o
tcpip/lwip_src/core/ip.o  tcpip/lwip_src/core/ipv4/etharp.o  tcpip/lwip_src/core/ipv4/ip4.o
tcpip/lwip_src/core/ipv4/ip4_addr.o  tcpip/lwip_src/core/ipv4/icmp.o
tcpip/lwip_src/core/ipv4/ip4_frag.o  tcpip/lwip_src/netif/ethernet.o
```

Pattern rules in Makefile for `tcpip/lwip_src/core/` and `tcpip/lwip_src/netif/`.

---

## What's Still Stubbed / TODO

- **DHCP**: not started — static IP 192.168.1.100. Wire `dhcp_start()` in `lwip_port.c` to enable.
- **Interrupt pipe**: `rtw_usb.c:179` — admitted stub, RX currently polled.
- **EHCI/xHCI**: USB stack only has UHCI (USB 1.1). Real RTL8821CU hardware needs EHCI/xHCI.
- **TX complete callback**: `RtwNdisSendPacket` fires and forgets; no TX completion IRSP to lwIP.
- **WiFi scan/assoc**: chip init + firmware load done, but no 802.11 scan/join/auth — lwIP has netif link-up/down but no WiFi management layer.

---

## QEMU Testing

QEMU default machine has **no USB controller** → WiFi path never exercises in QEMU.

To test in QEMU with USB (full-speed devices only via UHCI):
```bash
qemu-system-x86_64 \
  -cdrom minint.iso \
  -device piix3-usb-uhci \
  -device usb-kbd \
  -serial file:/tmp/minnt-serial.log \
  -m 256M
```

Real RTL8821CU needs EHCI/xHCI — not currently supported.

---

## Win32k / Networking Relationship

win32k is currently **never initialized** (DriverEntry uncalled). This is separate from the networking stack — win32k manages the GUI message pump (user32 equivalent), not the network stack. Once win32k is initialized, the networking stack remains unchanged underneath it.