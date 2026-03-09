/*
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * HLX memory operation tests (predicated load/store)
 *
 * Tests: LX_L16b_p_tmp, LX_S16b_p_new
 *
 * Predicated load/store have no intrinsic equivalents, so the test
 * operations use inline assembly.  Buffers are BSS-allocated (not VTCM)
 * because mixing HLX intrinsics with HLX inline asm in the same
 * compilation unit causes the compiler to generate invalid packets.
 *
 * NOTE: This test only runs on QEMU.  On hexagon-sim the HLX mem()
 * instructions require VTCM addresses, but VTCM initialisation via
 * HLX intrinsics conflicts with the inline asm HLX instructions.
 */

#include "hlx_test.h"

/* Single-register buffers */
static uint16_t buf_a[HLX_HALF_ELEMS] HLX_ALIGN;
static uint16_t buf_b[HLX_HALF_ELEMS] HLX_ALIGN;
static uint16_t buf_out[HLX_HALF_ELEMS] HLX_ALIGN;
static uint16_t expected[HLX_HALF_ELEMS];

static void init_data(void)
{
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        buf_a[i] = (i * 37 + 100) & 0xFFFF;
        buf_b[i] = (i * 23 + 200) & 0xFFFF;
        buf_out[i] = 0xDEAD;
    }
}

/* ===== Predicated load ===== */

static void test_pred_load_true(void)
{
    init_data();
    /* if (Pv) Ld.tmp = mem(Rs) -- with true predicate */
    asm volatile(
        "p0 = cmp.eq(r0, r0)\n"     /* p0 = true */
        "{ if (p0) L0.tmp = mem(%0) }\n"
        "{ L1.h = L0.h\n"
        "  mem(%1) = L1.new }\n"
        : : "r"(buf_a), "r"(buf_out) : "memory"
    );
    check_h("LX_L16b_p_tmp (true)", buf_out, buf_a, HLX_HALF_ELEMS);
}

static void test_pred_load_false(void)
{
    init_data();
    /* if (Pv) Ld.tmp = mem(Rs) -- with false predicate
     * When predicate is false, the load is cancelled.
     * The output should reflect whatever was in L0 before (not buf_a).
     * We pre-load L0 with buf_b, then do a false-predicated load of buf_a.
     */
    asm volatile(
        "{ L0.tmp = mem(%0) }\n"     /* Load buf_b into L0 */
        "p0 = !cmp.eq(r0, r0)\n"    /* p0 = false (always) */
        "{ if (p0) L0.tmp = mem(%1) }\n"  /* Should NOT load buf_a */
        "{ L1.h = L0.h\n"
        "  mem(%2) = L1.new }\n"
        : : "r"(buf_b), "r"(buf_a), "r"(buf_out) : "memory"
    );
    /* L0 should still contain buf_b */
    check_h("LX_L16b_p_tmp (false)", buf_out, buf_b, HLX_HALF_ELEMS);
}

/* ===== Predicated store ===== */

static void test_pred_store_true(void)
{
    init_data();
    /* if (Pv) mem(Rs) = Ku.new -- with true predicate */
    asm volatile(
        "{ L0.tmp = mem(%0) }\n"
        "p0 = cmp.eq(r0, r0)\n"     /* p0 = true */
        "{ L1.h = L0.h\n"
        "  if (p0) mem(%1) = L1.new }\n"
        : : "r"(buf_a), "r"(buf_out) : "memory"
    );
    check_h("LX_S16b_p_new (true)", buf_out, buf_a, HLX_HALF_ELEMS);
}

static void test_pred_store_false(void)
{
    init_data();
    /* if (Pv) mem(Rs) = Ku.new -- with false predicate
     * Store should be cancelled; buf_out retains 0xDEAD
     */
    asm volatile(
        "{ L0.tmp = mem(%0) }\n"
        "p0 = !cmp.eq(r0, r0)\n"    /* p0 = false (always) */
        "{ L1.h = L0.h\n"
        "  if (p0) mem(%1) = L1.new }\n"
        : : "r"(buf_a), "r"(buf_out) : "memory"
    );
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = 0xDEAD;
    }
    check_h("LX_S16b_p_new (false)", buf_out, expected, HLX_HALF_ELEMS);
}

int main(void)
{
    enable_hlx();

    /* Predicated load */
    test_pred_load_true();
    test_pred_load_false();

    /* Predicated store */
    test_pred_store_true();
    test_pred_store_false();

    puts(err ? "FAIL" : "PASS");
    return err;
}
