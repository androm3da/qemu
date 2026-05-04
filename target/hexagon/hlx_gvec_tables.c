/*
 * HLX GVecGen3 descriptor tables for inline TCG IR generation.
 *
 * Phase C of the HLX inlining plan: saturating add/sub and logical ops.
 *
 * Design: the canonical GVecGen3 descriptors for ssadd/sssub/usadd/ussub
 * live as static locals inside tcg_gen_gvec_ssadd() etc. in tcg-op-gvec.c.
 * Rather than exporting them from TCG core, we duplicate the small set we
 * need here (Option 1 from the plan).  The fniv pointers (tcg_gen_ssadd_vec,
 * etc.) are already public.  The fni4/fni8 helpers for usadd/ussub are
 * static in tcg-op-gvec.c, so we provide our own copies here.
 *
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "tcg/tcg.h"
#include "tcg/tcg-op-common.h"
#include "tcg/tcg-op-gvec-common.h"
#include "hlx_gvec_tables.h"

/* -----------------------------------------------------------------------
 * Local copies of static helpers from tcg-op-gvec.c
 * ----------------------------------------------------------------------- */

static void hlx_usadd_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 max = tcg_constant_i32(-1);
    tcg_gen_add_i32(d, a, b);
    tcg_gen_movcond_i32(TCG_COND_LTU, d, d, a, max, d);
}

static void hlx_usadd_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 max = tcg_constant_i64(-1);
    tcg_gen_add_i64(d, a, b);
    tcg_gen_movcond_i64(TCG_COND_LTU, d, d, a, max, d);
}

static void hlx_ussub_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 min = tcg_constant_i32(0);
    tcg_gen_sub_i32(d, a, b);
    tcg_gen_movcond_i32(TCG_COND_LTU, d, a, b, min, d);
}

static void hlx_ussub_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 min = tcg_constant_i64(0);
    tcg_gen_sub_i64(d, a, b);
    tcg_gen_movcond_i64(TCG_COND_LTU, d, a, b, min, d);
}

/* -----------------------------------------------------------------------
 * Saturating add descriptors (ssadd — signed)
 * ----------------------------------------------------------------------- */

static const TCGOpcode hlx_vecop_ssadd[] = { INDEX_op_ssadd_vec, 0 };

const GVecGen3 hlx_ssadd_op[4] = {
    { .fniv    = tcg_gen_ssadd_vec,
      .fno     = gen_helper_gvec_ssadd8,
      .opt_opc = hlx_vecop_ssadd,
      .vece    = MO_8 },
    { .fniv    = tcg_gen_ssadd_vec,
      .fno     = gen_helper_gvec_ssadd16,
      .opt_opc = hlx_vecop_ssadd,
      .vece    = MO_16 },
    { .fniv    = tcg_gen_ssadd_vec,
      .fno     = gen_helper_gvec_ssadd32,
      .opt_opc = hlx_vecop_ssadd,
      .vece    = MO_32 },
    { .fniv    = tcg_gen_ssadd_vec,
      .fno     = gen_helper_gvec_ssadd64,
      .opt_opc = hlx_vecop_ssadd,
      .vece    = MO_64 },
};

/* -----------------------------------------------------------------------
 * Saturating sub descriptors (sssub — signed)
 * ----------------------------------------------------------------------- */

static const TCGOpcode hlx_vecop_sssub[] = { INDEX_op_sssub_vec, 0 };

const GVecGen3 hlx_sssub_op[4] = {
    { .fniv    = tcg_gen_sssub_vec,
      .fno     = gen_helper_gvec_sssub8,
      .opt_opc = hlx_vecop_sssub,
      .vece    = MO_8 },
    { .fniv    = tcg_gen_sssub_vec,
      .fno     = gen_helper_gvec_sssub16,
      .opt_opc = hlx_vecop_sssub,
      .vece    = MO_16 },
    { .fniv    = tcg_gen_sssub_vec,
      .fno     = gen_helper_gvec_sssub32,
      .opt_opc = hlx_vecop_sssub,
      .vece    = MO_32 },
    { .fniv    = tcg_gen_sssub_vec,
      .fno     = gen_helper_gvec_sssub64,
      .opt_opc = hlx_vecop_sssub,
      .vece    = MO_64 },
};

