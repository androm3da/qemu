/*
 * HMX operation-family TCG test suite.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

int err;

#include "hex_test.h"

static void clear_accumulators(void)
{
    asm volatile(".word 0xa6e0c013    /* mxclracc.hf */" : : : "memory");
    /* Initialize both halves of the F8 convert-result slot to zero. */
    asm volatile("r9 = #1\n"
                 ".word 0xa6e9db10    /* cvt.f8=acc(r9) */\n"
                 :
                 :
                 : "r9", "memory");
    asm volatile("r9 = #0x801\n"
                 ".word 0xa6e9db10    /* cvt.f8=acc(r9) */\n"
                 :
                 :
                 : "r9", "memory");
}

/* hmx_matmul_fp_hf */
#define H_ACT_SIZE  1024  /* one crouton, as uint16_t: 2048 bytes / 2 */
#define H_WEI_SIZE  1024  /* 16 vectors x 128 bytes, as uint16_t */
#define H_OUT_SIZE  2048

static uint16_t H_act_buf[H_ACT_SIZE] __attribute__((aligned(2048)));
static uint16_t H_wei_buf[H_WEI_SIZE] __attribute__((aligned(128)));
static uint32_t H_bias_buf[32] __attribute__((aligned(128)));
static uint8_t H_out_buf[H_OUT_SIZE] __attribute__((aligned(2048)));

#define H_FP16_ONE    0x3C00u
#define H_BIAS_LOW32  H_FP16_ONE   /* scale=1.0, everything else 0 */
#define H_EXPECTED_LO 0x00u
#define H_EXPECTED_HI 0x50u      /* 32.0 as FP16 = 0x5000 */

static int hmx_test_hmx_matmul_fp_hf(void)
{
    err = 0;
    clear_accumulators();
    for (int i = 0; i < H_ACT_SIZE; i++) {
        H_act_buf[i] = H_FP16_ONE;
    }
    for (int i = 0; i < H_WEI_SIZE; i++) {
        H_wei_buf[i] = H_FP16_ONE;
    }
    for (int i = 0; i < 32; i++) {
        H_bias_buf[i] = H_BIAS_LOW32;
    }
    memset(H_out_buf, 0xff, sizeof(H_out_buf));

    /* bias = mxmem(r8) */
    asm volatile("r8 = %[bias]\n"
                 ".word 0x9208c3ff    /* bias=mxmem(r8) */\n"
                 :
                 : [bias] "r"(H_bias_buf)
                 : "r8", "memory");

    /* activation.hf = mxmem(r2,r3) */
    asm volatile("r2 = %[act]\n"
                 "r3 = #0x7c\n"
                 ".word 0x9202c3e4    /* activation.hf=mxmem(r2,r3) */\n"
                 :
                 : [act] "r"(H_act_buf)
                 : "r2", "r3", "memory");

    /* weight.hf = mxmem(r4,r5) -- triggers the FP matrix multiply. */
    asm volatile("r4 = %[wei]\n"
                 "r5 = #0xffff\n"
                 ".word 0x9204e5ef    /* weight.hf=mxmem(r4,r5) */\n"
                 :
                 : [wei] "r"(H_wei_buf)
                 : "r4", "r5", "memory");

    /*
     * cvt.hf=acc(r9), r9=0 (acc_clear/relu/fb_dst/fb_limit/maxnorm/
     * bias_sel all zero -- bias_sel=0 matches the bias load above).
     * Its own packet, so HELPER(hmx_commit_packet) flushes the pending
     * convert at the end of this instruction before the next one runs.
     */
    asm volatile("r9 = #0\n"
                 ".word 0xa6e9da10    /* cvt.hf=acc(r9) */\n"
                 :
                 :
                 : "r9", "memory");

    /* mxmem(r6,r7)=cvt -- reads the now-flushed convert result. */
    asm volatile("r6 = %[out]\n"
                 "r7 = #0\n"
                 ".word 0xa6e6c718    /* mxmem(r6,r7)=cvt */\n"
                 :
                 : [out] "r"(H_out_buf)
                 : "r6", "r7", "memory");

    for (int i = 0; i < H_OUT_SIZE; i += 2) {
        check32(H_out_buf[i], H_EXPECTED_LO);
        check32(H_out_buf[i + 1], H_EXPECTED_HI);
    }

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}

