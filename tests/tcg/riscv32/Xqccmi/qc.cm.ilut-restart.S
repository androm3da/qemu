/*
 * Xqccmi ILUT second-slot restart test.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#if __riscv_xlen != 32
# error "This test targets RV32 only."
#endif

#define QC_CM_ILUT(index) .short (0x2000 | ((index) << 2))

    .section .text
    .global _start
_start:
    li a0, 11
    lla a1, sigsegv_action
    li a2, 0
    li a3, 8
    li a7, 134
    ecall
    bnez a0, fail

    lla t0, ilut
    csrw 0x800, t0
    li t0, 8
    csrw 0x801, t0

    li a0, 0
    li a1, 0
    QC_CM_ILUT(0)

    li t0, 1
    bne a1, t0, fail
    lw t0, data
    bne s0, t0, fail

    li a0, 0
    li a7, 93
    ecall

fail:
    li a0, 1
    li a7, 93
    ecall

sigsegv_handler:
    li t0, 11
    bne a0, t0, fail
    lla t0, data
    /* ucontext_t.uc_mcontext.gpr[9] is the saved a0 register. */
    sw t0, 200(a2)
    ret

    .balign 64
ilut:
    /* addi a1, a1, 1; lw s0, 0(a0) */
    .4byte 0x00158593
    .4byte 0x00052403

    .section .data
    .balign 4
sigsegv_action:
    .word sigsegv_handler
    .word 4
    .word 0
    .word 0
    .word 0
data:
    .word 42
