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
    asm volatile(".word 0xa6e0c011    /* mxclracc */" : : : "memory");
}

/* hmx_bias */
#define HMX_BIAS_MX_COLS 32

static uint32_t HMX_BIAS_src[HMX_BIAS_MX_COLS] __attribute__((aligned(128)));
static uint32_t HMX_BIAS_dst[HMX_BIAS_MX_COLS] __attribute__((aligned(128)));

static int hmx_test_hmx_bias(void)
{
    err = 0;
    clear_accumulators();
    for (int i = 0; i < HMX_BIAS_MX_COLS; i++) {
        HMX_BIAS_src[i] = 0x10000000 * (i + 1) + i;
        HMX_BIAS_dst[i] = 0xdeadbeef;
    }

    /* bias = mxmem(r2), r2 = &HMX_BIAS_src */
    asm volatile("r2 = %[src]\n"
                 ".word 0x9202c3ff    /* bias=mxmem(r2) */\n"
                 :
                 : [src] "r"(HMX_BIAS_src)
                 : "r2", "memory");

    /* mxmem(r3) = bias, r3 = &HMX_BIAS_dst */
    asm volatile("r3 = %[dst]\n"
                 ".word 0xa6e3c010    /* mxmem(r3)=bias */\n"
                 :
                 : [dst] "r"(HMX_BIAS_dst)
                 : "r3", "memory");

    for (int i = 0; i < HMX_BIAS_MX_COLS; i++) {
        check32(HMX_BIAS_dst[i], HMX_BIAS_src[i]);
    }

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}

/* hmx_matmul_fxp */
#define F_ACT_SIZE  2048  /* one crouton: 64 spatial x 32 channels */
#define F_WEI_SIZE  1024  /* 8 vectors x 128 bytes: 32 input channels, byte */
#define F_OUT_SIZE  2048

static uint8_t F_act_buf[F_ACT_SIZE] __attribute__((aligned(2048)));
static uint8_t F_wei_buf[F_WEI_SIZE] __attribute__((aligned(128)));
static uint32_t F_bias_buf[32] __attribute__((aligned(128)));
static uint8_t F_out_buf[F_OUT_SIZE] __attribute__((aligned(2048)));

/*
 * Every activation byte is 3, every weight byte is 5.  With a single
 * spatial tap and no group convolution, each of the 64x32 accumulator
 * cells sums 32 input-channel MACs of 3*5=15, giving 480 uniformly.
 *
 * The bias register (input_bias=0, exponent=20, shape=0, scale=0x800,
 * out_bias=0, encoded directly against hmx_state.h's
 * hmx_bias_exponent()/hmx_bias_scale()/hmx_bias_shape()/
 * hmx_bias_output_bias_unsigned() extraction formulas -- NOT via
 * pack_fxp_bias()-style helpers, which invert the output-bias field
 * on the way in to match real hardware's wire encoding, and would
 * need their own out_bias to be pre-inverted to land on out_bias=0
 * here) was chosen, by running the real hmx_u8_cvt() algorithm
 * natively, to map accumulator value 480 through hmx_u8_cvt() to
 * exactly 480 again -- an identity point for this specific
 * accumulator value, not a general identity transform. The legacy
 * convert-and-store path then right-shifts the 12-bit hmx_u8_cvt()
 * result by 4 before storing the byte, so the final stored value is
 * 480 >> 4 = 30.
 */
#define F_EXPECTED_ACC     480
#define F_EXPECTED_BYTE    (F_EXPECTED_ACC >> 4)
#define F_BIAS_LOW32       0x5000u

