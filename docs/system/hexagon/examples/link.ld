/* SPDX-License-Identifier: GPL-2.0-or-later */

ENTRY(_start)

MEMORY
{
    RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 1M
}

SECTIONS
{
    .vectors : ALIGN(256) {
        KEEP(*(.vectors))
    } > RAM
    .text : ALIGN(16) {
        *(.text.start)
        *(.text .text.*)
    } > RAM
    .rodata : ALIGN(16) {
        *(.rodata .rodata.*)
    } > RAM
    .data : ALIGN(16) {
        *(.data .data.* .sdata .sdata.*)
    } > RAM
    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.bss .bss.* .sbss .sbss.*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    } > RAM
    __stack_top = ORIGIN(RAM) + LENGTH(RAM);
    ASSERT(__bss_end <= __stack_top - 16K, "No room for stack")
}
