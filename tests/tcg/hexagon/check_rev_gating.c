/*
 * Test that instructions from a newer revision than the running CPU
 * are rejected with SIGILL.
 *
 * Compiled with -mv66 so that e_flags selects CPU v66. The test embeds
 * instructions from v68 up through v85 via .word encoding: the
 * assembler enforces the selected CPU's own minimum version, so none
 * of these -- including ones it otherwise knows how to assemble at
 * their own target, such as callrh or unpause -- can be written as
 * themselves in a file built for v66. The v79 non-temporal hints
 * (":nt") aren't known to the assembler at any target yet, so those
 * would need .word regardless. The revision-gated decoder must reject
 * representative instructions from each family, and linux-user must
 * deliver SIGILL.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "rev_gating.h"

TRY_FUNC(v68_loadw_aq,
         ".word 0x9200c800    /* { r0 = memw_aq(r0) } */\n")
TRY_FUNC(v68_loadd_aq,
         ".word 0x9201d800    /* r1:0 = memd_aq(r1) */\n")
TRY_FUNC(v68_release_at,
         ".word 0xa0e0c00c    /* release(r0):at */\n")
TRY_FUNC(v68_release_st,
         ".word 0xa0e0c02c    /* release(r0):st */\n")
TRY_FUNC(v68_storew_rl_at,
         ".word 0xa0a0c108    /* memw_rl(r0):at = r1 */\n")
TRY_FUNC(v68_stored_rl_at,
         ".word 0xa0e2c008    /* memd_rl(r2):at = r1:0 */\n")
TRY_FUNC(v68_storew_rl_st,
         ".word 0xa0a0c128    /* memw_rl(r0):st = r1 */\n")
TRY_FUNC(v68_stored_rl_st,
         ".word 0xa0e2c028    /* memd_rl(r2):st = r1:0 */\n")

TRY_FUNC(v68hvx_v6mpy,
         ".word 0x1f42e424    /* v5:4.w = v6mpy(v5:4.ub, v3:2.b, #1):v */\n")

TRY_FUNC(v68hvx_vabs_hf,
         ".word 0x1e06e180    /* v0.hf = vabs(v1.hf) */\n")
TRY_FUNC(v68hvx_vadd_hf_hf,
         ".word 0x1fa2e1e0    /* v0.hf = vadd(v1.hf, v2.hf) */\n")
TRY_FUNC(v68hvx_vfmax_sf,
         ".word 0x1c62e160    /* v0.sf = vfmax(v1.sf, v2.sf) */\n")
TRY_FUNC(v68hvx_vmpy_sf_hf,
         ".word 0x1f83e240    /* v1:0.sf = vmpy(v2.hf, v3.hf) */\n")

TRY_FUNC(v69hvx_vasrvuhubrndsat,
         ".word 0x1d06c465    /* v5.ub = vasr(v5:4.uh, v6.ub):rnd:sat */\n")
TRY_FUNC(v69hvx_vasrvuhubsat,
         ".word 0x1d06c445    /* v5.ub = vasr(v5:4.uh, v6.ub):sat */\n")
TRY_FUNC(v69hvx_vasrvwuhrndsat,
         ".word 0x1d06c425    /* v5.uh = vasr(v5:4.w, v6.uh):rnd:sat */\n")
TRY_FUNC(v69hvx_vasrvwuhsat,
         ".word 0x1d06c405    /* v5.uh = vasr(v5:4.w, v6.uh):sat */\n")
TRY_FUNC(v69hvx_vassign_tmp,
         ".word 0x1e014dcc    /* { v12.tmp = v13 */\n"
         ".word 0x1c43cc04    /*    v4.w = vadd(v12.w, v3.w) } */\n")
TRY_FUNC(v69hvx_vcombine_tmp,
         ".word 0x1eae4fec    /* { v13:12.tmp = vcombine(v15, v14) */\n"
         ".word 0x1c434c04    /*   v4.w = vadd(v12.w, v3.w) */\n"
         ".word 0x1e03edf0    /*   v16 = v13 } */\n")
