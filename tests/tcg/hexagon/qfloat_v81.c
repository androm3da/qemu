/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <string.h>

#include <hexagon_types.h>
#include <hvx_hexagon_protos.h>

static int err;

#include "hvx_misc.h"

static void expect_h(int output_index, uint16_t value)
{
    for (int i = 0; i < MAX_VEC_SIZE_BYTES / 2; i++) {
        expect[output_index].uh[i] = value;
    }
}

static void expect_w(int output_index, uint32_t value)
{
    for (int i = 0; i < MAX_VEC_SIZE_BYTES / 4; i++) {
        expect[output_index].uw[i] = value;
    }
}

static void test_qf16(void)
{
    asm volatile(
        "r0 = #0x3c003c00\n"
        "v1 = vsplat(r0)\n"
        "r0 = #0x40004000\n"
        "v2 = vsplat(r0)\n"
        "v0.qf16 = vadd(v1.hf, v2.hf)\n"
        "v3.hf = v0.qf16\n"
        "r0 = #0x10001000\n"
        "v2 = vsplat(r0)\n"
        "v0.qf16 = vadd(v1.hf, v2.hf)\n"
        "v5.hf = v0.qf16\n"
        "r0 = #0xc000c000\n"
        "v1 = vsplat(r0)\n"
        "v0.qf16 = vabs(v1.hf)\n"
        "v6.hf = v0.qf16\n"
        "r0 = #0x40004000\n"
        "v1 = vsplat(r0)\n"
        "v0.qf16 = vneg(v1.hf)\n"
        "v7.hf = v0.qf16\n"
        "vmemu(%[out0]) = v3\n"
        "vmemu(%[out1]) = v5\n"
        "vmemu(%[out2]) = v6\n"
        "vmemu(%[out3]) = v7\n"
        :
        : [out0] "r"(&output[0]), [out1] "r"(&output[1]),
          [out2] "r"(&output[2]), [out3] "r"(&output[3])
        : "r0", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
          "memory");

    expect_h(0, 0x4200);
    expect_h(1, 0x3c00);
    expect_h(2, 0x4000);
    expect_h(3, 0xc000);
    check_output_h(__LINE__, 4);
}

static void test_qf32(void)
{
    asm volatile(
        "r0 = #0x3f800000\n"
        "v1 = vsplat(r0)\n"
        "r0 = #0x40000000\n"
        "v2 = vsplat(r0)\n"
        "v0.qf32 = vadd(v1.sf, v2.sf)\n"
        "v3.sf = v0.qf32\n"
        "v4.w = vilog2(v0.qf32)\n"
        "r0 = #0x7f800000\n"
        "v1 = vsplat(r0)\n"
        "r0 = #0xff800000\n"
        "v2 = vsplat(r0)\n"
        "v0.qf32 = vadd(v1.sf, v2.sf)\n"
        "v5.sf = v0.qf32\n"
        "vmemu(%[out0]) = v3\n"
        "vmemu(%[out1]) = v4\n"
        "vmemu(%[out2]) = v5\n"
        :
        : [out0] "r"(&output[0]), [out1] "r"(&output[1]),
          [out2] "r"(&output[2])
        : "r0", "v0", "v1", "v2", "v3", "v4", "v5", "memory");

    expect_w(0, 0x40400000);
    expect_w(1, 1);
    expect_w(2, 0x7fffffff);
    check_output_w(__LINE__, 3);
}

static void test_qf32_bf_conversion(void)
{
    HVX_Vector one = Q6_V_vsplat_R(0x3c003c00);
    HVX_VectorPair qf = Q6_Wqf32_vmpy_VhfVhf(one, one);
    HVX_Vector result = Q6_Vbf_equals_Wqf32(qf);

    memcpy(output, &result, sizeof(result));
    for (int i = 0; i < MAX_VEC_SIZE_BYTES / 2; i++) {
        expect[0].uh[i] = 0x3f80;
    }
    check_output_h(__LINE__, 1);
}

