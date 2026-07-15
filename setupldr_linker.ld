/*
 * MinNT - setupldr_linker.ld
 * Linker script for the standalone setup loader.
 *
 * Much simpler than the full kernel's linker.ld because setupldr
 * doesn't have the kernel module structure - it's a single binary
 * with just a small text section, a tiny BSS, and the multiboot
 * header at the very start.
 */

ENTRY(_start)

SECTIONS {
    . = 0x100000;   /* loaded at 1MB - GRUB multiboot2 default */

    .multiboot : {
        *(.multiboot)
    }

    .text : {
        *(.text)
        *(.text.*)
    }

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    .data : {
        *(.data)
        *(.data.*)
    }

    .bss : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }

    /DISCARD/ : {
        *(.note*)
        *(.comment)
    }
}