TRY_FUNC(v69hvx_vmpyuhvs,
         ".word 0x1fc5e4e4    /* v4.uh = vmpy(V4.uh, v5.uh):>>16 */\n")

TRY_FUNC(v73_callrh,
         ".word 0x50c5c000    /* callrh r5 */\n")
TRY_FUNC(v73_jumprh,
         ".word 0x52c0c000    /* jumprh r0 */\n")
TRY_FUNC(v73_unpause,
         ".word 0x57e0d000    /* unpause */\n")

TRY_FUNC(v73hvx_vmax_bf,
         ".word 0x1d42e1e0    /* v0.bf = vmax(v1.bf, v2.bf) */\n")
TRY_FUNC(v73hvx_vadd_sf_bf,
         ".word 0x1d43e2c0    /* v1:0.sf = vadd(v2.bf, v3.bf) */\n")

TRY_FUNC(v79_storeri_pi_nt,
         ".word 0xaa82c308    /* memw(r2++#4):nt = r3 */\n")
TRY_FUNC(v79_pstorerit_pi_nt,
         ".word 0xaa82e308    /* if (p0) memw(r2++#4):nt = r3 */\n")
TRY_FUNC(v79_pstorerif_pi_nt,
         ".word 0xaa82e30c    /* if (!p0) memw(r2++#4):nt = r3 */\n")
TRY_FUNC(v79_dczeroa_nt,
         ".word 0xa0c2e000    /* dczeroa(r2):nt */\n")
TRY_FUNC(v79_dcfetchbo_nt,
         ".word 0x9402e000    /* dcfetch(r2+#0):nt */\n")

TRY_FUNC(v83_callr_rb,
         ".word 0x5020c000    /* callr_rb r0 */\n")
TRY_FUNC(v83_nop_rb,
         ".word 0x7f80c000    /* nop_rb */\n")
TRY_FUNC(v83_vsfadd,
         ".word 0xeb02c440    /* r1:0 = vsfadd(r3:2, r5:4) */\n")

TRY_FUNC(v79hvx_vmerge_qf,
         ".word 0x1f01e0e2    /* v2 = vmerge(v0.x, v1.w) */\n")
TRY_FUNC(v79hvx_vcvt2_hf_ub,
         ".word 0x1ed5e0e2    /* v3:2.hf = vcvt2(v0.ub) */\n")
TRY_FUNC(v79hvx_vcvt2_b_hf,
         ".word 0x1ac1e0c2    /* v2.b = vcvt2(v0.hf, v1.hf) */\n")
TRY_FUNC(v79hvx_vcvt_f8_hf,
         ".word 0x1fe1e040    /* v0.f8 = vcvt(v0.hf, v1.hf) */\n")
TRY_FUNC(v79hvx_vcvt_hf_f8,
         ".word 0x1e05e0a2    /* v3:2.hf = vcvt(v0.f8) */\n")
TRY_FUNC(v79hvx_vmpy_hf_f8,
         ".word 0x1f80c0c0    /* v1:0.hf = vmpy(v0.f8, v0.f8) */\n")
TRY_FUNC(v79hvx_vmpy_hf_f8_acc,
         ".word 0x1f80c0e0    /* v1:0.hf += vmpy(v0.f8, v0.f8) */\n")
TRY_FUNC(v79hvx_vfneg_f8,
         ".word 0x1c66e0e2    /* v2.f8 = vfneg(v0.f8) */\n")
TRY_FUNC(v79hvx_vfmin_f8,
         ".word 0x1c61e082    /* v2.f8 = vfmin(v0.f8, v1.f8) */\n")

TRY_FUNC(v81hvx_veqhf,
         ".word 0x1f82c11c    /* q0 = vcmp.eq(v1.hf, v2.hf) */\n")
TRY_FUNC(v81hvx_vconv_qf16_f8,
         ".word 0x1e0ce2a0    /* v1:0.qf16 = v2.f8 */\n")