static int hmx_test_hmx_matmul_fxp(void)
{
    err = 0;
    clear_accumulators();
    memset(F_act_buf, 3, sizeof(F_act_buf));
    memset(F_wei_buf, 5, sizeof(F_wei_buf));
    for (int i = 0; i < 32; i++) {
        F_bias_buf[i] = F_BIAS_LOW32;
    }
    memset(F_out_buf, 0xff, sizeof(F_out_buf));

    /* bias = mxmem(r8) */
    asm volatile("r8 = %[bias]\n"
                 ".word 0x9208c3ff    /* bias=mxmem(r8) */\n"
                 :
                 : [bias] "r"(F_bias_buf)
                 : "r8", "memory");

    /*
     * activation.ub = mxmem(r2,r3) : r3 selects the full [0,32) channel
     * range and a single spatial tap (hmx_compute_act_params() decodes
     * the fields).
     */
    asm volatile("r2 = %[act]\n"
                 "r3 = #0x7c\n"
                 ".word 0x9202c3ec    /* activation.ub=mxmem(r2,r3) */\n"
                 :
                 : [act] "r"(F_act_buf)
                 : "r2", "r3", "memory");

    /* weight.b = mxmem(r4,r5) -- triggers the FXP matrix multiply. */
    asm volatile("r4 = %[wei]\n"
                 "r5 = #0xffff\n"
                 ".word 0x9204e5e0    /* weight.b=mxmem(r4,r5) */\n"
                 :
                 : [wei] "r"(F_wei_buf)
                 : "r4", "r5", "memory");

    /* mxmem(r6,r7):after:sat.ub=acc */
    asm volatile("r6 = %[out]\n"
                 "r7 = #0\n"
                 ".word 0xa6e6c704    "
                 "/* mxmem(r6,r7):after:sat.ub=acc */\n"
                 :
                 : [out] "r"(F_out_buf)
                 : "r6", "r7", "memory");

    for (int i = 0; i < F_OUT_SIZE; i++) {
        check32(F_out_buf[i], F_EXPECTED_BYTE);
    }

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}

/* hmx_matmul_fxp_nibble */
#define N_ACT_SIZE  2048  /* one crouton: 64 spatial x 32 channels */
#define N_WEI_SIZE  512   /* 4 vectors x 128 bytes: 32 channels, cpv=8 */
#define N_OUT_SIZE  2048

static uint8_t N_act_buf[N_ACT_SIZE] __attribute__((aligned(2048)));
static uint8_t N_wei_buf[N_WEI_SIZE] __attribute__((aligned(128)));
static uint32_t N_bias_buf[32] __attribute__((aligned(128)));
static uint8_t N_out_buf[N_OUT_SIZE] __attribute__((aligned(2048)));

/*
 * Every activation byte is 3. Every weight byte is 0x33: nibble
 * unpack sign-extends each 4-bit nibble, and 0b0011 (3) is already
 * positive, so both nibbles of every weight byte decode to weight
 * value 3 for every one of the 32 input channels' stream indices.
 *
 * Single spatial tap, no group convolution (same setup as
 * hmx_matmul_fxp.c): each of the 64x32 accumulator cells sums 32
 * input-channel MACs of 3*3=9, giving 288 uniformly.
 *
 * Reusing hmx_matmul_fxp.c's bias register (exponent=20, scale=0x800,
 * out_bias=0): verified natively that this maps accumulator value 288
 * through hmx_u8_cvt() to exactly 288 again (the same identity point
 * that worked for 480 -- this configuration is linear across the
 * range tested, not just at 480). The legacy convert-and-store path
 * right-shifts that 12-bit result by 4 before storing the byte, so
 * the expected stored value is 288 >> 4 = 18.
 */
#define N_EXPECTED_ACC     288
#define N_EXPECTED_BYTE    (N_EXPECTED_ACC >> 4)
#define N_BIAS_LOW32       0x5000u

