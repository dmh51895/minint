/*
 * MinNT - firmware/rtw_fw_blob.S
 * Embeds rtl8821cu.bin (linux-firmware rtw88/rtw8821c_fw.bin) into the
 * kernel image. BUG-005 Option A: no filesystem needed — the blob rides
 * in .rodata and RtwUsbReadFirmware() hands out a pointer to it.
 *
 * To refresh the blob: run firmware/fetch-firmware.sh, then rebuild.
 * An empty (0-byte) rtl8821cu.bin builds fine and the driver reports
 * "no firmware embedded" at runtime.
 */

    .section .rodata
    .align 16

    .global RtwFwBlobStart
    .global RtwFwBlobEnd

RtwFwBlobStart:
    .incbin "firmware/rtl8821cu.bin"
RtwFwBlobEnd:

    /* Mark stack as non-executable for the linker */
    .section .note.GNU-stack, "", @progbits
