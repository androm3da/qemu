/*
 * Self-checking Xqccmi ILUT test.
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
    lla t0, ilut
    csrw 0x800, t0
    li t0, 8
    csrw 0x801, t0

    /* Entry 0 is a 64-bit two-instruction entry. */
    QC_CM_ILUT(0)
    li t0, 42
    bne s0, t0, fail

    /* Entry 1 follows the double-entry region and is 32 bits wide. */
    QC_CM_ILUT(1)
    li t0, 7
    bne s1, t0, fail

    li a0, 0
    li a7, 93
    ecall

fail:
    li a0, 1
    li a7, 93
    ecall

    .balign 64
ilut:
    /* addi s0, zero, 40; addi s0, s0, 2 */
    .4byte 0x02800413
    .4byte 0x00240413
    /* addi s1, zero, 7 */
    .4byte 0x00700493
