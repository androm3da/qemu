/*
 * HMX instructions that only exist on v75: mxaccshl shifts both FXP
 * accumulator sets left by 16 bits, and was removed at v79 (see
 * hmx_rev_gating.c).
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

int err;

#include "hex_test.h"

#define ACT_SIZE  2048  /* one crouton: 64 spatial x 32 channels */
#define WEI_SIZE  1024  /* 8 vectors x 128 bytes: 32 input channels, byte */
#define OUT_SIZE  2048

static uint8_t act_buf[ACT_SIZE] __attribute__((aligned(2048)));
static uint8_t wei_buf[WEI_SIZE] __attribute__((aligned(128)));
static uint32_t bias_buf[32] __attribute__((aligned(128)));
static uint8_t out_buf[OUT_SIZE] __attribute__((aligned(2048)));

/* 32 input channels of 3 * 5, then the byte convert's >> 4: see hmx_fxp.c */
#define EXPECTED_BYTE  30

/* Multiply, optionally shift the accumulators, and convert-and-store. */
static void run(int shift)
{
    memset(out_buf, 0xff, sizeof(out_buf));
    asm volatile(".word 0xa6e0c011    /* mxclracc */" : : : "memory");

    asm volatile("r8 = %[bias]\n"
                 ".word 0x9208c3ff    /* bias=mxmem(r8) */\n"
                 :
                 : [bias] "r"(bias_buf)
                 : "r8", "memory");
    asm volatile("r2 = %[act]\n"
                 "r3 = #0x7c\n"
                 ".word 0x9202c3ec    /* activation.ub=mxmem(r2,r3) */\n"
                 :
                 : [act] "r"(act_buf)
                 : "r2", "r3", "memory");
    asm volatile("r4 = %[wei]\n"
                 "r5 = #0xffff\n"
                 ".word 0x9204e5e0    /* weight.b=mxmem(r4,r5) */\n"
                 :
                 : [wei] "r"(wei_buf)
                 : "r4", "r5", "memory");
    if (shift) {
        asm volatile(".word 0xa6e0c017    /* mxaccshl */" : : : "memory");
    }
    asm volatile("r6 = %[out]\n"
                 "r7 = #0\n"
                 ".word 0xa6e6c704    "
                 "/* mxmem(r6,r7):after:sat.ub=acc */\n"
                 :
                 : [out] "r"(out_buf)
                 : "r6", "r7", "memory");
}

int main(void)
{
    memset(act_buf, 3, sizeof(act_buf));
    memset(wei_buf, 5, sizeof(wei_buf));
    for (int i = 0; i < 32; i++) {
        bias_buf[i] = 0x5000u;
    }

    run(0);
    for (int i = 0; i < OUT_SIZE; i++) {
        check32(out_buf[i], EXPECTED_BYTE);
    }

    /* 480 << 16 is far out of range, so the convert saturates. */
    run(1);
    for (int i = 0; i < OUT_SIZE; i++) {
        check32(out_buf[i], 0xff);
    }

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
