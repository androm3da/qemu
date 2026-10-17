/*
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * Shared helpers for the HLX (Hexagon eLement-wise eXtensions) tests
 *
 * The HLX instructions are encoded with .word directives, see hlx_asm.h,
 * so the tests build with a toolchain that does not know about HLX.
 *
 * The tests run on Linux, where QEMU's user-mode init enables HLX:
 *   hexagon-unknown-linux-musl-clang -O2 -static
 *   qemu-hexagon -cpu v85 <test>
 * and on the SDK's Standalone OS, where the test has to enable HLX and
 * map VTCM itself:
 *   hexagon-clang -mv85 -O2
 *   hexagon-sim -mv85qa_1 <test>
 */

#ifndef HLX_TEST_H
#define HLX_TEST_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hlx_asm.h"

static int err;

/* Verify halfword (uint16_t) array against expected values */
static inline void check_h(const char *name, const uint16_t *out,
                     const uint16_t *expected, int count)
{
    for (int i = 0; i < count; i++) {
        if (out[i] != expected[i]) {
            printf("FAIL %s [%d]: expected 0x%04x, got 0x%04x\n",
                   name, i, expected[i], out[i]);
            err++;
            if (err > 20) {
                printf("Too many errors, stopping %s\n", name);
                return;
            }
        }
    }
}

/* Verify word (uint32_t) array against expected values */
static inline void check_w(const char *name, const uint32_t *out,
                     const uint32_t *expected, int count)
{
    for (int i = 0; i < count; i++) {
        if (out[i] != expected[i]) {
            printf("FAIL %s [%d]: expected 0x%08lx, got 0x%08lx\n",
                   name, i, (unsigned long)expected[i],
                   (unsigned long)out[i]);
            err++;
            if (err > 20) {
                printf("Too many errors, stopping %s\n", name);
                return;
            }
        }
    }
}

/*
 * Verify word-pair values by reconstructing 32-bit words from the lo/hi
 * L-register buffers.  In HLX, a word pair is halfword-interleaved:
 *   word[i] = (hi.h[i] << 16) | lo.h[i]
 * Each L-register has HLX_HALF_ELEMS halfwords, giving HLX_HALF_ELEMS
 * 32-bit word elements per pair.
 */
static inline void check_wp(const char *name, const uint32_t *out_lo,
                      const uint32_t *out_hi, const uint32_t *expected,
                      int count)
{
    const uint16_t *lo = (const uint16_t *)out_lo;
    const uint16_t *hi = (const uint16_t *)out_hi;
    for (int i = 0; i < count; i++) {
        uint32_t got = ((uint32_t)hi[i] << 16) | lo[i];
        if (got != expected[i]) {
            printf("FAIL %s [%d]: expected 0x%08lx, got 0x%08lx\n",
                   name, i, (unsigned long)expected[i],
                   (unsigned long)got);
            err++;
            if (err > 20) {
                printf("Too many errors, stopping %s\n", name);
                return;
            }
        }
    }
}

/* Saturation helpers for reference computations */
static inline int16_t sat_h(int32_t val)
{
    if (val > 32767) {
        return 32767;
    }
    if (val < -32768) {
        return -32768;
    }
    return (int16_t)val;
}

static inline uint16_t sat_uh(int32_t val)
{
    if (val > 65535) {
        return 65535;
    }
    if (val < 0) {
        return 0;
    }
    return (uint16_t)val;
}

static inline int32_t sat_w(int64_t val)
{
    if (val > 2147483647LL) {
        return 2147483647;
    }
    if (val < -2147483648LL) {
        return -2147483648;
    }
    return (int32_t)val;
}

static inline uint32_t sat_uw(int64_t val)
{
    if (val > 4294967295LL) {
        return 4294967295U;
    }
    if (val < 0) {
        return 0;
    }
    return (uint32_t)val;
}

/*
 * HLX register size: 2048 bytes = 1024 halfwords = 512 words
 * All buffers must be 2048-byte aligned for L-register loads/stores.
 */
#define HLX_HALF_ELEMS  1024
#define HLX_WORD_ELEMS  512
#define HLX_VEC_BYTES   2048
#define HLX_ALIGN       __attribute__((aligned(2048)))

#if defined(__linux__)

