#!/bin/sh
# MinNT — fetch the Realtek 8821C firmware blob (BUG-005)
#
# Grabs rtw88/rtw8821c_fw.bin from a linux-firmware mirror and drops it
# here as rtl8821cu.bin so the kernel build embeds it via rtw_fw_blob.S.
#
# If you already run the rtw88 driver on this Zorin box, the identical
# blob is at /lib/firmware/rtw88/rtw8821c_fw.bin (possibly .zst) — the
# local copy is preferred when present.
#
# License: Realtek firmware, redistributable per linux-firmware
# LICENCE.rtlwifi_firmware.txt.

set -e
cd "$(dirname "$0")"

LOCAL_FW="/lib/firmware/rtw88/rtw8821c_fw.bin"
URL="https://raw.githubusercontent.com/armbian/firmware/master/rtw88/rtw8821c_fw.bin"

if [ -f "$LOCAL_FW" ]; then
    echo "Using local blob: $LOCAL_FW"
    cp "$LOCAL_FW" rtl8821cu.bin
elif [ -f "$LOCAL_FW.zst" ]; then
    echo "Using local zstd blob: $LOCAL_FW.zst"
    zstd -d -f "$LOCAL_FW.zst" -o rtl8821cu.bin
else
    echo "Fetching from linux-firmware mirror..."
    curl -sfL -o rtl8821cu.bin "$URL"
fi

SIZE=$(stat -c%s rtl8821cu.bin)
SIG=$(od -A n -t x1 -N 2 rtl8821cu.bin | tr -d ' ')
echo "rtl8821cu.bin: $SIZE bytes, signature bytes: $SIG (expect 2188 = chip 0x8821)"
[ "$SIG" = "2188" ] && echo "OK — rebuild the kernel: make clean && make iso" \
                    || echo "WARNING: unexpected signature, wrong blob?"
