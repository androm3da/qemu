/*
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * Tests for the HVX IEEE floating point instructions introduced in v79.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <hexagon_types.h>
#include <hvx_hexagon_protos.h>

int err;
#include "hvx_misc.h"
#include "hex_test.h"

/*
 * The vcvt2 instructions differ from vcvt in how elements are interleaved
 * between the vectors of a pair (widening) or the sources (narrowing):
 *
 *    Vdd.v[0].hf[2*i]   = OP(Vu.FROM[4*i]);
 *    Vdd.v[0].hf[2*i+1] = OP(Vu.FROM[4*i+2]);
 *    Vdd.v[1].hf[2*i]   = OP(Vu.FROM[4*i+1]);
 *    Vdd.v[1].hf[2*i+1] = OP(Vu.FROM[4*i+3]);
 *
 *    Vd.TO[4*i]   = OP(Vu.hf[2*i]);
 *    Vd.TO[4*i+1] = OP(Vv.hf[2*i]);
 *    Vd.TO[4*i+2] = OP(Vu.hf[2*i+1]);
 *    Vd.TO[4*i+3] = OP(Vv.hf[2*i+1]);
 */
#define DEF_TEST_VCVT2_WIDEN(FROM, MUL, ADD) \
    static void test_vcvt2_hf_##FROM(void) \
    { \
        HVX_VectorPair *hvx_output = (HVX_VectorPair *)&output[0]; \
        HVX_Vector buffer; \
        memset(expect, 0, sizeof(expect)); \
        for (int i = 0; i < MAX_VEC_SIZE_BYTES; i++) { \
            ((MMVector *)&buffer)->FROM[i] = i * MUL + ADD; \
            expect[i % 2].hf[2 * (i / 4) + ((i % 4) / 2)] = \
                raw_hf((_Float16)(((MMVector *)&buffer)->FROM[i])); \
        } \
        *hvx_output = Q6_Whf_vcvt2_V##FROM(buffer); \
        check_output_hf(__LINE__, 2); \
    }

DEF_TEST_VCVT2_WIDEN(ub, 3, 1)
DEF_TEST_VCVT2_WIDEN(b, 5, 7)

static const struct {
    uint16_t hf;
    uint8_t ub;
    int8_t b;
} cvt2_narrow_cases[] = {
    { HF_QNaN, UINT8_MAX, INT8_MAX },
    { HF_SNaN, UINT8_MAX, INT8_MAX },
    { HF_QNaN_neg, UINT8_MAX, INT8_MAX },
    { HF_INF, UINT8_MAX, INT8_MAX },
    { HF_INF_neg, 0, INT8_MIN },
    { HF_zero, 0, 0 },
    { HF_zero_neg, 0, 0 },
    { HF_neg_two, 0, -2 },
    { HF_one, 1, 1 },
    { 0x3e00 /* 1.5 */, 2, 2 },
    { 0x4100 /* 2.5 */, 2, 2 },
    { 0x4300 /* 3.5 */, 4, 4 },
    { 0x5640 /* 100 */, 100, 100 },
    { 0x5a40 /* 200 */, 200, INT8_MAX },
    { 0x5c00 /* 256 */, UINT8_MAX, INT8_MAX },
    { 0xd640 /* -100 */, 0, -100 },
    { 0xda40 /* -200 */, 0, INT8_MIN },
};
#define NUM_CVT2_NARROW_CASES ARRAY_SIZE(cvt2_narrow_cases)

#define DEF_TEST_VCVT2_NARROW(TO) \
    static void test_vcvt2_##TO##_hf(void) \
    { \
        HVX_Vector *hvx_output = (HVX_Vector *)&output[0]; \
        HVX_Vector buffers[2]; \
        memset(expect, 0, sizeof(expect)); \
        for (int j = 0; j < MAX_VEC_SIZE_BYTES / 2; j++) { \
            for (int s = 0; s < 2; s++) { \
                int c = (2 * j + s) % NUM_CVT2_NARROW_CASES; \
                ((MMVector *)&buffers[s])->hf[j] = cvt2_narrow_cases[c].hf; \
                expect[0].TO[4 * (j / 2) + 2 * (j % 2) + s] = \
                    cvt2_narrow_cases[c].TO; \
            } \
        } \
        *hvx_output = Q6_V##TO##_vcvt2_VhfVhf(buffers[0], buffers[1]); \
        check_output_##TO(__LINE__, 1); \
    }