enum { QOP_ADD, QOP_SUB, QOP_MPY };

static HVX_Vector qf16_op(int op, HVX_Vector a, HVX_Vector b)
{
    switch (op) {
    case QOP_ADD:
        return Q6_Vhf_equals_Vqf16(Q6_Vqf16_vadd_VhfVhf(a, b));
    case QOP_SUB:
        return Q6_Vhf_equals_Vqf16(Q6_Vqf16_vsub_VhfVhf(a, b));
    default:
        return Q6_Vhf_equals_Vqf16(Q6_Vqf16_vmpy_VhfVhf(a, b));
    }
}

static HVX_Vector qf32_op(int op, HVX_Vector a, HVX_Vector b)
{
    switch (op) {
    case QOP_ADD:
        return Q6_Vsf_equals_Vqf32(Q6_Vqf32_vadd_VsfVsf(a, b));
    case QOP_SUB:
        return Q6_Vsf_equals_Vqf32(Q6_Vqf32_vsub_VsfVsf(a, b));
    default:
        return Q6_Vsf_equals_Vqf32(Q6_Vqf32_vmpy_VsfVsf(a, b));
    }
}

/*
 * Zero results and negative results, the former with the sign that a
 * negative operand gives them, and the latter with the fraction bits that
 * a negative number carries.  Reference values are from hexagon-sim.
 */
static void test_qf_zero_negative(void)
{
    static const struct {
        int op;
        uint16_t a, b, result;
    } hf_cases[] = {
        { QOP_ADD, 0x3c00, 0xbc00, 0x0000 },
        { QOP_ADD, 0x0000, 0x0000, 0x0000 },
        { QOP_ADD, 0x8000, 0x8000, 0x8000 },
        { QOP_SUB, 0x3c00, 0x3c00, 0x0000 },
        { QOP_SUB, 0x8000, 0x0000, 0x8000 },
        { QOP_MPY, 0x0000, 0x3c00, 0x0000 },
        { QOP_MPY, 0x8000, 0x3c00, 0x8000 },
        { QOP_MPY, 0x0000, 0xbc00, 0x8000 },
        { QOP_MPY, 0x8000, 0xbc00, 0x0000 },
        { QOP_MPY, 0xbc00, 0x3c00, 0xbc00 },
        { QOP_MPY, 0xbd00, 0x3c00, 0xbd00 },
        { QOP_MPY, 0xbc01, 0x3c03, 0xbc04 },
        { QOP_ADD, 0xbc01, 0xbc03, 0xc002 },
    };
    static const struct {
        int op;
        uint32_t a, b, result;
    } sf_cases[] = {
        { QOP_ADD, 0x3f800000, 0xbf800000, 0x00000000 },
        { QOP_ADD, 0x80000000, 0x80000000, 0x80000000 },
        { QOP_MPY, 0x80000000, 0x3f800000, 0x80000000 },
        { QOP_MPY, 0xbf800000, 0x3f800000, 0xbf800000 },
    };

    for (int i = 0; i < ARRAY_SIZE(hf_cases); i++) {
        HVX_Vector result = qf16_op(hf_cases[i].op,
                                    Q6_Vh_vsplat_R(hf_cases[i].a),
                                    Q6_Vh_vsplat_R(hf_cases[i].b));

        memcpy(output, &result, sizeof(result));
        expect_h(0, hf_cases[i].result);
        check_output_h(__LINE__, 1);
    }
    for (int i = 0; i < ARRAY_SIZE(sf_cases); i++) {
        HVX_Vector result = qf32_op(sf_cases[i].op,
                                    Q6_V_vsplat_R(sf_cases[i].a),
                                    Q6_V_vsplat_R(sf_cases[i].b));

        memcpy(output, &result, sizeof(result));
        expect_w(0, sf_cases[i].result);
        check_output_w(__LINE__, 1);
    }
}

/*
 * The significand and extension bits of negative results.  The qf values
 * are checked as they're stored, which is how they were found to differ from
 * hexagon-sim, and not only after converting back to an IEEE format.
 */