static int hmx_test_hmx_matmul_fxp_nibble(void)
{
    err = 0;
    clear_accumulators();
    memset(N_act_buf, 3, sizeof(N_act_buf));
    memset(N_wei_buf, 0x33, sizeof(N_wei_buf));
    for (int i = 0; i < 32; i++) {
        N_bias_buf[i] = N_BIAS_LOW32;
    }
    memset(N_out_buf, 0xff, sizeof(N_out_buf));

    /* bias = mxmem(r8) */
    asm volatile("r8 = %[bias]\n"
                 ".word 0x9208c3ff    /* bias=mxmem(r8) */\n"
                 :
                 : [bias] "r"(N_bias_buf)
                 : "r8", "memory");

    /* activation.ub = mxmem(r2,r3) */
    asm volatile("r2 = %[act]\n"
                 "r3 = #0x7c\n"
                 ".word 0x9202c3ec    /* activation.ub=mxmem(r2,r3) */\n"
                 :
                 : [act] "r"(N_act_buf)
                 : "r2", "r3", "memory");

    /* weight.n = mxmem(r4,r5) -- triggers the FXP matrix multiply. */
    asm volatile("r4 = %[wei]\n"
                 "r5 = #0xffff\n"
                 ".word 0x9204e5e1    /* weight.n=mxmem(r4,r5) */\n"
                 :
                 : [wei] "r"(N_wei_buf)
                 : "r4", "r5", "memory");

    /* mxmem(r6,r7):after:sat.ub=acc */
    asm volatile("r6 = %[out]\n"
                 "r7 = #0\n"
                 ".word 0xa6e6c704    "
                 "/* mxmem(r6,r7):after:sat.ub=acc */\n"
                 :
                 : [out] "r"(N_out_buf)
                 : "r6", "r7", "memory");

    for (int i = 0; i < N_OUT_SIZE; i++) {
        check32(N_out_buf[i], N_EXPECTED_BYTE);
    }

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}

/* hmx_matmul_fxp_dm */
#define D_ACT_SIZE  2048  /* one crouton: 64 spatial x 32 channels */
#define D_WEI_SIZE  1024  /* 8 vectors x 128 bytes: 32 channels, byte */
#define D_OUT_SIZE  2048

static uint8_t D_act_buf[D_ACT_SIZE] __attribute__((aligned(2048)));
static uint8_t D_wei_buf[D_WEI_SIZE] __attribute__((aligned(128)));
static uint32_t D_bias_buf[32] __attribute__((aligned(128)));
static uint8_t D_out_buf[D_OUT_SIZE] __attribute__((aligned(2048)));

/*
 * Same act=3/weight=5 uniform pattern as hmx_matmul_fxp.c. DM's
 * spatial mask (HMX_SPATIAL_MASK_BITS_CM=0x7E0) has the same 6-bit
 * hamming weight as SM's (0x783), i.e. the same 64 spatial positions,
 * so the expected accumulator (480, uniformly) and bias/expected byte
 * (30) are identical -- only the address *arithmetic* getting there
 * differs.
 */
#define D_EXPECTED_ACC     480
#define D_EXPECTED_BYTE    (D_EXPECTED_ACC >> 4)
#define D_BIAS_LOW32       0x5000u

static int hmx_test_hmx_matmul_fxp_dm(void)
{
    err = 0;
    clear_accumulators();
    memset(D_act_buf, 3, sizeof(D_act_buf));
    memset(D_wei_buf, 5, sizeof(D_wei_buf));
    for (int i = 0; i < 32; i++) {
        D_bias_buf[i] = D_BIAS_LOW32;
    }
    memset(D_out_buf, 0xff, sizeof(D_out_buf));

    /* bias = mxmem(r8) */
    asm volatile("r8 = %[bias]\n"
                 ".word 0x9208c3ff    /* bias=mxmem(r8) */\n"
                 :
                 : [bias] "r"(D_bias_buf)
                 : "r8", "memory");

    /*
     * activation.ub = mxmem(r2,r3):cm -- r3=0x1f selects the full
     * [0,32) channel range and a single spatial tap in DM format
     * (see this file's header comment).
     */
    asm volatile("r2 = %[act]\n"
                 "r3 = #0x1f\n"
                 ".word 0x9202c3ed    /* activation.ub=mxmem(r2,r3):cm */\n"
                 :
                 : [act] "r"(D_act_buf)
                 : "r2", "r3", "memory");

    /*
     * weight.b = mxmem(r4,r5) -- format-agnostic; reads the DM mode
     * hmx_act_load() just latched.
     */
    asm volatile("r4 = %[wei]\n"
                 "r5 = #0xffff\n"
                 ".word 0x9204e5e0    /* weight.b=mxmem(r4,r5) */\n"
                 :
                 : [wei] "r"(D_wei_buf)
                 : "r4", "r5", "memory");

    /* mxmem(r6,r7):after:sat.ub:cm=acc */
    asm volatile("r6 = %[out]\n"
                 "r7 = #0\n"
                 ".word 0xa6e6c705    "
                 "/* mxmem(r6,r7):after:sat.ub:cm=acc */\n"
                 :
                 : [out] "r"(D_out_buf)
                 : "r6", "r7", "memory");

    for (int i = 0; i < D_OUT_SIZE; i++) {
        check32(D_out_buf[i], D_EXPECTED_BYTE);
    }

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}

