/*
 * HMX multiply behaviour around guest memory:
 *
 *  - A multiply that faults part-way through (here, on a page that is mapped
 *    in by a SIGSEGV handler) must leave the accumulators as they were, so
 *    that the restarted instruction produces the same result as one that
 *    never faulted.
 *
 *  - An activation load can declare far more croutons than the multiply
 *    has MAC cycles for.  The multiply must stop when the budget is
 *    spent instead of reading every declared crouton.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

int err;

#include "hex_test.h"

#define CROUTON_SIZE   2048
#define OUT_SIZE       2048
#define FP16_ONE       0x3C00u
#define FXP_BIAS       0x5000u
#define FP_BIAS        FP16_ONE

static uint32_t bias_buf[32] __attribute__((aligned(128)));
static uint8_t out_buf[OUT_SIZE] __attribute__((aligned(2048)));

/* The guest page size, as in scatter_gather.c. */
static const size_t page_size = 4096;
static uint8_t *fault_page;
/* Written by the SIGSEGV handler, read by the main flow. */
static volatile int faults;

/* Map in the page the multiply was reading, so that it can restart. */
static void map_in(int sig, siginfo_t *info, void *puc)
{
    uintptr_t addr = (uintptr_t)info->si_addr;

    if (addr < (uintptr_t)fault_page ||
        addr >= (uintptr_t)fault_page + page_size) {
        _exit(EXIT_FAILURE);
    }
    faults++;
    if (mprotect(fault_page, page_size, PROT_READ | PROT_WRITE)) {
        _exit(EXIT_FAILURE);
    }
}

static void unexpected_segv(int sig, siginfo_t *info, void *puc)
{
    static const char msg[] = "FAIL: read past the MAC cycle budget\n";
    ssize_t ret = write(STDOUT_FILENO, msg, sizeof(msg) - 1);

    (void)ret;
    _exit(EXIT_FAILURE);
}

static void set_segv_handler(void (*fn)(int, siginfo_t *, void *))
{
    struct sigaction act;

    memset(&act, 0, sizeof(act));
    act.sa_flags = SA_SIGINFO;
    act.sa_sigaction = fn;
    chk_error(sigaction(SIGSEGV, &act, NULL));
}

