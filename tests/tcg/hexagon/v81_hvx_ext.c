/*
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * Tests for the v81 HVX qfloat extended-precision bits: vsetqfext/
 * vgetqfext, vmerge's ext-bit passthrough, and the "any non-qfloat write
 * resets a vector's ext bits to zero" rule -- including through .cur/.tmp
 * loads and predicated/packet-internal forwarding, where it's easy to get
 * the reset timing wrong.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int err;

#include "hvx_misc.h"

static void test_qf32_mpy(void)
{
    memset(buffer0, 0xff, sizeof(buffer0));
    asm volatile(
        "v0 = vmem(%0 + #0)\n\t"
        "v1.qf32 = vmpy(v0.qf32, v0.qf32)\n\t"
        "vmem(%1 + #0) = v1\n\t"
        :
        : "r"(buffer0), "r"(output)
        : "v0", "v1", "memory");

    for (int i = 0; i < MAX_VEC_SIZE_BYTES / 4; i++) {
        /* reference from the sim, v81 semantics */
        expect[0].uw[i] = 0x0000007f;
    }

    check_output_w(__LINE__, 1);
}

static void test_ext_bits_reset_on_copy(void)
{
    asm volatile(
        "r0 = #0x11111111\n"
        "v10 = vsplat(r0)\n"

        /* tweak ext bits from vassign's src and dest */
        "v0.x = vsetqfext(v10, r0)\n"
        "v1.x = vsetqfext(v10, r0)\n"

        "v2 = vgetqfext(v0.x, r0)\n"
        "v3 = vgetqfext(v1.x, r0)\n"
        "v1 = v0\n" /* copy should reset ext bits from dest (only) */
        "v4 = vgetqfext(v0.x, r0)\n"
        "v5 = vgetqfext(v1.x, r0)\n"

        "vmemu(%0) = v2\n"
        "vmemu(%1) = v3\n"
        "vmemu(%2) = v4\n"
        "vmemu(%3) = v5\n"
        :
        : "r"(&output[0]), "r"(&output[1]), "r"(&output[2]), "r"(&output[3])
        : "r0", "v0", "v1", "v2", "v3", "v4", "v5", "v10", "memory");

    for (int i = 0; i < MAX_VEC_SIZE_BYTES / 4; i++) {
        expect[0].uw[i] = expect[1].uw[i] = expect[2].uw[i] = 0x11111111;
        expect[3].uw[i] = 0x11001100;
    }

    check_output_w(__LINE__, 4);
}

static void test_ext_bits_reset_multiple_insns(void)
{
    asm volatile(
        "r1 = #0xe60e31d4\n"
        "r2 = #0xbaf1fa15\n"
        "r3 = #0x4a777c7b\n"
        "v1 = vsplat(r1)\n"
        "v2 = vsplat(r2)\n"
        "v3 = vsplat(r3)\n"

        "{\n"
        "    v2 = v1\n"
        "    v4.sf = vmax(v3.sf,v2.sf)\n"
        "}\n"
        "v5.hf = v4.qf16\n"
        "vmemu(%0) = v5\n"
        :
        : "r"(output)
        : "r1", "r2", "r3", "v1", "v2", "v3", "v4", "v5", "memory");

    for (int i = 0; i < MAX_VEC_SIZE_BYTES / 4; i++) {
        expect[0].uw[i] = 0x5ca76fc7; /* reference from the sim */
    }

    check_output_w(__LINE__, 1);
}

static void test_ext_bits_reset_interleaved_qf(void)
{
    asm volatile(
        "r0 = #0x00000057\n"
        "v1 = vsplat(r0)\n"
        "v2 = vsplat(r0)\n"

        "r1 = #0x2a8b9bf4\n"
        "v3 = vsplat(r1)\n"
        "v4 = vsplat(r1)\n"

        "v5.qf32 = vadd(v2.sf,v4.sf)\n"
        "v5.hf = vmin(v1.hf,v3.hf)\n"
        "v6.qf16 = vmpy(v5.qf16,r0.hf)\n"
        "vmemu(%0) = v6\n"
        :
        : "r"(output)
        : "r0", "r1", "r3", "v1", "v2", "v3", "v4", "v5", "v6", "memory");

    for (int i = 0; i < MAX_VEC_SIZE_BYTES / 4; i++) {
        expect[0].uw[i] = 0x0000f766; /* reference from the sim */
    }

    check_output_w(__LINE__, 1);
}

