/*
 *  Test the scalar core instructions that are new in v79: the
 *  non-temporal post-increment stores (and their predicated forms)
 *  and the non-temporal cache-op variants of dcfetch/dczeroa.
 *
 *  Unlike J2_unpause (v73) and Y2_tlbpp (v81), none of these six are
 *  recognized by clang's integrated assembler (checked against the
 *  container's toolchain, clang 22.1.8, at -mv81: it rejects both
 *  assembling and disassembling every one of them, with no relevant
 *  -mattr feature gating them either), so each is embedded via a raw
 *  .word instead.  Behavior is otherwise identical to the plain
 *  (non-":nt") instruction: the ":nt" suffix is only a cache-locality
 *  hint.
 *
 *  The post-increment result is read back through memory rather than
 *  a compiler-allocated output register: a register named literally
 *  in the asm text (Rx here) is not visible to the compiler's data
 *  flow, so an output constraint reading it back on the very next
 *  line is not reliably scheduled after the packet that wrote it.
 *
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

int err;

static void __check32(int line, uint32_t result, uint32_t expect)
{
    if (result != expect) {
        printf("ERROR at line %d: 0x%08x != 0x%08x\n", line, result, expect);
        err++;
    }
}

#define check32(RES, EXP) __check32(__LINE__, RES, EXP)

static uint32_t sbuf[4];
static uint32_t newp;

/* S2_storeri_pi_nt: memw(Rx32++#4):nt = Rt32 */
static void test_storeri_pi_nt(void)
{
    uint32_t *p = sbuf;

    memset(sbuf, 0xff, sizeof(sbuf));
    asm volatile(
        "r2 = %1\n\t"
        "r3 = #0x12345678\n\t"
        ".word 0xaa82c308\n\t" /* memw(r2++#4):nt = r3 */
        "r5 = ##newp\n\t"
        "memw(r5 + #0) = r2\n\t"
        : "+m"(newp)
        : "r"(p)
        : "r2", "r3", "r5", "memory");
    check32(sbuf[0], 0x12345678);
    check32(newp, (uint32_t)(uintptr_t)(sbuf + 1));
}

static uint8_t bbuf[4];

/* S2_storerb_pi_nt: memb(Rx32++#1):nt = Rt32 */
static void test_storerb_pi_nt(void)
{
    uint32_t *p = (uint32_t *)bbuf;

    memset(bbuf, 0xff, sizeof(bbuf));
    asm volatile(
        "r2 = %1\n\t"
        "r3 = #0x42\n\t"
        ".word 0xaa02c308\n\t" /* memb(r2++#1):nt = r3 */
        "r5 = ##newp\n\t"
        "memw(r5 + #0) = r2\n\t"
        : "+m"(newp)
        : "r"(p)
        : "r2", "r3", "r5", "memory");
    check32(bbuf[0], 0x42);
    check32(newp, (uint32_t)(uintptr_t)(bbuf + 1));
}

/* S2_pstorerit_pi_nt: if (Pv4) memw(Rx32++#4):nt = Rt32, predicate true */
static void test_pstorerit_pi_nt(void)
{
    uint32_t *p = sbuf;

    memset(sbuf, 0xff, sizeof(sbuf));
    asm volatile(
        "p0 = cmp.eq(r0, r0)\n\t" /* P0 = true */
        "r2 = %1\n\t"
        "r3 = #0x11223344\n\t"
        ".word 0xaa82e308\n\t" /* if (p0) memw(r2++#4):nt = r3 */
        "r5 = ##newp\n\t"
        "memw(r5 + #0) = r2\n\t"
        : "+m"(newp)
        : "r"(p)
        : "r0", "r2", "r3", "r5", "p0", "memory");
    check32(sbuf[0], 0x11223344);
    check32(newp, (uint32_t)(uintptr_t)(sbuf + 1));
}

/* S2_pstorerif_pi_nt: if (!Pv4) memw(Rx32++#4):nt = Rt32, predicate false */
static void test_pstorerif_pi_nt(void)
{
    uint32_t *p = sbuf;

    memset(sbuf, 0xff, sizeof(sbuf));
    asm volatile(
        "r0 = #1\n\t"
        "r1 = #2\n\t"
        "p0 = cmp.eq(r0, r1)\n\t" /* P0 = false */
        "r2 = %1\n\t"
        "r3 = #0x99887766\n\t"
        ".word 0xaa82e30c\n\t" /* if (!p0) memw(r2++#4):nt = r3 */
        "r5 = ##newp\n\t"
        "memw(r5 + #0) = r2\n\t"
        : "+m"(newp)
        : "r"(p)
        : "r0", "r1", "r2", "r3", "r5", "p0", "memory");
    check32(sbuf[0], 0x99887766);
    check32(newp, (uint32_t)(uintptr_t)(sbuf + 1));
}

/* Y2_dczeroa_nt: dczeroa(Rs32):nt -- zeroes a 32-byte cacheline */
static uint8_t cz[64] __attribute__((aligned(32)));

static void test_dczeroa_nt(void)
{
    memset(cz, 0xaa, sizeof(cz));
    asm volatile(
        "r2 = %1\n\t"
        ".word 0xa0c2e000\n\t" /* dczeroa(r2):nt */
        : "+m"(cz)
        : "r"(cz)
        : "r2", "memory");
    for (int i = 0; i < 32; i++) {
        check32(cz[i], 0);
    }
    for (int i = 32; i < 64; i++) {
        check32(cz[i], 0xaa);
    }
}

/* Y2_dcfetchbo_nt: dcfetch(Rs32+#0):nt -- a hint, must not fault or store */
static uint8_t df[32] __attribute__((aligned(8)));

static void test_dcfetchbo_nt(void)
{
    memset(df, 0x55, sizeof(df));
    asm volatile(
        "r2 = %1\n\t"
        ".word 0x9402e000\n\t" /* dcfetch(r2+#0):nt */
        : "+m"(df)
        : "r"(df)
        : "r2", "memory");
    for (int i = 0; i < 32; i++) {
        check32(df[i], 0x55);
    }
}

int main()
{
    test_storeri_pi_nt();
    test_storerb_pi_nt();
    test_pstorerit_pi_nt();
    test_pstorerif_pi_nt();
    test_dczeroa_nt();
    test_dcfetchbo_nt();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