/* hmx_matmul_fxp_signed_crumb */
#define SC_ACT_SIZE  2048  /* one crouton: 64 spatial x 32 channels */
#define SC_WEI_SIZE  256   /* 2 vectors x 128 bytes: 32 channels, cpv=16 */
#define SC_OUT_SIZE  2048

static uint8_t SC_act_buf[SC_ACT_SIZE] __attribute__((aligned(2048)));
static uint8_t SC_wei_buf[SC_WEI_SIZE] __attribute__((aligned(128)));
static uint32_t SC_bias_buf[32] __attribute__((aligned(128)));
static uint8_t SC_out_buf[SC_OUT_SIZE] __attribute__((aligned(2048)));

/*
 * Every activation byte is 3. Every weight byte is 0x00: each 2-bit
 * crumb is 0b00, and the signed-crumb lookup table maps crumb value 0
 * to weight value 2 (not 0 -- this is the point of testing it: a
 * plain sign-extending unpack would treat 0b00 as weight 0, which
 * would silently pass even a broken lookup-table implementation that
 * just zeroed everything).
 *
 * Single spatial tap, no group convolution (same setup as
 * hmx_matmul_fxp.c): each of the 64x32 accumulator cells sums 32
 * input-channel MACs of 3*2=6, giving 192 uniformly.
 *
 * Reusing hmx_matmul_fxp.c's bias register (exponent=20, scale=0x800,
 * out_bias=0): verified natively that this identity configuration
 * also holds for accumulator value 192. Expected stored byte:
 * 192 >> 4 = 12.
 */
#define SC_EXPECTED_ACC     192
#define SC_EXPECTED_BYTE    (SC_EXPECTED_ACC >> 4)
#define SC_BIAS_LOW32       0x5000u

static int hmx_test_hmx_matmul_fxp_signed_crumb(void)
{
    err = 0;
    clear_accumulators();
    memset(SC_act_buf, 3, sizeof(SC_act_buf));
    memset(SC_wei_buf, 0x00, sizeof(SC_wei_buf));
    for (int i = 0; i < 32; i++) {
        SC_bias_buf[i] = SC_BIAS_LOW32;
    }
    memset(SC_out_buf, 0xff, sizeof(SC_out_buf));

    /* bias = mxmem(r8) */
    asm volatile("r8 = %[bias]\n"
                 ".word 0x9208c3ff    /* bias=mxmem(r8) */\n"
                 :
                 : [bias] "r"(SC_bias_buf)
                 : "r8", "memory");

    /* activation.ub = mxmem(r2,r3) */
    asm volatile("r2 = %[act]\n"
                 "r3 = #0x7c\n"
                 ".word 0x9202c3ec    /* activation.ub=mxmem(r2,r3) */\n"
                 :
                 : [act] "r"(SC_act_buf)
                 : "r2", "r3", "memory");

    /* weight.sc = mxmem(r4,r5) -- triggers the FXP matrix multiply. */
    asm volatile("r4 = %[wei]\n"
                 "r5 = #0xffff\n"
                 ".word 0x9204e5f0    /* weight.sc=mxmem(r4,r5) */\n"
                 :
                 : [wei] "r"(SC_wei_buf)
                 : "r4", "r5", "memory");

    /* mxmem(r6,r7):after:sat.ub=acc */
    asm volatile("r6 = %[out]\n"
                 "r7 = #0\n"
                 ".word 0xa6e6c704    "
                 "/* mxmem(r6,r7):after:sat.ub=acc */\n"
                 :
                 : [out] "r"(SC_out_buf)
                 : "r6", "r7", "memory");

    for (int i = 0; i < SC_OUT_SIZE; i++) {
        check32(SC_out_buf[i], SC_EXPECTED_BYTE);
    }

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}

