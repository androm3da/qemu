/*
 * Test that the HMX (matrix) instructions, introduced at v75, are
 * rejected with SIGILL on every earlier CPU that has a revision-gated
 * decoder entry: v68, v69, v71 and v73 (see meson.build).
 *
 * The assembler doesn't know the HMX mnemonics, so the instructions
 * are introduced with .word (see check_rev_gating.c).
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

static void *resume_pc;
static int signals_handled;
static int expected_signals;

static void handle_sigill(int sig, siginfo_t *info, void *puc)
{
    ucontext_t *uc = (ucontext_t *)puc;

    if (sig != SIGILL) {
        _exit(EXIT_FAILURE);
    }

    uc->uc_mcontext.r0 = SIGILL;
    uc->uc_mcontext.pc = (unsigned long)resume_pc;
    signals_handled++;
}

/*
 * Execute one instruction with r0 = 0 and return r0 afterwards: SIGILL
 * if the instruction was rejected, otherwise whatever it left in r0.
 */
#define TRY_FUNC(NAME, WORD) \
static int try_##NAME(void) \
{ \
    int sig; \
    asm volatile( \
        "r0 = #0\n" \
        "r1 = ##1f\n" \
        "memw(%[resume]) = r1\n" \
        WORD \
        "1:\n" \
        "%[sig] = r0\n" \
        : [sig] "=r"(sig) \
        : [resume] "r"(&resume_pc) \
        : "r0", "r1", "memory"); \
    return sig; \
}

TRY_FUNC(mxclracc,
         ".word 0xa6e0c011    /* mxclracc */\n")
TRY_FUNC(mxclracc_hf,
         ".word 0xa6e0c013    /* mxclracc.hf */\n")
TRY_FUNC(mxaccshl,
         ".word 0xa6e0c017    /* mxaccshl */\n")
TRY_FUNC(bias_load,
         ".word 0x9208c3ff    /* bias=mxmem(r8) */\n")
TRY_FUNC(cvt_rs_ub,
         ".word 0xa6e0d710    /* cvt.ub=acc(r0) */\n")
TRY_FUNC(cvt_rs_hf,
         ".word 0xa6e0da10    /* cvt.hf=acc(r0) */\n")
TRY_FUNC(mxmem_store,
         ".word 0xa6e0c118    /* mxmem(r0,r1)=cvt */\n")

int main(void)
{
    struct sigaction act;

    memset(&act, 0, sizeof(act));
    act.sa_sigaction = handle_sigill;
    act.sa_flags = SA_SIGINFO;
    assert(sigaction(SIGILL, &act, NULL) == 0);

    assert(try_mxclracc() == SIGILL);
    expected_signals++;
    assert(try_mxclracc_hf() == SIGILL);
    expected_signals++;
    assert(try_mxaccshl() == SIGILL);
    expected_signals++;
    assert(try_bias_load() == SIGILL);
    expected_signals++;
    assert(try_cvt_rs_ub() == SIGILL);
    expected_signals++;
    assert(try_cvt_rs_hf() == SIGILL);
    expected_signals++;
    assert(try_mxmem_store() == SIGILL);
    expected_signals++;

    assert(signals_handled == expected_signals);

    puts("PASS");
    return EXIT_SUCCESS;
}
