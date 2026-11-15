/*
 * Test that the HVX IEEE floating point instructions are gated by the
 * revision that introduced each of them.  The half and single precision
 * operations arrived with v68 and the bfloat16 operations with v73, so a
 * v68 CPU must execute the first group and reject the second with SIGILL.
 *
 * The encodings are .word directives because the assembler enforces the
 * revision of the file's own CPU for the second group.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "rev_gating.h"

TRY_FUNC(v68hvx_vabs_hf,
         ".word 0x1e06e180    /* v0.hf = vabs(v1.hf) */\n")
TRY_FUNC(v68hvx_vfmax_sf,
         ".word 0x1c62e160    /* v0.sf = vfmax(v1.sf, v2.sf) */\n")

TRY_FUNC(v73hvx_vmax_bf,
         ".word 0x1d42e1e0    /* v0.bf = vmax(v1.bf, v2.bf) */\n")
TRY_FUNC(v73hvx_vadd_sf_bf,
         ".word 0x1d43e2c0    /* v1:0.sf = vadd(v2.bf, v3.bf) */\n")

int main(void)
{
    struct sigaction act;

    memset(&act, 0, sizeof(act));
    act.sa_sigaction = handle_sigill;
    act.sa_flags = SA_SIGINFO;
    assert(sigaction(SIGILL, &act, NULL) == 0);

    /* Introduced by v68: executed, no signal. */
    assert(try_v68hvx_vabs_hf() == 0);
    assert(try_v68hvx_vfmax_sf() == 0);

    /* Introduced by v73: rejected. */
    assert(try_v73hvx_vmax_bf() == SIGILL);
    assert(try_v73hvx_vadd_sf_bf() == SIGILL);

    assert(signals_handled == 2);

    puts("PASS");
    return EXIT_SUCCESS;
}
