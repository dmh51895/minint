/*
 * MinNT - tcpip/lwip_port.c
 * lwIP TCP/IP stack port for MinNT.
 * Hooks lwIP core to the NDIS miniport.
 */

#include <nt/ke.h>
#include <nt/mm.h>
#include <nt/ex.h>
#include <nt/io.h>
#include <nt/rtl.h>
#include <rtw/rtw_usb.h>

extern RTW_ADAPTER RtwAdapter;

typedef unsigned char u8_t;
typedef unsigned short u16_t;
typedef unsigned long u32_t;
typedef int err_t;

extern void lwip_init(void);
extern void pbuf_init(void);
extern void *pbuf_alloc(unsigned long layer, unsigned short len, unsigned long type);
extern unsigned char pbuf_free(void *p);
extern unsigned short pbuf_copy(void *p, const void *dataptr, unsigned short len);
extern int netif_add(void *netif, void *ipaddr, void *netmask, void *gw,
                     void *state, int (*init)(void *netif),
                     int (*input)(void *p, void *netif));
extern int ethernet_input(void *p, void *netif);
extern void RtwNdisSendPacket(void *packet, unsigned long length);

struct ip4_addr {
    unsigned long addr;
};
typedef struct ip4_addr ip4_addr_t;

struct netif {
    char name[2];
    unsigned char hwaddr[6];
    unsigned short mtu;
    void *state;
    unsigned long flags;
    int (*input)(void *p, struct netif *netif);
    int (*output)(struct netif *netif, void *p);
    int (*linkoutput)(struct netif *netif, void *p);
};

struct pbuf {
    unsigned short tot_len;
    unsigned short len;
    void *payload;
    struct pbuf *next;
};

#define PBUF_POOL 2
#define PBUF_LINK 1

#define ERR_OK 0
#define netif_set_default(n) do {} while(0)
#define netif_set_up(n) do {} while(0)

#define IP4_ADDR(ipaddr, a,b,c,d) \
    do { (ipaddr)->addr = ((unsigned long)(a) | ((unsigned long)(b) << 8) | \
                          ((unsigned long)(c) << 16) | ((unsigned long)(d) << 24)); } while(0)

static struct netif RtwNetif;

static int rtw_netif_init(struct netif *netif)
{
    unsigned char mac[6];
    RtlCopyMemory(mac, RtwAdapter.CurrentMacAddress, 6);
    netif->hwaddr[0] = mac[0];
    netif->hwaddr[1] = mac[1];
    netif->hwaddr[2] = mac[2];
    netif->hwaddr[3] = mac[3];
    netif->hwaddr[4] = mac[4];
    netif->hwaddr[5] = mac[5];
    netif->mtu = 1500;
    DbgPrint("LWIP: netif_init() MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return ERR_OK;
}

static int rtw_netif_output(struct netif *netif, void *p)
{
    struct pbuf *pb = p;
    RtwNdisSendPacket(pb->payload, pb->tot_len);
    return ERR_OK;
}

static VOID rtw_rx_deliver(PVOID Data, ULONG Length)
{
    struct pbuf *p;

    if (!Data || Length < 14 || Length > RTW_USB_MAX_BULK_SIZE)
        return;

    p = pbuf_alloc(1, (u16_t)Length, 2);
    if (!p)
    {
        DbgPrint("LWIP: RX pbuf_alloc failed, len=%lu\n", (unsigned long)Length);
        return;
    }

    __builtin_memcpy(p->payload, Data, Length);
    p->tot_len = (u16_t)Length;
    p->len = (u16_t)Length;

    if (RtwNetif.input(p, &RtwNetif) != ERR_OK)
    {
        DbgPrint("LWIP: RX ethernet_input dropped packet\n");
        pbuf_free(p);
    }
}

static int rtw_netif_input(void *p, struct netif *netif)
{
    (void)netif;
    return ethernet_input(p, &RtwNetif);
}

static NTSTATUS NTAPI RtwLwipInit(VOID)
{
    ip4_addr_t ipaddr, netmask, gw;

    lwip_init();

    IP4_ADDR(&ipaddr, 192, 168, 1, 100);
    IP4_ADDR(&netmask, 255, 255, 255, 0);
    IP4_ADDR(&gw, 192, 168, 1, 1);

    netif_add(&RtwNetif, &ipaddr, &netmask, &gw,
              NULL, rtw_netif_init, rtw_netif_input);

    RtwNetif.output = rtw_netif_output;

    netif_set_default(&RtwNetif);
    netif_set_up(&RtwNetif);

    RtwRegisterRxCallback((PVOID)rtw_rx_deliver);
    DbgPrint("LWIP: netif registered IP=192.168.1.100, RX callback wired\n");
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtwTcpipInit(VOID)
{
    NTSTATUS Status;

    DbgPrint("TCPIP: initializing...\n");

    Status = RtwLwipInit();
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("TCPIP: lwIP init failed (0x%08lx)\n", (unsigned long)Status);
        return Status;
    }

    DbgPrint("TCPIP: ready\n");
    return STATUS_SUCCESS;
}