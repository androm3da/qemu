/*
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * Shared helpers for HLX (Hexagon eLement-wise eXtensions) tests
 *
 * All HLX tests compile with:
 *   hexagon-clang -mv85 -mhlx -G0 -O2 -static
 * and run with:
 *   qemu-system-hexagon -M V85QA_1 -display none -kernel <test>
 *   hexagon-sim -mv85qa_1 <test>
 */

#ifndef HLX_TEST_H
#define HLX_TEST_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef HLX_NO_VTCM
#include <hexagon_standalone.h>
#include <hexagon_types.h>
#include <hlx_hexagon_protos.h>
#endif

static int err;

/* Enable HLX via CCR.XE3 (bit 28) */
static inline void enable_hlx(void)
{
    uint32_t tmp;
    asm volatile(
        "%0 = ccr\n"
        "%0 = setbit(%0, #28)\n"
        "ccr = %0\n"
        "isync\n"
        : "=&r"(tmp)
    );
}

/* Verify halfword (uint16_t) array against expected values */
static void check_h(const char *name, const uint16_t *out,
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
static void check_w(const char *name, const uint32_t *out,
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
static void check_wp(const char *name, const uint32_t *out_lo,
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
 * All buffers must be in VTCM and 2048-byte aligned for
 * L-register loads/stores.
 */
#define HLX_HALF_ELEMS  1024
#define HLX_WORD_ELEMS  512
#define HLX_VEC_BYTES   2048
#define HLX_ALIGN       __attribute__((aligned(2048)))

#ifndef HLX_NO_VTCM
/*
 * VTCM (Vectorized Tightly Coupled Memory) discovery and allocation.
 *
 * HLX memory operations require buffers to reside in VTCM. The VTCM base
 * address is read from the hardware config table at offset 0x38.
 *
 * We must set up explicit TLB entries with uncached attributes (cache_attr=7)
 * so that CPU writes to VTCM are not trapped in L1/L2 cache. Without this,
 * the HLX coprocessor (which reads directly from physical VTCM) would see
 * stale/zero data.
 */
static void *vtcm_base_ptr;
static size_t vtcm_offset;

/* Read VTCM base address from hardware config table and map it uncached */
static inline void init_vtcm(void)
{
    uint32_t base;
    asm volatile(
        "r1 = cfgbase\n"
        "r1 = asl(r1, #5)\n"
        "r2 = #0x38\n"
        "r1 = memw_phys(r2, r1)\n"
        "%0 = asl(r1, #16)\n"
        : "=r"(base) : : "r1", "r2"
    );
    vtcm_base_ptr = (void *)(uintptr_t)base;
    vtcm_offset = 0;

    /* Map 2MB of VTCM with uncached attributes (two 1MB pages) */
    uint64_t pa = (uint64_t)base;
    add_translation_fixed(1, vtcm_base_ptr, (void *)pa, 6, 0);
    add_translation_fixed(2, (char *)vtcm_base_ptr + 1024 * 1024,
                          (void *)(pa + 1024 * 1024), 6, 0);
}

/*
 * Flush data cache for a buffer to ensure CPU writes reach physical VTCM.
 * Uses dccleaninva (clean + invalidate) per cache line (32 bytes).
 */
static inline void vtcm_flush(const void *ptr, size_t bytes)
{
    const char *p = (const char *)ptr;
    const char *end = p + bytes;
    for (; p < end; p += 32) {
        asm volatile("dccleaninva(%0)" : : "r"(p));
    }
}

/* Allocate a 2KB-aligned buffer from VTCM */
static inline void *vtcm_alloc(size_t bytes)
{
    vtcm_offset = (vtcm_offset + (HLX_VEC_BYTES - 1))
                  & ~(size_t)(HLX_VEC_BYTES - 1);
    void *ptr = (char *)vtcm_base_ptr + vtcm_offset;
    vtcm_offset += bytes;
    return ptr;
}

/*
 * Set up the Vector Stack Pointer (VSP) for L-register spills.
 *
 * The compiler uses VSP when it needs to spill HLX_Vector values.
 * VSP must point to VTCM memory (with at least HLX_VEC_BYTES of space
 * per spill slot, aligned to HLX_VEC_BYTES).
 *
 * Call this after init_vtcm() and before any HLX intrinsic usage.
 * Reserves HLX_VSP_SIZE bytes of VTCM for the spill area.
 */
#define HLX_VSP_SIZE  (16 * HLX_VEC_BYTES) /* 32 KB for spill slots */

static inline void init_hlx_vsp(void)
{
    void *vsp_area = vtcm_alloc(HLX_VSP_SIZE);
    /* VSP grows downward, so point it to the END of the area */
    uint32_t vsp_top = (uint32_t)(uintptr_t)vsp_area + HLX_VSP_SIZE;
    asm volatile("vsp = %0\n" : : "r"(vsp_top));
}

#endif /* !HLX_NO_VTCM */

#endif /* HLX_TEST_H */