/* hmx_matmul_fxp_sign_magnitude */
#define SM_ACT_SIZE  2048  /* one crouton: 64 spatial x 32 channels */
#define SM_WEI_SIZE  1024  /* 8 vectors x 128 bytes: 32 channels, cpv=4 */
#define SM_OUT_SIZE  2048

static uint8_t SM_act_buf[SM_ACT_SIZE] __attribute__((aligned(2048)));
static uint8_t SM_wei_buf[SM_WEI_SIZE] __attribute__((aligned(128)));
static uint32_t SM_bias_buf[32] __attribute__((aligned(128)));
static uint8_t SM_out_buf[SM_OUT_SIZE] __attribute__((aligned(2048)));

/*
 * Every activation byte is 3. Every weight byte is 0x80, which
 * hmx_extract_weights_sm() decodes to weight value -1 for every one
 * of the 32 input channels.
 *
 * Single spatial tap, no group convolution (same setup as every
 * other HMX matmul test): each of the 64x32 accumulator cells sums
 * 32 input-channel MACs of 3*(-1)=-3, giving -96 uniformly.
 *
 * Reusing the identity bias register (exponent=20, scale=0x800,
 * out_bias=0) with the *non*-saturating convert (M8_mxcvtr_ub, not
 * _sat_): a saturating convert would just clamp -96 to 0, which
 * wouldn't distinguish a correctly-signed -96 from an
 * incorrectly-zeroed or garbage accumulator. Non-saturating masks to
 * 12 bits (two's complement): -96 mod 4096 = 4000 = 0xFA0, and the
 * legacy convert-and-store path right-shifts that by 4 before storing
 * the byte, so the expected stored value is 0xFA0 >> 4 = 0xFA = 250 --
 * verified against the real hmx_u8_cvt()/hmx_sat_to_max() algorithm
 * run natively, not hand-derived.
 */
#define SM_EXPECTED_BYTE    250
#define SM_BIAS_LOW32       0x5000u

static int hmx_test_hmx_matmul_fxp_sign_magnitude(void)
{
    err = 0;
    clear_accumulators();
    memset(SM_act_buf, 3, sizeof(SM_act_buf));
    memset(SM_wei_buf, 0x80, sizeof(SM_wei_buf));
    for (int i = 0; i < 32; i++) {
        SM_bias_buf[i] = SM_BIAS_LOW32;
    }
    memset(SM_out_buf, 0xff, sizeof(SM_out_buf));

    /* bias = mxmem(r8) */
    asm volatile("r8 = %[bias]\n"
                 ".word 0x9208c3ff    /* bias=mxmem(r8) */\n"
                 :
                 : [bias] "r"(SM_bias_buf)
                 : "r8", "memory");

    /* activation.ub = mxmem(r2,r3) */
    asm volatile("r2 = %[act]\n"
                 "r3 = #0x7c\n"
                 ".word 0x9202c3ec    /* activation.ub=mxmem(r2,r3) */\n"
                 :
                 : [act] "r"(SM_act_buf)
                 : "r2", "r3", "memory");

    /* weight.sm = mxmem(r4,r5) -- triggers the FXP matrix multiply. */
    asm volatile("r4 = %[wei]\n"
                 "r5 = #0xffff\n"
                 ".word 0x9204e5f1    /* weight.sm=mxmem(r4,r5) */\n"
                 :
                 : [wei] "r"(SM_wei_buf)
                 : "r4", "r5", "memory");

    /* mxmem(r6,r7):after.ub=acc (non-saturating) */
    asm volatile("r6 = %[out]\n"
                 "r7 = #0\n"
                 ".word 0xa6e6c706    /* mxmem(r6,r7):after.ub=acc */\n"
                 :
                 : [out] "r"(SM_out_buf)
                 : "r6", "r7", "memory");

    for (int i = 0; i < SM_OUT_SIZE; i++) {
        check32(SM_out_buf[i], SM_EXPECTED_BYTE);
    }

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}