/* hmx_matmul_fp_bf16 */
#define B_ACT_SIZE  1024  /* one crouton, as uint16_t: 2048 bytes / 2 */
#define B_WEI_SIZE  1024  /* 16 vectors x 128 bytes, as uint16_t */
#define B_OUT_SIZE  2048

static uint16_t B_act_buf[B_ACT_SIZE] __attribute__((aligned(2048)));
static uint16_t B_wei_buf[B_WEI_SIZE] __attribute__((aligned(128)));
static uint32_t B_bias_buf[32] __attribute__((aligned(128)));
static uint8_t B_out_buf[B_OUT_SIZE] __attribute__((aligned(2048)));

#define B_BF16_ONE    0x3F80u
#define B_FP16_ONE    0x3C00u
#define B_BIAS_LOW32  B_FP16_ONE   /* scale=1.0 (FP16 convert), rest 0 */
#define B_EXPECTED_LO 0x00u
#define B_EXPECTED_HI 0x50u      /* 32.0 as FP16 = 0x5000 */

static int hmx_test_hmx_matmul_fp_bf16(void)
{
    err = 0;
    clear_accumulators();
    for (int i = 0; i < B_ACT_SIZE; i++) {
        B_act_buf[i] = B_BF16_ONE;
    }
    for (int i = 0; i < B_WEI_SIZE; i++) {
        B_wei_buf[i] = B_BF16_ONE;
    }
    for (int i = 0; i < 32; i++) {
        B_bias_buf[i] = B_BIAS_LOW32;
    }
    memset(B_out_buf, 0xff, sizeof(B_out_buf));

    /* bias = mxmem(r8) */
    asm volatile("r8 = %[bias]\n"
                 ".word 0x9208c3ff    /* bias=mxmem(r8) */\n"
                 :
                 : [bias] "r"(B_bias_buf)
                 : "r8", "memory");

    /*
     * activation.hf = mxmem(r2,r3) -- buffer holds BF16 values, but
     * the load itself is type-agnostic (raw 16-bit copy);
     * interpretation as BF16 happens later, gated by the weight-load's
     * Rs[6].
     */
    asm volatile("r2 = %[act]\n"
                 "r3 = #0x7c\n"
                 ".word 0x9202c3e4    /* activation.hf=mxmem(r2,r3) */\n"
                 :
                 : [act] "r"(B_act_buf)
                 : "r2", "r3", "memory");

    /*
     * weight.hf = mxmem(r4,r5) -- Rs[6]=1 selects BF16 for both weight
     * and activation decode in the matmul this triggers.
     */
    asm volatile("r4 = %[wei]\n"
                 "r4 = or(r4, #64)\n"
                 "r5 = #0xffff\n"
                 ".word 0x9204e5ef    /* weight.hf=mxmem(r4,r5) */\n"
                 :
                 : [wei] "r"(B_wei_buf)
                 : "r4", "r5", "memory");

    /*
     * cvt.hf=acc(r9), r9=0 (see hmx_matmul_fp_hf.c for the bit-field
     * derivation).
     */
    asm volatile("r9 = #0\n"
                 ".word 0xa6e9da10    /* cvt.hf=acc(r9) */\n"
                 :
                 :
                 : "r9", "memory");

    /* mxmem(r6,r7)=cvt -- reads the now-flushed convert result. */
    asm volatile("r6 = %[out]\n"
                 "r7 = #0\n"
                 ".word 0xa6e6c718    /* mxmem(r6,r7)=cvt */\n"
                 :
                 : [out] "r"(B_out_buf)
                 : "r6", "r7", "memory");

    for (int i = 0; i < B_OUT_SIZE; i += 2) {
        check32(B_out_buf[i], B_EXPECTED_LO);
        check32(B_out_buf[i + 1], B_EXPECTED_HI);
    }

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}

