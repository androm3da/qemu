/*
 * Shared machinery for the revision gating tests: execute one raw
 * encoding and report whether it raised SIGILL.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HEXAGON_REV_GATING_H
#define HEXAGON_REV_GATING_H

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
 * Try to execute an instruction introduced after v66
 * On a v66 CPU this must raise SIGILL.
 *
 * Since we are building for v66, the assembler will reject
 * the instructions, so introduce them with .word.
 */
#define TRY_FUNC(NAME, WORD) \
static int try_##NAME(void) \
{ \
    int sig; \
    expected_signals++; \
    asm volatile( \
        "r0 = #0\n" \
        "r1 = ##1f\n" \
        "memw(%1) = r1\n" \
        WORD \
        "1:\n" \
        "%0 = r0\n" \
        : "=r"(sig) \
        : "r"(&resume_pc) \
        : "r0", "r1", "memory"); \
    return sig; \
}

#endif /* HEXAGON_REV_GATING_H */