DEF_TEST_VCVT2_NARROW(ub)
DEF_TEST_VCVT2_NARROW(b)

/*
 * FP8 instructions.
 *
 * FP8 is E4M3FNUZ: 1 sign bit, 4 exponent bits with a bias of 8 and 3
 * fraction bits.  There are no infinities and no negative zero; 0x80 is the
 * only NaN and 0x7f (240) is the largest normal number.
 *
 * The reference model below works on doubles, which hold every operation's
 * result exactly, so rounding to half precision happens only once.
 */
#define F8_NAN 0x80
#define HF_NAN 0x7fff

static bool hf_is_nan(uint16_t h)
{
    return (h & 0x7c00) == 0x7c00 && (h & 0x3ff);
}

static double f8_to_double(uint8_t f8)
{
    int exp = (f8 >> 3) & 0xf;
    int frac = f8 & 7;
    double val = exp ? ldexp(8 + frac, exp - 11) : ldexp(frac, -10);

    return f8 & 0x80 ? -val : val;
}

static uint16_t double_to_hf(double d)
{
    return raw_hf((_Float16)d);
}

static uint16_t ref_f8_to_hf(uint8_t f8)
{
    return f8 == F8_NAN ? HF_NAN : double_to_hf(f8_to_double(f8));
}

static uint16_t ref_mpy(uint8_t a, uint8_t b)
{
    if (a == F8_NAN || b == F8_NAN) {
        return HF_NAN;
    }
    return double_to_hf(f8_to_double(a) * f8_to_double(b));
}

static uint16_t ref_add(uint8_t a, uint8_t b)
{
    if (a == F8_NAN || b == F8_NAN) {
        return HF_NAN;
    }
    return double_to_hf(f8_to_double(a) + f8_to_double(b));
}

static uint16_t ref_sub(uint8_t a, uint8_t b)
{
    if (a == F8_NAN || b == F8_NAN) {
        return HF_NAN;
    }
    return double_to_hf(f8_to_double(a) - f8_to_double(b));
}

static uint16_t ref_mpy_acc(uint8_t a, uint8_t b, uint16_t acc)
{
    if (a == F8_NAN || b == F8_NAN || hf_is_nan(acc)) {
        return HF_NAN;
    }
    return double_to_hf(f8_to_double(a) * f8_to_double(b) +
                        (double)*(_Float16 *)&acc);
}

/* The magnitudes of the FP8 numbers, which increase with the encoding */
static double f8_mag[0x80];

static void init_f8_mag(void)
{
    for (int code = 0; code < ARRAY_SIZE(f8_mag); code++) {
        f8_mag[code] = f8_to_double(code);
    }
}