/* hmx_cvt_rs_bf16 */
#define CB_ACT_SIZE  1024  /* one crouton, as uint16_t: 2048 bytes / 2 */
#define CB_WEI_SIZE  1024  /* 16 vectors x 128 bytes, as uint16_t */
#define CB_OUT_SIZE  2048

static uint16_t CB_act_buf[CB_ACT_SIZE] __attribute__((aligned(2048)));
static uint16_t CB_wei_buf[CB_WEI_SIZE] __attribute__((aligned(128)));
static uint32_t CB_bias_buf[32] __attribute__((aligned(128)));
static uint8_t CB_out_buf[CB_OUT_SIZE] __attribute__((aligned(2048)));

#define CB_FP16_ONE    0x3C00u
#define CB_BF16_ONE    0x3F80u
/* scale=1.0 BF16-encoded (see file comment), everything else 0. */
#define CB_BIAS_LOW32  CB_BF16_ONE
#define CB_EXPECTED_LO 0x00u
#define CB_EXPECTED_HI 0x42u      /* 32.0 as BF16 = 0x4200 */

static int hmx_test_hmx_cvt_rs_bf16(void)
{
    err = 0;
    clear_accumulators();
    for (int i = 0; i < CB_ACT_SIZE; i++) {
        CB_act_buf[i] = CB_FP16_ONE;
    }
    for (int i = 0; i < CB_WEI_SIZE; i++) {
        CB_wei_buf[i] = CB_FP16_ONE;
    }
    for (int i = 0; i < 32; i++) {
        CB_bias_buf[i] = CB_BIAS_LOW32;
    }
    memset(CB_out_buf, 0xff, sizeof(CB_out_buf));

    /* bias = mxmem(r8) */
    asm volatile("r8 = %[bias]\n"
                 ".word 0x9208c3ff    /* bias=mxmem(r8) */\n"
                 :
                 : [bias] "r"(CB_bias_buf)
                 : "r8", "memory");

    /* activation.hf = mxmem(r2,r3) */
    asm volatile("r2 = %[act]\n"
                 "r3 = #0x7c\n"
                 ".word 0x9202c3e4    /* activation.hf=mxmem(r2,r3) */\n"
                 :
                 : [act] "r"(CB_act_buf)
                 : "r2", "r3", "memory");

    /* weight.hf = mxmem(r4,r5) -- triggers the FP matrix multiply. */
    asm volatile("r4 = %[wei]\n"
                 "r5 = #0xffff\n"
                 ".word 0x9204e5ef    /* weight.hf=mxmem(r4,r5) */\n"
                 :
                 : [wei] "r"(CB_wei_buf)
                 : "r4", "r5", "memory");

    /*
     * cvt.hf=acc(r9), r9=0x80 (Rs[7]=1: is_bf16_out; everything else
     * 0 -- see hmx_matmul_fp_hf.c for the other bit-field derivations).
     */
    asm volatile("r9 = #0x80\n"
                 ".word 0xa6e9da10    /* cvt.hf=acc(r9) */\n"
                 :
                 :
                 : "r9", "memory");

    /* mxmem(r6,r7)=cvt -- reads the now-flushed convert result. */
    asm volatile("r6 = %[out]\n"
                 "r7 = #0\n"
                 ".word 0xa6e6c718    /* mxmem(r6,r7)=cvt */\n"
                 :
                 : [out] "r"(CB_out_buf)
                 : "r6", "r7", "memory");

    for (int i = 0; i < CB_OUT_SIZE; i += 2) {
        check32(CB_out_buf[i], CB_EXPECTED_LO);
        check32(CB_out_buf[i + 1], CB_EXPECTED_HI);
    }

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}