static void test_qf_encoding(void)
{
    static const struct {
        int op;
        uint16_t a, b, qf;
    } hf_cases[] = {
        { QOP_ADD, 0xbc00, 0x0000, 0x800e },
        { QOP_ADD, 0x8400, 0x0000, 0xc001 },
        { QOP_ADD, 0xc000, 0x159a, 0x800f },
        { QOP_ADD, 0x0001, 0x0000, 0x0001 },
        { QOP_ADD, 0x8001, 0x0000, 0xffe1 },
        { QOP_ADD, 0xbc01, 0x3c03, 0x002f },
        { QOP_ADD, 0x3c01, 0xbc03, 0xffef },
        { QOP_ADD, 0xbc01, 0xbc03, 0xbfd0 },
        { QOP_ADD, 0x3c00, 0xbc00, 0x000f },
        { QOP_ADD, 0x8000, 0x8000, 0xffe1 },
        { QOP_SUB, 0xbc00, 0xbc00, 0x000f },
        { QOP_SUB, 0x8000, 0x0000, 0xffe1 },
        { QOP_MPY, 0xbc00, 0x3c00, 0xbfef },
        { QOP_MPY, 0xbc01, 0x3c03, 0xbfaf },
        { QOP_MPY, 0x0001, 0xdc00, 0xffe9 },
        /* 2^16 is too large, and so is -2^16 */
        { QOP_MPY, 0x5c00, 0x5c00, 0x7fff },
        { QOP_MPY, 0xdc00, 0x5c00, 0x801f },
    };
    static const struct {
        int op;
        uint32_t a, b, qf;
    } sf_cases[] = {
        { QOP_ADD, 0xbf800000, 0x00000000, 0x8000007e },
        { QOP_ADD, 0x80000001, 0x00000000, 0xffffff01 },
        { QOP_ADD, 0xbf800000, 0x3f800001, 0x0000007f },
        { QOP_SUB, 0xbf800000, 0xbf800000, 0x0000007f },
        { QOP_MPY, 0xbf800000, 0x3f800000, 0xbfffff7f },
        { QOP_MPY, 0xbf800001, 0x3f800003, 0xbffffd7f },
    };

    for (int i = 0; i < ARRAY_SIZE(hf_cases); i++) {
        HVX_Vector a = Q6_Vh_vsplat_R(hf_cases[i].a);
        HVX_Vector b = Q6_Vh_vsplat_R(hf_cases[i].b);
        HVX_Vector qf;

        switch (hf_cases[i].op) {
        case QOP_ADD:
            qf = Q6_Vqf16_vadd_VhfVhf(a, b);
            break;
        case QOP_SUB:
            qf = Q6_Vqf16_vsub_VhfVhf(a, b);
            break;
        default:
            qf = Q6_Vqf16_vmpy_VhfVhf(a, b);
            break;
        }
        memcpy(output, &qf, sizeof(qf));
        expect_h(0, hf_cases[i].qf);
        check_output_h(__LINE__, 1);
    }
    for (int i = 0; i < ARRAY_SIZE(sf_cases); i++) {
        HVX_Vector a = Q6_V_vsplat_R(sf_cases[i].a);
        HVX_Vector b = Q6_V_vsplat_R(sf_cases[i].b);
        HVX_Vector qf;

        switch (sf_cases[i].op) {
        case QOP_ADD:
            qf = Q6_Vqf32_vadd_VsfVsf(a, b);
            break;
        case QOP_SUB:
            qf = Q6_Vqf32_vsub_VsfVsf(a, b);
            break;
        default:
            qf = Q6_Vqf32_vmpy_VsfVsf(a, b);
            break;
        }
        memcpy(output, &qf, sizeof(qf));
        expect_w(0, sf_cases[i].qf);
        check_output_w(__LINE__, 1);
    }
}

int main()
{
    test_qf16();
    test_qf32();
    test_qf32_bf_conversion();
    test_qf_zero_negative();
    test_qf_encoding();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