/* hmx_matmul_fxp_bit */
#define BT_ACT_SIZE  2048  /* one crouton: 64 spatial x 32 channels */
#define BT_WEI_SIZE  128   /* 1 vector: 32 channels, cpv=32 */
#define BT_OUT_SIZE  2048

static uint8_t BT_act_buf[BT_ACT_SIZE] __attribute__((aligned(2048)));
static uint8_t BT_wei_buf[BT_WEI_SIZE] __attribute__((aligned(128)));
static uint32_t BT_bias_buf[32] __attribute__((aligned(128)));
static uint8_t BT_out_buf[BT_OUT_SIZE] __attribute__((aligned(2048)));

/*
 * Every activation byte is 3. Every weight bit is 1: with cpv=32,
 * every one of the 32 channels' stream indices reads a different bit
 * position across the vector's 4 words, so making every extraction
 * come out to weight=1 needs every bit of the whole 128-byte vector
 * set (0xFF throughout), not just one word.
 *
 * Single spatial tap, no group convolution: each of the 64x32
 * accumulator cells sums 32 input-channel MACs of 3*1=3, giving 96
 * uniformly. Reusing the identity bias register (exponent=20,
 * scale=0x800, out_bias=0): verified natively that it also holds for
 * accumulator value 96. Expected stored byte: 96 >> 4 = 6.
 */
#define BT_EXPECTED_ACC     96
#define BT_EXPECTED_BYTE    (BT_EXPECTED_ACC >> 4)
#define BT_BIAS_LOW32       0x5000u

static int hmx_test_hmx_matmul_fxp_bit(void)
{
    err = 0;
    clear_accumulators();
    memset(BT_act_buf, 3, sizeof(BT_act_buf));
    memset(BT_wei_buf, 0xff, sizeof(BT_wei_buf));
    for (int i = 0; i < 32; i++) {
        BT_bias_buf[i] = BT_BIAS_LOW32;
    }
    memset(BT_out_buf, 0xff, sizeof(BT_out_buf));

    /* bias = mxmem(r8) */
    asm volatile("r8 = %[bias]\n"
                 ".word 0x9208c3ff    /* bias=mxmem(r8) */\n"
                 :
                 : [bias] "r"(BT_bias_buf)
                 : "r8", "memory");

    /* activation.ub = mxmem(r2,r3) */
    asm volatile("r2 = %[act]\n"
                 "r3 = #0x7c\n"
                 ".word 0x9202c3ec    /* activation.ub=mxmem(r2,r3) */\n"
                 :
                 : [act] "r"(BT_act_buf)
                 : "r2", "r3", "memory");

    /* weight.ubit = mxmem(r4,r5) -- triggers the FXP matrix multiply. */
    asm volatile("r4 = %[wei]\n"
                 "r5 = #0xffff\n"
                 ".word 0x9204e5e3    /* weight.ubit=mxmem(r4,r5) */\n"
                 :
                 : [wei] "r"(BT_wei_buf)
                 : "r4", "r5", "memory");

    /* mxmem(r6,r7):after:sat.ub=acc */
    asm volatile("r6 = %[out]\n"
                 "r7 = #0\n"
                 ".word 0xa6e6c704    "
                 "/* mxmem(r6,r7):after:sat.ub=acc */\n"
                 :
                 : [out] "r"(BT_out_buf)
                 : "r6", "r7", "memory");

    for (int i = 0; i < BT_OUT_SIZE; i++) {
        check32(BT_out_buf[i], BT_EXPECTED_BYTE);
    }

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}

/* hmx_matmul_fxp_signed_bit */
#define SB_ACT_SIZE  2048  /* one crouton: 64 spatial x 32 channels */
#define SB_WEI_SIZE  128   /* 1 vector: 32 channels, cpv=32 */
#define SB_OUT_SIZE  2048

