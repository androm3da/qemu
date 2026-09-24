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
    ori t0, t0, 63
    csrw 0x800, t0
    csrr t1, 0x800
    lla t2, ilut
    bne t1, t2, fail

    li t0, -1
    csrw 0x801, t0
    csrr t1, 0x801
    li t2, 0x3ff8
    bne t1, t2, fail

    li t0, 24
    csrw 0x801, t0

    /* Entry 0 reads a CSR, forcing a TB exit before its second instruction. */
    QC_CM_ILUT(0)
    li t0, 42
    bne s0, t0, fail

    /* Entry 2 contains a 16-bit instruction and a 48-bit instruction. */
    QC_CM_ILUT(2)
    li t0, 7
    bne s1, t0, fail

    /* Entry 3 follows the double-entry region and is 32 bits wide. */
    QC_CM_ILUT(3)
    li t0, 7
    bne s2, t0, fail

    li a0, 0
    li a7, 93
    ecall

fail:
    li a0, 1
    li a7, 93
    ecall

    .balign 64
ilut:
    /* csrrs x0, qc.itba, x0; addi s0, zero, 42 */
    .4byte 0x80002073
    .4byte 0x02a00413
    /* c.nop; c.nop; c.nop; c.nop */
    .4byte 0x00010001
    .4byte 0x00010001
    /* c.nop; qc.e.li s1, 7 */
    .4byte 0x049f0001
    .4byte 0x00000007
    /* addi s2, zero, 7 */
    .4byte 0x00700913