static void *map_pages(size_t n)
{
    uint8_t *p = mmap(NULL, n * page_size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (p == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }
    return p;
}

static void load_bias(uint32_t low32)
{
    for (int i = 0; i < 32; i++) {
        bias_buf[i] = low32;
    }
    asm volatile("r8 = %[bias]\n"
                 ".word 0x9208c3ff    /* bias=mxmem(r8) */\n"
                 :
                 : [bias] "r"(bias_buf)
                 : "r8", "memory");
}

/*
 * One FXP multiply of a single crouton with the weights at @wei, stored
 * to out_buf.  @rt3 is the activation's Rt, @rt5 the weights'.
 */
static void run_fxp(const void *act, uint32_t rt3, const void *wei,
                    uint32_t rt5, int deep)
{
    memset(out_buf, 0xff, sizeof(out_buf));
    asm volatile(".word 0xa6e0c011    /* mxclracc */" : : : "memory");
    load_bias(FXP_BIAS);

    if (deep) {
        asm volatile("r2 = %[act]\n"
                     "r3 = %[rt3]\n"
                     ".word 0x9202c3e0    /* activation.ub:deep */\n"
                     :
                     : [act] "r"(act), [rt3] "r"(rt3)
                     : "r2", "r3", "memory");
    } else {
        asm volatile("r2 = %[act]\n"
                     "r3 = %[rt3]\n"
                     ".word 0x9202c3ec    /* activation.ub=mxmem(r2,r3) */\n"
                     :
                     : [act] "r"(act), [rt3] "r"(rt3)
                     : "r2", "r3", "memory");
    }
    asm volatile("r4 = %[wei]\n"
                 "r5 = %[rt5]\n"
                 ".word 0x9204e5e0    /* weight.b=mxmem(r4,r5) */\n"
                 :
                 : [wei] "r"(wei), [rt5] "r"(rt5)
                 : "r4", "r5", "memory");
    asm volatile("r6 = %[out]\n"
                 "r7 = #0\n"
                 ".word 0xa6e6c704    "
                 "/* mxmem(r6,r7):after:sat.ub=acc */\n"
                 :
                 : [out] "r"(out_buf)
                 : "r6", "r7", "memory");
}

/* The same with FP16 activations and weights. */
static void run_fp(const void *act, uint32_t rt3, const void *wei,
                   uint32_t rt5, int deep)
{
    memset(out_buf, 0xff, sizeof(out_buf));
    asm volatile(".word 0xa6e0c013    /* mxclracc.hf */" : : : "memory");
    load_bias(FP_BIAS);

    if (deep) {
        asm volatile("r2 = %[act]\n"
                     "r3 = %[rt3]\n"
                     ".word 0x9202c3e2    /* activation.hf:deep */\n"
                     :
                     : [act] "r"(act), [rt3] "r"(rt3)
                     : "r2", "r3", "memory");
    } else {
        asm volatile("r2 = %[act]\n"
                     "r3 = %[rt3]\n"
                     ".word 0x9202c3e4    /* activation.hf=mxmem(r2,r3) */\n"
                     :
                     : [act] "r"(act), [rt3] "r"(rt3)
                     : "r2", "r3", "memory");
    }
    asm volatile("r4 = %[wei]\n"
                 "r5 = %[rt5]\n"
                 ".word 0x9204e5ef    /* weight.hf=mxmem(r4,r5) */\n"
                 :
                 : [wei] "r"(wei), [rt5] "r"(rt5)
                 : "r4", "r5", "memory");
    asm volatile("r9 = #0\n"
                 ".word 0xa6e9da10    /* cvt.hf=acc(r9) */\n"
                 :
                 :
                 : "r9", "memory");
    asm volatile("r6 = %[out]\n"
                 "r7 = #0\n"
                 ".word 0xa6e6c718    /* mxmem(r6,r7)=cvt */\n"
                 :
                 : [out] "r"(out_buf)
                 : "r6", "r7", "memory");
}

/*
 * Run a multiply whose weights cross from a mapped page into one that is
 * not, and check that the restart gives the result of a run that did not
 * fault.  @fp selects the FP16 multiply, whose weights fill 16 vectors
 * (2048 bytes) instead of the FXP multiply's 8 (1024 bytes).
 */
static void test_restart(int fp)
{
    static uint8_t act[CROUTON_SIZE] __attribute__((aligned(2048)));
    static uint8_t ref[OUT_SIZE];
    size_t wei_size = fp ? 2048 : 1024;
    uint8_t *pages = map_pages(3);
    uint8_t *page_a = (uint8_t *)(((uintptr_t)pages + page_size - 1) &
                                  ~(page_size - 1));
    /* Half of the weight vectors end up on each side of the boundary. */
    uint8_t *wei = page_a + page_size - wei_size / 2;

    if (fp) {
        uint16_t *p = (uint16_t *)act;

        for (int i = 0; i < CROUTON_SIZE / 2; i++) {
            p[i] = FP16_ONE;
        }
        p = (uint16_t *)page_a;
        for (size_t i = 0; i < 2 * page_size / 2; i++) {
            p[i] = FP16_ONE;
        }
    } else {
        memset(act, 3, sizeof(act));
        memset(page_a, 5, 2 * page_size);
    }

    fault_page = page_a + page_size;
    faults = 0;
    set_segv_handler(map_in);

    if (fp) {
        run_fp(act, 0x7c, wei, wei_size - 1, 0);
    } else {
        run_fxp(act, 0x7c, wei, wei_size - 1, 0);
    }
    check32(faults, 0);
    check32_ne(out_buf[0], 0xff);
    memcpy(ref, out_buf, sizeof(ref));

    chk_error(mprotect(fault_page, page_size, PROT_NONE));
    if (fp) {
        run_fp(act, 0x7c, wei, wei_size - 1, 0);
    } else {
        run_fxp(act, 0x7c, wei, wei_size - 1, 0);
    }
    check32(faults, 1);
    check32(memcmp(out_buf, ref, sizeof(ref)), 0);

    munmap(pages, 3 * page_size);
}

/*
 * Declare many more croutons than the MAC cycle budget covers (64 for a
 * single tap), with memory for only 96 of them followed by an unmapped
 * page.  The multiply has to stop before the unmapped page.
 */
static void test_budget(int fp)
{
    const size_t mapped = 96 * CROUTON_SIZE;
    const uint32_t declared = 4096;
    uint8_t *pages = map_pages(2 + (mapped + page_size - 1) / page_size + 1);
    uint8_t *wei = pages;
    /* The pages are page aligned, which is more than a crouton needs. */
    uint8_t *act = pages + 2 * page_size;
    uint8_t *guard = act + mapped;

    guard = (uint8_t *)(((uintptr_t)guard + page_size - 1) & ~(page_size - 1));
    chk_error(mprotect(guard, page_size, PROT_NONE));
    set_segv_handler(unexpected_segv);

    if (fp) {
        uint16_t *p = (uint16_t *)act;

        for (size_t i = 0; i < mapped / 2; i++) {
            p[i] = FP16_ONE;
        }
        p = (uint16_t *)wei;
        for (size_t i = 0; i < page_size; i++) {
            p[i] = FP16_ONE;
        }
        run_fp(act, ((declared - 1) << 11) | 0x7c, wei, 2048 - 1, 1);
    } else {
        memset(act, 3, mapped);
        memset(wei, 5, 2 * page_size);
        run_fxp(act, ((declared - 1) << 11) | 0x7c, wei, 1024 - 1, 1);
    }
    check32_ne(out_buf[0], 0xff);

    munmap(pages, (2 + (mapped + page_size - 1) / page_size + 1) * page_size);
}

int main(void)
{
    test_restart(0);
    test_restart(1);
    test_budget(0);
    test_budget(1);

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
