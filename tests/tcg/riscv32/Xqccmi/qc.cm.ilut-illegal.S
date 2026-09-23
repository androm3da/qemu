/*
 * Xqccmi ILUT forbidden-instruction test.
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
    /* Install a SA_SIGINFO handler for the expected SIGILL traps. */
    li a0, 4
    lla a1, sigill_action
    li a2, 0
    li a3, 8
    li a7, 134
    ecall
    bnez a0, fail

    lla t0, ilut
    csrw 0x800, t0

    lla t0, resume_0
    lla t1, resume_pc
    sw t0, 0(t1)
    QC_CM_ILUT(0)
resume_0:
    lla t0, resume_1
    sw t0, 0(t1)
    QC_CM_ILUT(1)
resume_1:
    lla t0, resume_2
    sw t0, 0(t1)
    QC_CM_ILUT(2)
resume_2:
    lla t0, resume_3
    sw t0, 0(t1)
    QC_CM_ILUT(3)
resume_3:
    lla t0, resume_4
    sw t0, 0(t1)
    QC_CM_ILUT(4)
resume_4:
    lla t0, resume_5
    sw t0, 0(t1)
    QC_CM_ILUT(5)
resume_5:
    lla t0, resume_6
    sw t0, 0(t1)
    QC_CM_ILUT(6)
resume_6:
    lw t0, signals
    li t1, 7
    bne t0, t1, fail

    li a0, 0
    li a7, 93
    ecall

fail:
    li a0, 1
    li a7, 93
    ecall

sigill_handler:
    li t0, 4
    bne a0, t0, fail
    lla t0, resume_pc
    lw t0, 0(t0)
    /* ucontext_t.uc_mcontext.pc is 160 bytes from the SA_SIGINFO context. */
    sw t0, 160(a2)
    lla t0, signals
    lw t1, 0(t0)
    addi t1, t1, 1
    sw t1, 0(t0)
    ret

    .balign 64
ilut:
    /* auipc x0, 0 */
    .4byte 0x00000017
    /* beq x0, x0, 0 */
    .4byte 0x00000063
    /* jal x0, 0 */
    .4byte 0x0000006f
    /* jalr x0, x0, 0 */
    .4byte 0x00000067
    /* c.j 0; c.nop */
    .4byte 0x0001a001
    /* c.beqz x8, 0; c.nop */
    .4byte 0x0001c001
    /* c.jr ra; c.nop */
    .4byte 0x00018082

    .section .data
    .balign 4
sigill_action:
    .word sigill_handler
    .word 4
    .word 0
    .word 0
    .word 0
resume_pc:
    .word 0
signals:
    .word 0