static void test_qfloat_semantics(void)
{
    asm volatile(
        "r0 = #0xffffff80\n"
        "v0 = vsplat(r0)\n"
        "r1 = #0xfde1d91d\n"
        "v1 = vsplat(r1)\n"

        "v2.qf32=vsub(v1.qf32, v0.qf32)\n"
        "vmemu(%0) = v2\n"
        :
        : "r"(output)
        : "r0", "r1", "v0", "v1", "v2", "memory");

    for (int i = 0; i < MAX_VEC_SIZE_BYTES / 4; i++) {
        /* reference from the sim, v81 semantics */
        expect[0].uw[i] = 0x7fffff68;
    }

    check_output_w(__LINE__, 1);
}

static void test_qfloat_with_cur(void)
{
    memset(buffer0, 0xff, sizeof(buffer0));
    asm volatile(
        "r0 = #0x11111111\n"
        "v10 = vsplat(r0)\n"

        "r1 = #0xffffff80\n"
        "v1 = vsplat(r1)\n"

        /* tweak ext bits */
        "v0.x = vsetqfext(v10, r0)\n"
        "v2 = vmerge(v0.x, v1.w)\n"

        "{\n"
        "    v2.cur = vmem(%0++#0)\n"
        "    v3.qf32=vsub(v2.qf32, v1.qf32)\n"
        "}\n"
        "vmemu(%1) = v3\n"
        :
        : "r"(buffer0), "r"(output)
        : "r0", "r1", "v0", "v1", "v2", "v3", "v10", "memory");

    for (int i = 0; i < MAX_VEC_SIZE_BYTES / 4; i++) {
        expect[0].uw[i] = 0x800000e7; /* reference from the sim */
    }

    check_output_w(__LINE__, 1);
}

static void test_qfloat_with_tmp(void)
{
    memset(buffer0, 0xff, sizeof(buffer0));
    asm volatile(
        "r0 = #0x11111111\n"
        "v10 = vsplat(r0)\n"

        "r1 = #0xffffff80\n"
        "v1 = vsplat(r1)\n"

        /* tweak ext bits */
        "v0.x = vsetqfext(v10, r0)\n"
        "v2 = vmerge(v0.x, v1.w)\n"

        "{\n"
        "    v3.tmp = vmem(%0++#0)\n"
        "    v3.qf32=vsub(v2.qf32, v1.qf32)\n"
        "    v4 = v3\n"
        "}\n"
        "vmemu(%1) = v3\n"

        "v10 = vgetqfext(v3.x, r0)\n"
        "vmemu(%2) = v10\n"
        :
        : "r"(buffer0), "r"(&output[0]), "r"(&output[1])
        : "r0", "r1", "v0", "v1", "v2", "v3", "v4", "v10", "memory");

    for (int i = 0; i < MAX_VEC_SIZE_BYTES / 4; i++) {
        expect[0].uw[i] = 0x00000080; /* reference from the sim */
        expect[1].uw[i] = 0x00111111; /* tmp should not reset v3's ext bits */
    }

    check_output_w(__LINE__, 2);
}

static void test_vconv_bf(void)
{
    asm volatile(
        "r0 = #0x12345678\n"
        "v0 = vsplat(r0)\n"
        "r0 = #0xff00ff00\n"
        "v1 = vsplat(r0)\n"
        "v2.bf = v1:0.qf32\n"
        "vmem(%0) = v2\n"
        :
        : "r"(&output[0])
        : "r0", "v0", "v1", "v2", "memory"
    );
    for (int i = 0; i < MAX_VEC_SIZE_BYTES / 4; i++) {
        expect[0].w[i] = 0x80013b12; /* reference from the sim */
    }
    check_output_w(__LINE__, 1);
}

int main()
{
    test_qf32_mpy();
    test_ext_bits_reset_on_copy();
    test_ext_bits_reset_interleaved_qf();
    test_qfloat_with_cur();
    test_qfloat_with_tmp();
    test_ext_bits_reset_multiple_insns();
    test_qfloat_semantics();
    test_vconv_bf();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