/* The FP8 number nearest to a half precision one, ties to even */
static uint8_t ref_hf_to_f8(uint16_t h, bool sat)
{
    double val = (double)*(_Float16 *)&h;
    double mag = fabs(val);
    uint8_t sign = (h >> 8) & 0x80;
    int lo = 0, hi = ARRAY_SIZE(f8_mag) - 1;

    if (hf_is_nan(h)) {
        return F8_NAN;
    }
    /* 256 would be next, and is even: overflow if at least halfway to it */
    if (isinf(val) || mag >= 248) {
        return sat ? sign | 0x7f : F8_NAN;
    }
    /* Find the closest pair of numbers around it */
    while (hi - lo > 1) {
        int mid = (lo + hi) / 2;

        if (f8_mag[mid] <= mag) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    if (mag - f8_mag[lo] > f8_mag[hi] - mag ||
        (mag - f8_mag[lo] == f8_mag[hi] - mag && (hi & 1) == 0)) {
        lo = hi;
    }
    /* There's no negative zero */
    return lo ? sign | lo : 0;
}

/*
 * The widening FP8 instructions put the even-numbered bytes of each word in
 * the first vector of the result pair and the odd-numbered ones in the
 * second, like vcvt2 does:
 *
 *    Vdd.v[0].hf[2*i]   = OP(Vu.f8[4*i], Vv.f8[4*i]);
 *    Vdd.v[0].hf[2*i+1] = OP(Vu.f8[4*i+2], Vv.f8[4*i+2]);
 *    Vdd.v[1].hf[2*i]   = OP(Vu.f8[4*i+1], Vv.f8[4*i+1]);
 *    Vdd.v[1].hf[2*i+1] = OP(Vu.f8[4*i+3], Vv.f8[4*i+3]);
 */
#define WIDEN_VEC(I) ((I) % 2)
#define WIDEN_IDX(I) (2 * ((I) / 4) + (((I) % 4) / 2))

#define DEF_TEST_F8_BINARY(NAME, INTRIN, REF) \
    static void test_##NAME(void) \
    { \
        HVX_VectorPair *hvx_output = (HVX_VectorPair *)&output[0]; \
        HVX_Vector a, b; \
        /* Every pair of FP8 values, 128 at a time */ \
        for (int blk = 0; blk < 512; blk++) { \
            for (int i = 0; i < MAX_VEC_SIZE_BYTES; i++) { \
                int pair = blk * MAX_VEC_SIZE_BYTES + i; \
                ((MMVector *)&a)->ub[i] = pair & 0xff; \
                ((MMVector *)&b)->ub[i] = pair >> 8; \
                expect[WIDEN_VEC(i)].hf[WIDEN_IDX(i)] = \
                    REF(pair & 0xff, pair >> 8); \
            } \
            *hvx_output = INTRIN(a, b); \
            check_output_hf(__LINE__, 2); \
        } \
    }

DEF_TEST_F8_BINARY(vadd_hf_f8, Q6_Whf_vadd_VV, ref_add)
DEF_TEST_F8_BINARY(vsub_hf_f8, Q6_Whf_vsub_VV, ref_sub)
DEF_TEST_F8_BINARY(vmpy_hf_f8, Q6_Whf_vmpy_VV, ref_mpy)

static uint16_t acc_values[] = {
    0x0000, 0x8000, 0x3c00, 0xbc00, 0x6800, 0x0001, 0x7bff, 0xfbff, 0x7c00,
    0xfc00, 0x7e00, 0x7d00, 0x1234, 0xa5c3, 0x5b80,
};

static void test_vmpy_hf_f8_acc(void)
{
    HVX_VectorPair *hvx_output = (HVX_VectorPair *)&output[0];
    HVX_VectorPair acc;
    HVX_Vector a, b;

    /* Every pair of FP8 values with each of the accumulator values */
    for (int n = 0; n < ARRAY_SIZE(acc_values); n++) {
        for (int blk = 0; blk < 512; blk++) {
            for (int i = 0; i < MAX_VEC_SIZE_BYTES; i++) {
                int pair = blk * MAX_VEC_SIZE_BYTES + i;
                /* Make the accumulator close to cancelling the product */
                uint16_t init = n == ARRAY_SIZE(acc_values) - 1 ?
                    ref_mpy(pair & 0xff, pair >> 8) ^ 0x8000 ^ (i & 3) :
                    acc_values[n];

                ((MMVector *)&a)->ub[i] = pair & 0xff;
                ((MMVector *)&b)->ub[i] = pair >> 8;
                ((MMVector *)&acc)->hf[WIDEN_VEC(i) * 64 + WIDEN_IDX(i)] = init;
                expect[WIDEN_VEC(i)].hf[WIDEN_IDX(i)] =
                    ref_mpy_acc(pair & 0xff, pair >> 8, init);
            }
            *hvx_output = Q6_Whf_vmpyacc_WhfVV(acc, a, b);
            check_output_hf(__LINE__, 2);
        }
    }
}

static void test_vcvt_hf_f8(void)
{
    HVX_VectorPair *hvx_output = (HVX_VectorPair *)&output[0];
    HVX_Vector a;

    for (int blk = 0; blk < 2; blk++) {
        for (int i = 0; i < MAX_VEC_SIZE_BYTES; i++) {
            int f8 = blk * MAX_VEC_SIZE_BYTES + i;

            ((MMVector *)&a)->ub[i] = f8;
            expect[WIDEN_VEC(i)].hf[WIDEN_IDX(i)] = ref_f8_to_hf(f8);
        }
        *hvx_output = Q6_Whf_vcvt_V(a);
        check_output_hf(__LINE__, 2);
    }
}

#define DEF_TEST_F8_UNARY(NAME, INTRIN, REF) \
    static void test_##NAME(void) \
    { \
        HVX_Vector *hvx_output = (HVX_Vector *)&output[0]; \
        HVX_Vector a; \
        for (int blk = 0; blk < 2; blk++) { \
            for (int i = 0; i < MAX_VEC_SIZE_BYTES; i++) { \
                int f8 = blk * MAX_VEC_SIZE_BYTES + i; \
                ((MMVector *)&a)->ub[i] = f8; \
                expect[0].ub[i] = REF(f8); \
            } \
            *hvx_output = INTRIN(a); \
            check_output_ub(__LINE__, 1); \
        } \
    }

static uint8_t ref_neg(uint8_t f8)
{
    return f8 == 0 || f8 == F8_NAN ? f8 : f8 ^ 0x80;
}

static uint8_t ref_abs(uint8_t f8)
{
    return f8 == F8_NAN ? f8 : f8 & 0x7f;
}

DEF_TEST_F8_UNARY(vfneg_f8, Q6_V_vfneg_V, ref_neg)
DEF_TEST_F8_UNARY(vabs_f8, Q6_V_vabs_V, ref_abs)

static uint8_t ref_min(uint8_t a, uint8_t b)
{
    if (a == F8_NAN || b == F8_NAN) {
        return F8_NAN;
    }
    return f8_to_double(a) < f8_to_double(b) ? a : b;
}

static uint8_t ref_max(uint8_t a, uint8_t b)
{
    if (a == F8_NAN || b == F8_NAN) {
        return F8_NAN;
    }
    return f8_to_double(a) > f8_to_double(b) ? a : b;
}

#define DEF_TEST_F8_MINMAX(NAME, INTRIN, REF) \
    static void test_##NAME(void) \
    { \
        HVX_Vector *hvx_output = (HVX_Vector *)&output[0]; \
        HVX_Vector a, b; \
        for (int blk = 0; blk < 512; blk++) { \
            for (int i = 0; i < MAX_VEC_SIZE_BYTES; i++) { \
                int pair = blk * MAX_VEC_SIZE_BYTES + i; \
                ((MMVector *)&a)->ub[i] = pair & 0xff; \
                ((MMVector *)&b)->ub[i] = pair >> 8; \
                expect[0].ub[i] = REF(pair & 0xff, pair >> 8); \
            } \
            *hvx_output = INTRIN(a, b); \
            check_output_ub(__LINE__, 1); \
        } \
    }

DEF_TEST_F8_MINMAX(vfmin_f8, Q6_V_vfmin_VV, ref_min)
DEF_TEST_F8_MINMAX(vfmax_f8, Q6_V_vfmax_VV, ref_max)

/*
 * USR.FPSAT picks what the narrowing conversion does on overflow, so it's set
 * before the source operands are loaded to keep the compiler from scheduling
 * the conversion ahead of it.
 */
static void set_fpsat(bool sat)
{
    uint32_t usr;

    asm volatile("%[usr] = usr" : [usr] "=r"(usr));
    usr = sat ? usr | (1 << 24) : usr & ~(1 << 24);
    asm volatile("usr = %[usr]" : : [usr] "r"(usr));
}

static void test_vcvt_f8_hf(bool sat)
{
    HVX_Vector *hvx_output = (HVX_Vector *)&output[0];
    HVX_Vector buffers[2];
    /* volatile keeps the loads of the sources after set_fpsat() */
    volatile HVX_Vector *vbuffers = buffers;

    /* Every half precision value, 64 at a time, in alternating sources */
    for (int blk = 0; blk < 1024; blk++) {
        for (int j = 0; j < MAX_VEC_SIZE_BYTES / 2; j++) {
            for (int s = 0; s < 2; s++) {
                /* A different value in each source, covering all of them */
                uint16_t h = (blk * 64 + j) * (s ? 40503 : 1) + (s ? 12345 : 0);

                ((MMVector *)&buffers[s])->hf[j] = h;
                expect[0].ub[4 * (j / 2) + 2 * (j % 2) + s] =
                    ref_hf_to_f8(h, sat);
            }
        }
        set_fpsat(sat);
        *hvx_output = Q6_V_vcvt_VhfVhf(vbuffers[0], vbuffers[1]);
        set_fpsat(false);
        check_output_ub(__LINE__, 1);
    }
}

int main(void)
{
    init_f8_mag();

    test_vcvt2_hf_ub();
    test_vcvt2_hf_b();
    test_vcvt2_ub_hf();
    test_vcvt2_b_hf();

    test_vcvt_hf_f8();
    test_vcvt_f8_hf(false);
    test_vcvt_f8_hf(true);
    test_vadd_hf_f8();
    test_vsub_hf_f8();
    test_vmpy_hf_f8();
    test_vmpy_hf_f8_acc();
    test_vfneg_f8();
    test_vabs_f8();
    test_vfmin_f8();
    test_vfmax_f8();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
