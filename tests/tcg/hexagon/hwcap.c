/*
 * Test that AT_HWCAP reports the capabilities the CPU really has.
 *
 * The expected ISA revision and feature bits come from the command line
 * because the same source runs against several CPU models and property
 * settings.  Beside the reported bits, check that the reporting agrees
 * with what the CPU executes: HVX instructions must raise SIGILL exactly
 * when HVX is not reported.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdbool.h>
#include <sys/auxv.h>
#include "rev_gating.h"

#ifndef EXPECT_ISA
#error "EXPECT_ISA is the HWCAP_HEXAGON_ISA value of the CPU under test"
#endif

/* The toolchain headers may predate some of these. */
#define HWCAP_ISA_MASK          0x7f
#define HWCAP_HVX               (1 << 7)
#define HWCAP_HVX_LENGTH_128B   (1 << 9)
#define HWCAP_HVX_IEEE_FP       (1 << 10)
#define HWCAP_HMX               (1 << 12)

TRY_FUNC(hvx_vaddb,
         ".word 0x1fa2c1c0    /* v0.b = vadd(v1.b, v2.b) */\n")

static void check_bit(unsigned long hwcap, unsigned long bit,
                      bool expected, const char *name)
{
    bool reported = (hwcap & bit) != 0;

    if (reported != expected) {
        printf("FAIL: %s is %sreported, expected %sreported\n", name,
               reported ? "" : "not ", expected ? "" : "not ");
        exit(EXIT_FAILURE);
    }
}

int main(void)
{
    unsigned long hwcap = getauxval(AT_HWCAP);
    struct sigaction act;

    if ((hwcap & HWCAP_ISA_MASK) != EXPECT_ISA) {
        printf("FAIL: ISA %lu, expected %d\n", hwcap & HWCAP_ISA_MASK,
               EXPECT_ISA);
        return EXIT_FAILURE;
    }
    check_bit(hwcap, HWCAP_HVX, EXPECT_HVX, "HVX");
    check_bit(hwcap, HWCAP_HVX_LENGTH_128B, EXPECT_HVX, "HVX 128B");
    check_bit(hwcap, HWCAP_HVX_IEEE_FP, EXPECT_HVX_IEEE_FP, "HVX IEEE FP");
    check_bit(hwcap, HWCAP_HMX, EXPECT_HMX, "HMX");

    memset(&act, 0, sizeof(act));
    act.sa_sigaction = handle_sigill;
    act.sa_flags = SA_SIGINFO;
    assert(sigaction(SIGILL, &act, NULL) == 0);

    /* HVX is reported exactly when its instructions execute. */
    assert(try_hvx_vaddb() == (EXPECT_HVX ? 0 : SIGILL));
    assert(signals_handled == (EXPECT_HVX ? 0 : 1));

    puts("PASS");
    return EXIT_SUCCESS;
}