/* -----------------------------------------------------------------------
 * Saturating add descriptors (usadd — unsigned)
 * ----------------------------------------------------------------------- */

static const TCGOpcode hlx_vecop_usadd[] = { INDEX_op_usadd_vec, 0 };

const GVecGen3 hlx_usadd_op[4] = {
    { .fniv    = tcg_gen_usadd_vec,
      .fno     = gen_helper_gvec_usadd8,
      .opt_opc = hlx_vecop_usadd,
      .vece    = MO_8 },
    { .fniv    = tcg_gen_usadd_vec,
      .fno     = gen_helper_gvec_usadd16,
      .opt_opc = hlx_vecop_usadd,
      .vece    = MO_16 },
    { .fni4    = hlx_usadd_i32,
      .fniv    = tcg_gen_usadd_vec,
      .fno     = gen_helper_gvec_usadd32,
      .opt_opc = hlx_vecop_usadd,
      .vece    = MO_32 },
    { .fni8    = hlx_usadd_i64,
      .fniv    = tcg_gen_usadd_vec,
      .fno     = gen_helper_gvec_usadd64,
      .opt_opc = hlx_vecop_usadd,
      .vece    = MO_64 },
};

/* -----------------------------------------------------------------------
 * Saturating sub descriptors (ussub — unsigned)
 * ----------------------------------------------------------------------- */

static const TCGOpcode hlx_vecop_ussub[] = { INDEX_op_ussub_vec, 0 };

const GVecGen3 hlx_ussub_op[4] = {
    { .fniv    = tcg_gen_ussub_vec,
      .fno     = gen_helper_gvec_ussub8,
      .opt_opc = hlx_vecop_ussub,
      .vece    = MO_8 },
    { .fniv    = tcg_gen_ussub_vec,
      .fno     = gen_helper_gvec_ussub16,
      .opt_opc = hlx_vecop_ussub,
      .vece    = MO_16 },
    { .fni4    = hlx_ussub_i32,
      .fniv    = tcg_gen_ussub_vec,
      .fno     = gen_helper_gvec_ussub32,
      .opt_opc = hlx_vecop_ussub,
      .vece    = MO_32 },
    { .fni8    = hlx_ussub_i64,
      .fniv    = tcg_gen_ussub_vec,
      .fno     = gen_helper_gvec_ussub64,
      .opt_opc = hlx_vecop_ussub,
      .vece    = MO_64 },
};

/* -----------------------------------------------------------------------
 * Logical op descriptors (vece-independent, prefer 64-bit scalar)
 * ----------------------------------------------------------------------- */

const GVecGen3 hlx_and_op = {
    .fni8      = tcg_gen_and_i64,
    .fniv      = tcg_gen_and_vec,
    .fno       = gen_helper_gvec_and,
    .prefer_i64 = true,
};

const GVecGen3 hlx_or_op = {
    .fni8      = tcg_gen_or_i64,
    .fniv      = tcg_gen_or_vec,
    .fno       = gen_helper_gvec_or,
    .prefer_i64 = true,
};

const GVecGen3 hlx_xor_op = {
    .fni8      = tcg_gen_xor_i64,
    .fniv      = tcg_gen_xor_vec,
    .fno       = gen_helper_gvec_xor,
    .prefer_i64 = true,
};

/* andc(a, b) = a & ~b — maps to LX_andn semantics */
const GVecGen3 hlx_andc_op = {
    .fni8      = tcg_gen_andc_i64,
    .fniv      = tcg_gen_andc_vec,
    .fno       = gen_helper_gvec_andc,
    .prefer_i64 = true,
};

/* orc(a, b) = a | ~b — maps to LX_orn semantics */
const GVecGen3 hlx_orc_op = {
    .fni8      = tcg_gen_orc_i64,
    .fniv      = tcg_gen_orc_vec,
    .fno       = gen_helper_gvec_orc,
    .prefer_i64 = true,
};