static uint8_t SB_act_buf[SB_ACT_SIZE] __attribute__((aligned(2048)));
static uint8_t SB_wei_buf[SB_WEI_SIZE] __attribute__((aligned(128)));
static uint32_t SB_bias_buf[32] __attribute__((aligned(128)));
static uint8_t SB_out_buf[SB_OUT_SIZE] __attribute__((aligned(2048)));

/*
 * Every activation byte is 3. Every weight bit is 1 (whole vector
 * 0xFF, same reasoning as hmx_matmul_fxp_bit.c's cpv=32 addressing),
 * which hmx_extract_weights_signed_bit() decodes to weight -1 (bit=1
 * is the *negative* case, the opposite of the unsigned-bit type's
 * plain bit=1 -> weight=1).
 *
 * Single spatial tap, no group convolution: each of the 64x32
 * accumulator cells sums 32 input-channel MACs of 3*(-1)=-3, giving
 * -96 uniformly -- same accumulator value as
 * hmx_matmul_fxp_sign_magnitude.c, so the same non-saturating convert
 * (M8_mxcvtr_ub) and expected byte (250) apply; see that file for the
 * two's-complement masking rationale.
 */
#define SB_EXPECTED_BYTE    250
#define SB_BIAS_LOW32       0x5000u

static int hmx_test_hmx_matmul_fxp_signed_bit(void)
{
    err = 0;
    clear_accumulators();
    memset(SB_act_buf, 3, sizeof(SB_act_buf));
    memset(SB_wei_buf, 0xff, sizeof(SB_wei_buf));
    for (int i = 0; i < 32; i++) {
        SB_bias_buf[i] = SB_BIAS_LOW32;
    }
    memset(SB_out_buf, 0xff, sizeof(SB_out_buf));

    /* bias = mxmem(r8) */
    asm volatile("r8 = %[bias]\n"
                 ".word 0x9208c3ff    /* bias=mxmem(r8) */\n"
                 :
                 : [bias] "r"(SB_bias_buf)
                 : "r8", "memory");

    /* activation.ub = mxmem(r2,r3) */
    asm volatile("r2 = %[act]\n"
                 "r3 = #0x7c\n"
                 ".word 0x9202c3ec    /* activation.ub=mxmem(r2,r3) */\n"
                 :
                 : [act] "r"(SB_act_buf)
                 : "r2", "r3", "memory");

    /* weight.sbit = mxmem(r4,r5) -- triggers the FXP matrix multiply. */
    asm volatile("r4 = %[wei]\n"
                 "r5 = #0xffff\n"
                 ".word 0x9204e5e4    /* weight.sbit=mxmem(r4,r5) */\n"
                 :
                 : [wei] "r"(SB_wei_buf)
                 : "r4", "r5", "memory");

    /* mxmem(r6,r7):after.ub=acc (non-saturating) */
    asm volatile("r6 = %[out]\n"
                 "r7 = #0\n"
                 ".word 0xa6e6c706    /* mxmem(r6,r7):after.ub=acc */\n"
                 :
                 : [out] "r"(SB_out_buf)
                 : "r6", "r7", "memory");

    for (int i = 0; i < SB_OUT_SIZE; i++) {
        check32(SB_out_buf[i], SB_EXPECTED_BYTE);
    }

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}

int main(void)
{
    if (hmx_test_hmx_bias()) {
        return 1;
    }
    if (hmx_test_hmx_matmul_fxp()) {
        return 1;
    }
    if (hmx_test_hmx_matmul_fxp_nibble()) {
        return 1;
    }
    if (hmx_test_hmx_matmul_fxp_dm()) {
        return 1;
    }
    if (hmx_test_hmx_matmul_fxp_signed_crumb()) {
        return 1;
    }
    if (hmx_test_hmx_matmul_fxp_sign_magnitude()) {
        return 1;
    }
    if (hmx_test_hmx_matmul_fxp_bit()) {
        return 1;
    }
    if (hmx_test_hmx_matmul_fxp_signed_bit()) {
        return 1;
    }
    return 0;
}