/* hmx_matmul_fp_bf16_cvt */
#define BC_ACT_SIZE  1024  /* one crouton, as uint16_t: 2048 bytes / 2 */
#define BC_WEI_SIZE  1024  /* 16 vectors x 128 bytes, as uint16_t */
#define BC_OUT_SIZE  2048

static uint16_t BC_act_buf[BC_ACT_SIZE] __attribute__((aligned(2048)));
static uint16_t BC_wei_buf[BC_WEI_SIZE] __attribute__((aligned(128)));
static uint32_t BC_bias_buf[32] __attribute__((aligned(128)));
static uint8_t BC_out_buf[BC_OUT_SIZE] __attribute__((aligned(2048)));

#define BC_BF16_ONE    0x3F80u
/* scale=1.0, BF16-encoded (see file comment). */
#define BC_BIAS_LOW32  BC_BF16_ONE
#define BC_EXPECTED_LO 0x00u
#define BC_EXPECTED_HI 0x42u      /* 32.0 as BF16 = 0x4200 */

static int hmx_test_hmx_matmul_fp_bf16_cvt(void)
{
    err = 0;
    clear_accumulators();
    for (int i = 0; i < BC_ACT_SIZE; i++) {
        BC_act_buf[i] = BC_BF16_ONE;
    }
    for (int i = 0; i < BC_WEI_SIZE; i++) {
        BC_wei_buf[i] = BC_BF16_ONE;
    }
    for (int i = 0; i < 32; i++) {
        BC_bias_buf[i] = BC_BIAS_LOW32;
    }
    memset(BC_out_buf, 0xff, sizeof(BC_out_buf));

    /* bias = mxmem(r8) */
    asm volatile("r8 = %[bias]\n"
                 ".word 0x9208c3ff    /* bias=mxmem(r8) */\n"
                 :
                 : [bias] "r"(BC_bias_buf)
                 : "r8", "memory");

    /*
     * activation.hf = mxmem(r2,r3) -- buffer holds BF16 values, see
     * hmx_matmul_fp_bf16.c.
     */
    asm volatile("r2 = %[act]\n"
                 "r3 = #0x7c\n"
                 ".word 0x9202c3e4    /* activation.hf=mxmem(r2,r3) */\n"
                 :
                 : [act] "r"(BC_act_buf)
                 : "r2", "r3", "memory");

    /* weight.hf = mxmem(r4,r5) -- Rs[6]=1 selects BF16 matmul input. */
    asm volatile("r4 = %[wei]\n"
                 "r4 = or(r4, #64)\n"
                 "r5 = #0xffff\n"
                 ".word 0x9204e5ef    /* weight.hf=mxmem(r4,r5) */\n"
                 :
                 : [wei] "r"(BC_wei_buf)
                 : "r4", "r5", "memory");

    /* cvt.hf=acc(r9), r9=0x80 -- Rs[7]=1 selects BF16 convert output. */
    asm volatile("r9 = #0x80\n"
                 ".word 0xa6e9da10    /* cvt.hf=acc(r9) */\n"
                 :
                 :
                 : "r9", "memory");

    /* mxmem(r6,r7)=cvt -- reads the now-flushed convert result. */
    asm volatile("r6 = %[out]\n"
                 "r7 = #0\n"
                 ".word 0xa6e6c718    /* mxmem(r6,r7)=cvt */\n"
                 :
                 : [out] "r"(BC_out_buf)
                 : "r6", "r7", "memory");

    for (int i = 0; i < BC_OUT_SIZE; i += 2) {
        check32(BC_out_buf[i], BC_EXPECTED_LO);
        check32(BC_out_buf[i + 1], BC_EXPECTED_HI);
    }

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}

/* hmx_cvt_rs_negate_outbias */
#define NO_ACT_SIZE  1024  /* one crouton, as uint16_t: 2048 bytes / 2 */
#define NO_WEI_SIZE  1024  /* 16 vectors x 128 bytes, as uint16_t */
#define NO_OUT_SIZE  2048

