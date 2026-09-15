/*
 * Test the Hexagon user-DMA engine: dmstart/dmlink/dmpoll/dmwait/
 * dmpause/dmresume, type0 (linear) and type1 (2D box) descriptors.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

int err;

#include "hex_test.h"

#define DM0_STATUS_IDLE   0x00000000
#define DM0_STATUS_ERROR  0x00000002

#define DESC_DESCTYPE_TYPE0  (0u << 24)
#define DESC_DESCTYPE_TYPE1  (1u << 24)

/* desc[0]=next desc[1]=ctrl desc[2]=src desc[3]=dst */
typedef uint32_t type0_desc_t[4] __attribute__((aligned(16)));
/* + desc[4]=alloc/padding desc[5]=roi desc[6]=stride */
typedef uint32_t type1_desc_t[7] __attribute__((aligned(16)));

static inline void dmstart(uint32_t desc_va)
{
    asm volatile("dmstart(%[d])\n" : : [d] "r"(desc_va) : "memory");
}

static inline void dmresume(uint32_t desc_va)
{
    asm volatile("dmresume(%[d])\n" : : [d] "r"(desc_va) : "memory");
}

static inline void dmlink(uint32_t new_va, uint32_t tail_va)
{
    asm volatile("dmlink(%[new],%[tail])\n"
                 : : [new] "r"(new_va), [tail] "r"(tail_va) : "memory");
}

static inline uint32_t dmpoll(void)
{
    uint32_t status;
    asm volatile("%[out] = dmpoll\n" : [out] "=r"(status));
    return status;
}

static inline uint32_t dmwait(void)
{
    uint32_t status;
    asm volatile("%[out] = dmwait\n" : [out] "=r"(status));
    return status;
}

static inline uint32_t dmpause(void)
{
    uint32_t status;
    asm volatile("%[out] = dmpause\n" : [out] "=r"(status));
    return status;
}

static void test_type0_single(void)
{
    static type0_desc_t desc;
    static uint8_t src[64], dst[64];

    for (int i = 0; i < sizeof(src); i++) {
        src[i] = i + 1;
    }
    memset(dst, 0, sizeof(dst));

    desc[0] = 0; /* next */
    desc[1] = DESC_DESCTYPE_TYPE0 | sizeof(src);
    desc[2] = (uint32_t)(uintptr_t)src;
    desc[3] = (uint32_t)(uintptr_t)dst;

    dmstart((uint32_t)(uintptr_t)desc);

    check32(dmpoll(), DM0_STATUS_IDLE);
    check32(dmwait(), DM0_STATUS_IDLE);
    for (int i = 0; i < sizeof(dst); i++) {
        check32(dst[i], src[i]);
    }
}

static void test_type0_chain(void)
{
    static type0_desc_t desc0, desc1;
    static uint8_t src0[32], src1[32], dst0[32], dst1[32];

    for (int i = 0; i < sizeof(src0); i++) {
        src0[i] = i + 1;
        src1[i] = 0x80 + i;
    }
    memset(dst0, 0, sizeof(dst0));
    memset(dst1, 0, sizeof(dst1));

    desc0[0] = 0; /* next, patched by dmlink below */
    desc0[1] = DESC_DESCTYPE_TYPE0 | sizeof(src0);
    desc0[2] = (uint32_t)(uintptr_t)src0;
    desc0[3] = (uint32_t)(uintptr_t)dst0;

    desc1[0] = 0;
    desc1[1] = DESC_DESCTYPE_TYPE0 | sizeof(src1);
    desc1[2] = (uint32_t)(uintptr_t)src1;
    desc1[3] = (uint32_t)(uintptr_t)dst1;

    dmlink((uint32_t)(uintptr_t)desc1, (uint32_t)(uintptr_t)desc0);
    check32(desc0[0], (uint32_t)(uintptr_t)desc1);

    dmstart((uint32_t)(uintptr_t)desc0);

    check32(dmpoll(), DM0_STATUS_IDLE);
    for (int i = 0; i < sizeof(dst0); i++) {
        check32(dst0[i], src0[i]);
    }
    for (int i = 0; i < sizeof(dst1); i++) {
        check32(dst1[i], src1[i]);
    }
}

static void test_type1_box(void)
{
    static type1_desc_t desc;
    static uint8_t src[8 * 16], dst[8 * 16];
    const uint32_t width = 5, height = 4;
    const uint32_t srcstride = 16, dststride = 16;

    for (int i = 0; i < sizeof(src); i++) {
        src[i] = i + 1;
    }
    memset(dst, 0, sizeof(dst));

    desc[0] = 0;
    desc[1] = DESC_DESCTYPE_TYPE1;
    desc[2] = (uint32_t)(uintptr_t)src;
    desc[3] = (uint32_t)(uintptr_t)dst;
    desc[4] = 0;
    desc[5] = (height << 16) | width;
    desc[6] = (dststride << 16) | srcstride;

    dmstart((uint32_t)(uintptr_t)desc);

    check32(dmpoll(), DM0_STATUS_IDLE);
    for (uint32_t row = 0; row < height; row++) {
        for (uint32_t col = 0; col < width; col++) {
            check32(dst[row * dststride + col], src[row * srcstride + col]);
        }
        for (uint32_t col = width; col < dststride; col++) {
            check32(dst[row * dststride + col], 0);
        }
    }
}

static void test_dmpause_after_completion(void)
{
    static type0_desc_t desc;
    static uint8_t src[16], dst[16];

    memset(src, 0x5a, sizeof(src));
    memset(dst, 0, sizeof(dst));

    desc[0] = 0;
    desc[1] = DESC_DESCTYPE_TYPE0 | sizeof(src);
    desc[2] = (uint32_t)(uintptr_t)src;
    desc[3] = (uint32_t)(uintptr_t)dst;

    dmstart((uint32_t)(uintptr_t)desc);
    check32(dmpause(), DM0_STATUS_IDLE);

    /* dmresume restarts the same descriptor; the copy is idempotent. */
    dmresume((uint32_t)(uintptr_t)desc);
    check32(dmwait(), DM0_STATUS_IDLE);
    for (int i = 0; i < sizeof(dst); i++) {
        check32(dst[i], src[i]);
    }
}

static void test_misaligned_descriptor(void)
{
    static uint8_t buf[32] __attribute__((aligned(16)));

    /* buf + 1 is guaranteed misaligned relative to the 16-byte requirement. */
    dmstart((uint32_t)(uintptr_t)(buf + 1));
    check32(dmpoll(), DM0_STATUS_ERROR);
}

int main(void)
{
    test_type0_single();
    test_type0_chain();
    test_type1_box();
    test_dmpause_after_completion();
    test_misaligned_descriptor();

    puts(err ? "FAIL" : "PASS");
    return err;
}