/*
 * Linux user-mode memory allocation for HLX buffers.
 *
 * HLX memory operations work on regular aligned heap memory, QEMU handles
 * the memory access uniformly, and QEMU's user-mode init enables HLX
 * (CCR.XE3 = 1).
 */

/* No-op on Linux - no VTCM discovery or TLB setup needed */
static inline void init_vtcm(void) { }

/* No-op on Linux - HLX is already enabled */
static inline void enable_hlx(void) { }

/* Allocate a 2KB-aligned buffer from the heap */
static inline void *vtcm_alloc(size_t bytes)
{
    size_t aligned_bytes = (bytes + (HLX_VEC_BYTES - 1))
                           & ~(size_t)(HLX_VEC_BYTES - 1);
    void *ptr = aligned_alloc(HLX_VEC_BYTES, aligned_bytes);
    if (!ptr) {
        fprintf(stderr, "aligned_alloc failed for %zu bytes\n", aligned_bytes);
        exit(1);
    }
    memset(ptr, 0, aligned_bytes);
    return ptr;
}

#else /* Standalone OS */

#include <hexagon_standalone.h>

/*
 * Standalone OS memory allocation for HLX buffers.
 *
 * HLX memory operations only work on VTCM (Vectorized Tightly Coupled
 * Memory), which needs explicit TLB mappings.
 */
static void *vtcm_base_ptr;
static size_t vtcm_offset;

/* Find VTCM through the config table and map the first 2MB of it */
static inline void init_vtcm(void)
{
    uint32_t base;
    asm volatile("r1 = cfgbase\n"
                 "r1 = asl(r1, #5)\n"
                 "r2 = #0x38\n"
                 "r1 = memw_phys(r2, r1)\n"
                 "%[base] = asl(r1, #16)\n"
                 : [base] "=r"(base) : : "r1", "r2");
    vtcm_base_ptr = (void *)(uintptr_t)base;
    vtcm_offset = 0;

    uint64_t pa = (uint64_t)base;
    add_translation_fixed(1, vtcm_base_ptr, (void *)pa, 6, 0);
    add_translation_fixed(2, (char *)vtcm_base_ptr + 1024 * 1024,
                          (void *)(pa + 1024 * 1024), 6, 0);
}

/* Allocate a 2KB-aligned buffer from VTCM, call init_vtcm() first */
static inline void *vtcm_alloc(size_t bytes)
{
    vtcm_offset = (vtcm_offset + (HLX_VEC_BYTES - 1))
                  & ~(size_t)(HLX_VEC_BYTES - 1);
    void *ptr = (char *)vtcm_base_ptr + vtcm_offset;
    vtcm_offset += bytes;
    return ptr;
}

/*
 * Enable HLX (CCR.XE3).  The intrinsics of hlx_asm.h stage their operands
 * in VTCM too, so call init_vtcm() first.
 */
static inline void enable_hlx(void)
{
    uint32_t tmp;
    asm volatile("%[tmp] = ccr\n"
                 "%[tmp] = setbit(%[tmp], #28)\n"
                 "ccr = %[tmp]\n"
                 "isync\n"
                 : [tmp] "=&r"(tmp));
    hlx_stage = vtcm_alloc(HLX_ASM_STAGE_BYTES);
}

#endif /* __linux__ */

/*
 * Set up the Vector Stack Pointer (VSP) for L-register spills.
 *
 * A compiler that allocates L registers uses VSP to spill them.  VSP must
 * point to memory with at least HLX_VEC_BYTES of space per spill slot,
 * aligned to HLX_VEC_BYTES.
 *
 * Reserves HLX_VSP_SIZE bytes for the spill area.
 */
#define HLX_VSP_SIZE  (16 * HLX_VEC_BYTES) /* 32 KB for spill slots */

static inline void init_hlx_vsp(void)
{
    void *vsp_area = vtcm_alloc(HLX_VSP_SIZE);
    /* VSP grows downward, so point it to the END of the area */
    uint32_t vsp_top = (uint32_t)(uintptr_t)vsp_area + HLX_VSP_SIZE;
    asm volatile("r0 = %[top]\n"
                 ".word 0x6220c01d\n" /* vsp = r0 */
                 : : [top] "r"(vsp_top) : "r0");
}

#endif /* HLX_TEST_H */