static uint16_t NO_act_buf[NO_ACT_SIZE] __attribute__((aligned(2048)));
static uint16_t NO_wei_buf[NO_WEI_SIZE] __attribute__((aligned(128)));
/* bias.mxmem2 layout: 32 low words, then 32 high words at +128. */
static uint32_t NO_bias_buf[64] __attribute__((aligned(256)));
static uint8_t NO_out_buf[NO_OUT_SIZE] __attribute__((aligned(2048)));

#define NO_FP16_ONE      0x3C00u
#define NO_OUT_BIAS_FP16 0x5640u  /* 100.0 */
#define NO_NEGATE_BIT42  0x400u   /* bit 42 overall = bit 10 of hi word */
#define NO_EXPECTED_LO   0x40u
#define NO_EXPECTED_HI   0x54u    /* 68.0 as FP16 = 0x5440 */

static int hmx_test_hmx_cvt_rs_negate_outbias(void)
{
    err = 0;
    clear_accumulators();
    for (int i = 0; i < NO_ACT_SIZE; i++) {
        NO_act_buf[i] = NO_FP16_ONE;
    }
    for (int i = 0; i < NO_WEI_SIZE; i++) {
        NO_wei_buf[i] = NO_FP16_ONE;
    }
    for (int i = 0; i < 32; i++) {
        NO_bias_buf[i] = ((uint32_t)NO_OUT_BIAS_FP16 << 16) | NO_FP16_ONE;
        NO_bias_buf[32 + i] = NO_NEGATE_BIT42;
    }
    memset(NO_out_buf, 0xff, sizeof(NO_out_buf));

    /*
     * bias = mxmem2(r8) -- wide bias load, needed to reach negate
     * (bit 42, beyond the low-32-bit-only bias.mxmem every prior test
     * used).
     */
    asm volatile("r8 = %[bias]\n"
                 ".word 0x9208c3fe    /* bias=mxmem2(r8) */\n"
                 :
                 : [bias] "r"(NO_bias_buf)
                 : "r8", "memory");

    /* activation.hf = mxmem(r2,r3) */
    asm volatile("r2 = %[act]\n"
                 "r3 = #0x7c\n"
                 ".word 0x9202c3e4    /* activation.hf=mxmem(r2,r3) */\n"
                 :
                 : [act] "r"(NO_act_buf)
                 : "r2", "r3", "memory");

    /* weight.hf = mxmem(r4,r5) -- triggers the FP matrix multiply. */
    asm volatile("r4 = %[wei]\n"
                 "r5 = #0xffff\n"
                 ".word 0x9204e5ef    /* weight.hf=mxmem(r4,r5) */\n"
                 :
                 : [wei] "r"(NO_wei_buf)
                 : "r4", "r5", "memory");

    /*
     * cvt.hf=acc(r9), r9=0 (see hmx_matmul_fp_hf.c for the bit-field
     * derivation).
     */
    asm volatile("r9 = #0\n"
                 ".word 0xa6e9da10    /* cvt.hf=acc(r9) */\n"
                 :
                 :
                 : "r9", "memory");

    /* mxmem(r6,r7)=cvt -- reads the now-flushed convert result. */
    asm volatile("r6 = %[out]\n"
                 "r7 = #0\n"
                 ".word 0xa6e6c718    /* mxmem(r6,r7)=cvt */\n"
                 :
                 : [out] "r"(NO_out_buf)
                 : "r6", "r7", "memory");

    for (int i = 0; i < NO_OUT_SIZE; i += 2) {
        check32(NO_out_buf[i], NO_EXPECTED_LO);
        check32(NO_out_buf[i + 1], NO_EXPECTED_HI);
    }

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}

/* hmx_cvt_rs_shape_clamp */
#define SH_ACT_SIZE  1024  /* one crouton, as uint16_t: 2048 bytes / 2 */
#define SH_WEI_SIZE  1024  /* 16 vectors x 128 bytes, as uint16_t */
#define SH_OUT_SIZE  2048

