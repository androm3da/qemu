/* SPDX-License-Identifier: GPL-2.0-or-later */

    .section .vectors, "ax", @progbits
    .balign 256
    .globl _start
_start:
    jump reset
    .rept 47
    jump unexpected_event
    .endr

    .section .text.start, "ax", @progbits
reset:
    r0 = ##_start
    evb = r0
    r29 = ##__stack_top
    r30 = #0

    /* Clear whole words; the linker aligns both ends of .bss. */
    r0 = ##__bss_start
    r1 = ##__bss_end
    r2 = #0
clear_bss:
    p0 = cmp.eq(r0, r1)
    if (p0) jump bss_done
    memw(r0++#4) = r2
    jump clear_bss
bss_done:
    call main
    .globl stopped
stopped:
    jump stopped

    .globl unexpected_event
unexpected_event:
    jump unexpected_event
