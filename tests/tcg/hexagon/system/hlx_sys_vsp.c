/*
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * Test VSP (Vector Stack Pointer) spills to VTCM
 *
 * Points VSP at a VTCM area and reads it back.  Then spills four L
 * registers to the slots below VSP, as a compiler does when it runs out of
 * L registers, and verifies that reloading them from VTCM produces correct
 * results.
 */

#include <stdint.h>
#include <stdio.h>

#include <hexagon_standalone.h>
#include "../hlx_asm.h"

#define HLX_HALF_ELEMS  1024
#define HLX_VEC_BYTES   2048
#define HLX_VSP_SIZE    (16 * HLX_VEC_BYTES)

static void *vtcm_base_ptr;
static size_t vtcm_offset;

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

    uint64_t pa = (uint64_t)base;
    add_translation_fixed(1, vtcm_base_ptr, (void *)pa, 6, 0);
    add_translation_fixed(2, (char *)vtcm_base_ptr + 1024 * 1024,
                          (void *)(pa + 1024 * 1024), 6, 0);
}

static inline void *vtcm_alloc(size_t bytes)
{
    vtcm_offset = (vtcm_offset + (HLX_VEC_BYTES - 1))
                  & ~(size_t)(HLX_VEC_BYTES - 1);
    void *ptr = (char *)vtcm_base_ptr + vtcm_offset;
    vtcm_offset += bytes;
    return ptr;
}

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

static inline uint32_t read_hlx_vsp(void)
{
    uint32_t vsp;

    asm volatile(".word 0x6a1dc000\n" /* r0 = vsp */
                 "%[vsp] = r0\n"
                 : [vsp] "=r"(vsp) : : "r0");
    return vsp;
}

static inline void write_hlx_vsp(uint32_t vsp)
{
    asm volatile("r0 = %[vsp]\n"
                 ".word 0x6220c01d\n" /* vsp = r0 */
                 : : [vsp] "r"(vsp) : "r0");
}

int main(void)
{
    init_vtcm();
    enable_hlx();
    hlx_stage = vtcm_alloc(HLX_ASM_STAGE_BYTES);

    uint16_t *buf0 = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    uint16_t *buf1 = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    uint16_t *buf2 = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    uint16_t *buf3 = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    uint16_t *out  = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));

    /* VSP grows downward, so point it to the end of the area */
    uint8_t *vsp_area = vtcm_alloc(HLX_VSP_SIZE);
    uint32_t vsp_top = (uint32_t)(uintptr_t)vsp_area + HLX_VSP_SIZE;

    write_hlx_vsp(vsp_top);
    if (read_hlx_vsp() != vsp_top) {
        puts("FAIL");
        return 1;
    }

    const uint16_t pats[] = { 100, 200, 300, 400 };

    /* Store 4 different patterns */
    Q6_mem_Ku16_new_AL(buf0, Q6_Lh_equals_Rh(pats[0]));
    Q6_mem_Ku16_new_AL(buf1, Q6_Lh_equals_Rh(pats[1]));
    Q6_mem_Ku16_new_AL(buf2, Q6_Lh_equals_Rh(pats[2]));
    Q6_mem_Ku16_new_AL(buf3, Q6_Lh_equals_Rh(pats[3]));

    /* Load all 4 back and spill them to the slots below VSP */
    uint8_t *slot = (uint8_t *)(uintptr_t)read_hlx_vsp();
    Q6_mem_Ku16_new_AL(slot - 1 * HLX_VEC_BYTES, Q6_L_tmp_mem_A(buf0));
    Q6_mem_Ku16_new_AL(slot - 2 * HLX_VEC_BYTES, Q6_L_tmp_mem_A(buf1));
    Q6_mem_Ku16_new_AL(slot - 3 * HLX_VEC_BYTES, Q6_L_tmp_mem_A(buf2));
    Q6_mem_Ku16_new_AL(slot - 4 * HLX_VEC_BYTES, Q6_L_tmp_mem_A(buf3));

    /* Reload the spilled values and sum them */
    HLX_Vector l0 = Q6_L_tmp_mem_A(slot - 1 * HLX_VEC_BYTES);
    HLX_Vector l1 = Q6_L_tmp_mem_A(slot - 2 * HLX_VEC_BYTES);
    HLX_Vector l2 = Q6_L_tmp_mem_A(slot - 3 * HLX_VEC_BYTES);
    HLX_Vector l3 = Q6_L_tmp_mem_A(slot - 4 * HLX_VEC_BYTES);

    HLX_Vector sum01 = Q6_Lh_add_LhLh(l0, l1);
    HLX_Vector sum23 = Q6_Lh_add_LhLh(l2, l3);
    HLX_Vector total = Q6_Lh_add_LhLh(sum01, sum23);
    Q6_mem_Ku16_new_AL(out, total);

    uint16_t expected = pats[0] + pats[1] + pats[2] + pats[3];
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        if (out[i] != expected) {
            puts("FAIL");
            return 1;
        }
    }

    puts("PASS");
    return 0;
}