TRY_FUNC(v81hvx_vconv_f8_qf16,
         ".word 0x1e0ce4e3    /* v3.f8 = v4.qf16 */\n")

TRY_FUNC(v85hlx_assign_r,
         ".word 0xe520e422    /* L1.h = r0.h */\n")
TRY_FUNC(v85hlx_zero_store,
         ".word 0xa480c001    /* mem(r0) = L1.new */\n")

int main(void)
{
    struct sigaction act;

    memset(&act, 0, sizeof(act));
    act.sa_sigaction = handle_sigill;
    act.sa_flags = SA_SIGINFO;
    assert(sigaction(SIGILL, &act, NULL) == 0);

    assert(try_v68_loadw_aq() == SIGILL);
    assert(try_v68_loadd_aq() == SIGILL);
    assert(try_v68_release_at() == SIGILL);
    assert(try_v68_release_st() == SIGILL);
    assert(try_v68_storew_rl_at() == SIGILL);
    assert(try_v68_stored_rl_at() == SIGILL);
    assert(try_v68_storew_rl_st() == SIGILL);
    assert(try_v68_stored_rl_st() == SIGILL);

    assert(try_v68hvx_v6mpy() == SIGILL);

    assert(try_v68hvx_vabs_hf() == SIGILL);
    assert(try_v68hvx_vadd_hf_hf() == SIGILL);
    assert(try_v68hvx_vfmax_sf() == SIGILL);
    assert(try_v68hvx_vmpy_sf_hf() == SIGILL);

    assert(try_v69hvx_vasrvuhubrndsat() == SIGILL);
    assert(try_v69hvx_vasrvuhubsat() == SIGILL);
    assert(try_v69hvx_vasrvwuhrndsat() == SIGILL);
    assert(try_v69hvx_vasrvwuhsat() == SIGILL);
    assert(try_v69hvx_vassign_tmp() == SIGILL);
    assert(try_v69hvx_vcombine_tmp() == SIGILL);
    assert(try_v69hvx_vmpyuhvs() == SIGILL);

    assert(try_v73_callrh() == SIGILL);
    assert(try_v73_jumprh() == SIGILL);
    assert(try_v73_unpause() == SIGILL);

    assert(try_v73hvx_vmax_bf() == SIGILL);
    assert(try_v73hvx_vadd_sf_bf() == SIGILL);

    assert(try_v79_storeri_pi_nt() == SIGILL);
    assert(try_v79_pstorerit_pi_nt() == SIGILL);
    assert(try_v79_pstorerif_pi_nt() == SIGILL);
    assert(try_v79_dczeroa_nt() == SIGILL);
    assert(try_v79_dcfetchbo_nt() == SIGILL);
    assert(try_v83_callr_rb() == SIGILL);
    assert(try_v83_nop_rb() == SIGILL);
    assert(try_v83_vsfadd() == SIGILL);

    assert(try_v79hvx_vmerge_qf() == SIGILL);
    assert(try_v79hvx_vcvt2_hf_ub() == SIGILL);
    assert(try_v79hvx_vcvt2_b_hf() == SIGILL);
    assert(try_v79hvx_vcvt_f8_hf() == SIGILL);
    assert(try_v79hvx_vcvt_hf_f8() == SIGILL);
    assert(try_v79hvx_vmpy_hf_f8() == SIGILL);
    assert(try_v79hvx_vmpy_hf_f8_acc() == SIGILL);
    assert(try_v79hvx_vfneg_f8() == SIGILL);
    assert(try_v79hvx_vfmin_f8() == SIGILL);

    assert(try_v81hvx_veqhf() == SIGILL);
    assert(try_v81hvx_vconv_qf16_f8() == SIGILL);
    assert(try_v81hvx_vconv_f8_qf16() == SIGILL);

    assert(try_v85hlx_assign_r() == SIGILL);
    assert(try_v85hlx_zero_store() == SIGILL);

    assert(signals_handled == expected_signals);

    puts("PASS");
    return EXIT_SUCCESS;
}