static uint16_t SH_act_buf[SH_ACT_SIZE] __attribute__((aligned(2048)));
static uint16_t SH_wei_buf[SH_WEI_SIZE] __attribute__((aligned(128)));
/* bias.mxmem2 layout: 32 low words, then 32 high words at +128. */
static uint32_t SH_bias_buf[64] __attribute__((aligned(256)));
static uint8_t SH_out_buf[SH_OUT_SIZE] __attribute__((aligned(2048)));

#define SH_FP16_ONE   0x3C00u
#define SH_SHAPE_MIN  0x100u      /* shape=1 (min clamp) at bits[41:40] */
#define SH_EXPECTED   0x00u       /* 0.0 as FP16 = 0x0000, both bytes */

static int hmx_test_hmx_cvt_rs_shape_clamp(void)
{
    err = 0;
    clear_accumulators();
    for (int i = 0; i < SH_ACT_SIZE; i++) {
        SH_act_buf[i] = SH_FP16_ONE;
    }
    for (int i = 0; i < SH_WEI_SIZE; i++) {
        SH_wei_buf[i] = SH_FP16_ONE;
    }
    for (int i = 0; i < 32; i++) {
        SH_bias_buf[i] = SH_FP16_ONE;         /* scale=1.0, out_bias=0 */
        SH_bias_buf[32 + i] = SH_SHAPE_MIN;
    }
    memset(SH_out_buf, 0xff, sizeof(SH_out_buf));

    /*
     * bias = mxmem2(r8) -- wide bias load, needed to reach shape
     * (bits[41:40], beyond the low-32-bit-only bias.mxmem every prior
     * FP matmul test used).
     */
    asm volatile("r8 = %[bias]\n"
                 ".word 0x9208c3fe    /* bias=mxmem2(r8) */\n"
                 :
                 : [bias] "r"(SH_bias_buf)
                 : "r8", "memory");

    /* activation.hf = mxmem(r2,r3) */
    asm volatile("r2 = %[act]\n"
                 "r3 = #0x7c\n"
                 ".word 0x9202c3e4    /* activation.hf=mxmem(r2,r3) */\n"
                 :
                 : [act] "r"(SH_act_buf)
                 : "r2", "r3", "memory");

    /* weight.hf = mxmem(r4,r5) -- triggers the FP matrix multiply. */
    asm volatile("r4 = %[wei]\n"
                 "r5 = #0xffff\n"
                 ".word 0x9204e5ef    /* weight.hf=mxmem(r4,r5) */\n"
                 :
                 : [wei] "r"(SH_wei_buf)
                 : "r4", "r5", "memory");

    /*
     * cvt.hf=acc(r9), r9=0 (see hmx_matmul_fp_hf.c for the bit-field
     * derivation).
     */
    asm volatile("r9 = #0\n"
                 ".word 0xa6e9da10    /* cvt.hf=acc(r9) */\n"
                 :
                 :
                 : "r9", "memory");

    /* mxmem(r6,r7)=cvt -- reads the now-flushed convert result. */
    asm volatile("r6 = %[out]\n"
                 "r7 = #0\n"
                 ".word 0xa6e6c718    /* mxmem(r6,r7)=cvt */\n"
                 :
                 : [out] "r"(SH_out_buf)
                 : "r6", "r7", "memory");

    for (int i = 0; i < SH_OUT_SIZE; i++) {
        check32(SH_out_buf[i], SH_EXPECTED);
    }

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}

int main(void)
{
    if (hmx_test_hmx_matmul_fp_hf()) {
        return 1;
    }
    if (hmx_test_hmx_matmul_fp_bf16()) {
        return 1;
    }
    if (hmx_test_hmx_cvt_rs_bf16()) {
        return 1;
    }
    if (hmx_test_hmx_matmul_fp_bf16_cvt()) {
        return 1;
    }
    if (hmx_test_hmx_cvt_rs_negate_outbias()) {
        return 1;
    }
    if (hmx_test_hmx_cvt_rs_shape_clamp()) {
        return 1;
    }
    return 0;
}
