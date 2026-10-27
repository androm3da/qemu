/*
 * Hexagon HMX (Matrix eXtensions) Helper Functions
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * TCG helper implementations for HMX coprocessor instructions.
 */

#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "qemu/int128.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "accel/tcg/cpu-ldst.h"
#include "accel/tcg/cpu-loop.h"
#include "accel/tcg/probe.h"
#include "exec/target_page.h"
#include "exec/tlb-flags.h"
#include "hmx_state.h"

static inline const HmxConfig *hmx_cfg_from_env(CPUHexagonState *env)
{
    return &env_archcpu(env)->hmx_cfg;
}

/*
 * A window of guest memory that lies within one page (HMX operands are
 * 128 byte or 2KB aligned).  Ordinary RAM is accessed through a host
 * pointer, which avoids a TLB lookup per word.  Anything else (MMIO,
 * watchpoints) goes through the cpu_ld*()/cpu_st*() accessors, which also
 * deliver any fault.
 */
typedef struct HmxMem {
    CPUHexagonState *env;
    uint32_t base;
    uint8_t *host;
    uintptr_t ra;
} HmxMem;

/*
 * Map [base, base + len) for @access, raising any fault now.  The convert
 * and store helpers age the convert pipeline and clear the accumulator as
 * they go, so a restart after a fault taken part-way through would apply
 * that a second time; mapping the window first avoids that.
 */
static void hmx_mem_map(HmxMem *m, CPUHexagonState *env, uint32_t base,
                        uint32_t len, MMUAccessType access, uintptr_t ra)
{
    g_assert(len <= -(base | TARGET_PAGE_MASK));
    m->env = env;
    m->base = base;
    m->ra = ra;
    m->host = probe_access(env, base, len, access,
                           cpu_mmu_index(env_cpu(env), false), ra);
}

/* Copy @n bytes (a multiple of 4) at @off to @dst. */
static void hmx_mem_read(const HmxMem *m, uint32_t off, uint8_t *dst,
                         uint32_t n)
{
    if (m->host) {
        memcpy(dst, m->host + off, n);
        return;
    }
    for (uint32_t i = 0; i < n; i += 4) {
        stl_le_p(&dst[i], cpu_ldl_le_data_ra(m->env, m->base + off + i,
                                             m->ra));
    }
}

/*
 * M8_mxclracc - clear both FXP accumulator sets.
 */
void HELPER(hmx_clracc)(CPUHexagonState *env)
{
    HmxState *hmx = env->hmx_state;

    memset(&hmx->acc[0].fxp_primary, 0, sizeof(HmxAccFxp));
    memset(&hmx->acc[1].fxp_primary, 0, sizeof(HmxAccFxp));
}

/*
 * M8_mxaccshl - shift both FXP accumulator sets left by 16 bits. Removed
 * in v79 (see tag_rev_info.c.inc).
 */
void HELPER(hmx_accshl)(CPUHexagonState *env)
{
    HmxState *hmx = env->hmx_state;

    for (int set = 0; set < HMX_NUM_ACC_SETS; set++) {
        HmxAccFxp *acc = &hmx->acc[set].fxp_primary;
        for (int s = 0; s < HMX_SPATIAL_DIM_FXP; s++) {
            for (int o = 0; o < HMX_OUTPUT_CHANNELS; o++) {
                acc->data[s][o] = (int32_t)((uint32_t)acc->data[s][o] << 16);
            }
        }
    }
}

/*
 * M8_mxmem_bias / M8_mxmem2_bias - load HMX bias registers from memory.
 *
 * Bias set is (rs & 0x3).
 *
 * mxmem (is_mxmem2 == false) transfers only the low 32 bits of each of
 * mx_cols channels, packed contiguously.  mxmem2 additionally transfers
 * the high 32 bits, at a memory offset of HMX_BIAS_HIGH_WORD_OFFSET
 * bytes past the low half -- the HVX vector-length stride between the
 * two halves in the real bias-vector layout, not a function of mx_cols.
 */
void HELPER(hmx_bias_load)(CPUHexagonState *env, uint32_t rs,
                           uint32_t is_mxmem2)
{
    HmxState *hmx = env->hmx_state;
    const HmxConfig *hmx_cfg = hmx_cfg_from_env(env);
    uintptr_t ra = GETPC();
    uint32_t entry_bytes = is_mxmem2 ? HMX_BIAS_ENTRY_BYTES
                                     : HMX_OUTPUT_WORD_BYTES;
    uint32_t bias_bytes = hmx_cfg->mx_cols * entry_bytes;
    uint32_t align_mask = ~(bias_bytes - 1u);
    uint32_t addr = rs & align_mask;
    uint32_t set = rs & 0x3;

    /* Load everything first, so a fault leaves the bias set untouched. */
    uint64_t raw[HMX_OUTPUT_CHANNELS];

    if (is_mxmem2) {
        for (uint32_t i = 0; i < hmx_cfg->mx_cols; i++) {
            uint32_t lo = cpu_ldl_le_data_ra(
                env, addr + i * HMX_OUTPUT_WORD_BYTES, ra);
            uint32_t hi = cpu_ldl_le_data_ra(
                env, addr + HMX_BIAS_HIGH_WORD_OFFSET
                     + i * HMX_OUTPUT_WORD_BYTES, ra);
            raw[i] = ((uint64_t)hi << 32) | lo;
        }
    } else {
        for (uint32_t i = 0; i < hmx_cfg->mx_cols; i++) {
            raw[i] = cpu_ldl_le_data_ra(
                env, addr + i * HMX_OUTPUT_WORD_BYTES, ra);
        }
    }
    memcpy(hmx->bias_raw[set], raw, hmx_cfg->mx_cols * sizeof(raw[0]));
}

/*
 * M8_mxmem_st_bias / M8_mxmem2_st_bias - store HMX bias registers to
 * memory. See HELPER(hmx_bias_load) for the addressing/layout rationale.
 */
void HELPER(hmx_bias_store)(CPUHexagonState *env, uint32_t rs,
                            uint32_t is_mxmem2)
{
    HmxState *hmx = env->hmx_state;
    const HmxConfig *hmx_cfg = hmx_cfg_from_env(env);
    uintptr_t ra = GETPC();
    uint32_t entry_bytes = is_mxmem2 ? HMX_BIAS_ENTRY_BYTES
                                     : HMX_OUTPUT_WORD_BYTES;
    uint32_t bias_bytes = hmx_cfg->mx_cols * entry_bytes;
    uint32_t align_mask = ~(bias_bytes - 1u);
    uint32_t addr = rs & align_mask;
    uint32_t set = rs & 0x3;

    if (is_mxmem2) {
        for (uint32_t i = 0; i < hmx_cfg->mx_cols; i++) {
            uint64_t raw = hmx->bias_raw[set][i];
            cpu_stl_le_data_ra(env, addr + i * HMX_OUTPUT_WORD_BYTES,
                               (uint32_t)raw, ra);
            cpu_stl_le_data_ra(env, addr + HMX_BIAS_HIGH_WORD_OFFSET
                                    + i * HMX_OUTPUT_WORD_BYTES,
                               (uint32_t)(raw >> 32), ra);
        }
    } else {
        for (uint32_t i = 0; i < hmx_cfg->mx_cols; i++) {
            uint64_t raw = hmx->bias_raw[set][i];
            cpu_stl_le_data_ra(env, addr + i * HMX_OUTPUT_WORD_BYTES,
                               (uint32_t)raw, ra);
        }
    }
}

/*
 * Compute multi-tap convolution parameters from activation Rs/Rt.
 *
 * Called from HELPER(hmx_act_load) to pre-compute tile masks, filter
 * positions, channel ranges, and tap counts that the subsequent weight
 * multiply instruction (HELPER(hmx_matmul_fxp)) reads back out of
 * HmxState.
 */
static void hmx_compute_act_params(const HmxConfig *hmx_cfg,
                                    HmxState *hmx,
                                    uint32_t rs, uint32_t rt,
                                    int act_fmt, int act_mod, int act_type)
{
    int format_offset = (act_fmt == HMX_ACT_FMT_SM) ? 2 : 0;
    uint32_t ch_mask = (hmx_cfg->mx_cols - 1) << format_offset;
    int is_flt = (act_type == HMX_ACT_HF) || (act_type == HMX_ACT_F8);

    hmx->format_offset = format_offset;
    hmx->dY = rt & ~2047;  /* offset to second activation crouton */

    /*
     * Channel range and group convolution detection.
     *
     * When ch_start > ch_stop (raw), the hardware enters group
     * convolution mode.  The leading-ones count of
     * ((ch_stop-ch_start)^ch_start^ch_stop) determines the group size
     * (32 >> cl1).  The channel indices are then masked to within the
     * group, and the matmul iterates over all groups.
     */
    int raw_start = (rs & ch_mask) >> format_offset;
    int raw_stop = (rt & ch_mask) >> format_offset;

    hmx->group_conv = (raw_start > raw_stop) &&
                      (act_mod != HMX_ACT_NOBLK);

    if (hmx->group_conv) {
        /* Count leading ones on an 8-bit value to determine group size. */
        uint8_t cl1_arg = (uint8_t)(((raw_stop - raw_start) ^
                                      raw_start ^ raw_stop) << 2);
        int cl1 = 0;
        while (cl1_arg & 0x80) {
            cl1++;
            cl1_arg <<= 1;
        }

        hmx->group_size = 32 >> cl1;
        hmx->group_count = 32 / hmx->group_size;

        int grp_mask = 0x1F >> cl1;
        int group_ch_start, group_ch_stop;

        if (hmx->group_count == 16) {
            group_ch_start = (raw_start & ~1) & grp_mask;
            group_ch_stop = (raw_stop | 1) & grp_mask;
        } else if (hmx->group_count <= 8) {
            group_ch_start = (raw_start & ~3) & grp_mask;
            group_ch_stop = (raw_stop | 3) & grp_mask;
        } else {
            group_ch_start = raw_start & grp_mask;
            group_ch_stop = raw_stop & grp_mask;
        }

        hmx->ch_start = group_ch_start;
        hmx->ch_stop = group_ch_stop + 1;  /* exclusive */
    } else {
        hmx->group_size = 32;
        hmx->group_count = 1;
        hmx->ch_start = raw_start & ~3;
        /* exclusive, rounded up to group of 4 */
        hmx->ch_stop = (raw_stop | 3) + 1;
    }

    /* Tile masks from Rt spatial bits */
    hmx->tile_x_mask = hmx_get_spatial_mask(~rt, format_offset);
    hmx->tile_y_mask = hmx_get_spatial_mask(rt, format_offset);

    /*
     * FP mode: clear LSB of tile masks so spatial iteration steps by 2.
     * Each FP16 value occupies 2 crouton bytes (adjacent spatial
     * positions), so the spatial loop must skip odd positions.
     */
    if (is_flt) {
        hmx->tile_x_mask &= ~1;
        hmx->tile_y_mask &= ~1;
    }

    /* Tile increments (lowest set bit of each mask) */
    hmx->tile_x_inc = hmx_get_masked_inc(hmx->tile_x_mask);
    hmx->tile_y_inc = hmx_get_masked_inc(hmx->tile_y_mask);

    /* Filter positions from Rs */
    hmx->fx = rs & hmx->tile_x_mask;
    hmx->fy = rs & hmx->tile_y_mask;

    /* Y-dimension tap parameters (determined by activation block type) */
    hmx->y_start = 0;
    hmx->y_stop = 0;
    hmx->y_dilate = 0;
    hmx->blocks = 1;

    switch (act_mod) {
    case HMX_ACT_BLK:   /* BLOCK: normal multi-tap */
        hmx->y_stop = hmx->fy;
        hmx->y_dilate = 0;
        break;
    case HMX_ACT_NOBLK:  /* DEEP: multi-block mode */
    {
        uint32_t dY = rt & ~2047;
        hmx->blocks = (dY >> 11) + 1;
        hmx->y_stop = 0;
        break;
    }
    case HMX_ACT_BLK_U:  /* ABOVE: taps from fy to end */
        hmx->y_start = hmx->fy;
        hmx->y_stop = hmx->tile_y_mask;
        break;
    case HMX_ACT_BLK_S:  /* SINGLE: one y tap at fy */
        hmx->y_start = hmx->fy;
        hmx->y_stop = hmx->fy;
        break;
    case HMX_ACT_BLK_D:  /* DILATE: with dilation */
        hmx->y_stop = hmx->fy;
        hmx->y_dilate = 1;
        break;
    }

    /*
     * Guard: if ch_start >= ch_stop after processing, no channels to
     * process.  blocks=0.
     */
    if (hmx->ch_start >= hmx->ch_stop && hmx->blocks == 1) {
        hmx->blocks = 0;
    }
}

/* Expand one FP8 value to FP16; 0x80 is the FP8 NaN. */
static inline uint16_t hmx_f8_to_f16(uint8_t f8)
{
    if (f8 == 0x80) {
        return 0xFE00;
    }
    return ((f8 & 0x80) << 8) | ((f8 & 0x7F) << 7);
}

/*
 * Load the 2KB activation crouton in @mem into act_buffer at @buf_off.
 * FP8 activations are expanded in place to FP16, at the even spatial
 * positions and from the byte of each pair that Rs[0] selected.
 */
static void hmx_load_act_crouton(HmxState *hmx, const HmxMem *mem,
                                 int buf_off, int act_type)
{
    uint8_t *crouton = &hmx->act_buffer[buf_off];

    hmx_mem_read(mem, 0, crouton, HMX_ACT_CROUTON_SIZE);

    if (act_type != HMX_ACT_F8) {
        return;
    }
    for (int s = 0; s < HMX_SPATIAL_DIM_FP; s++) {
        for (int c = 0; c < HMX_INPUT_CHANNELS; c++) {
            int off = hmx_act_offset_sm(s * 2, c);
            uint8_t f8 = lduw_le_p(&crouton[off]) >> (hmx->is_f8_odd * 8);

            stw_le_p(&crouton[off], hmx_f8_to_f16(f8));
        }
    }
}

/*
 * M8_mxmem*_act_* - load an activation crouton (or two, for multi-tap Y
 * convolution) into HmxState.act_buffer and latch the parameters the
 * subsequent weight-multiply instruction needs.
 *
 * F8 activations are expanded to FP16 in place after loading: a raw bit
 * shift with no exponent rebias (HMX F8 is designed so this is exact),
 * so the matmul's FP path can always read act_buffer as uint16_t[]
 * regardless of whether the source was HF or F8.
 */
void HELPER(hmx_act_load)(CPUHexagonState *env, uint32_t rs, uint32_t rt,
                          uint32_t params)
{
    HmxState *hmx = env->hmx_state;
    int act_type = HMX_UNPACK_ACT_TYPE(params);
    int act_fmt = HMX_UNPACK_ACT_FMT(params);
    uintptr_t ra = GETPC();

    /* 2KB-aligned base address */
    uint32_t base_addr = rs & 0xFFFFF800;

    /* Latch parameters for weight multiply */
    hmx->act_rs = rs;
    hmx->act_type = act_type;

    /* 256 MAC cycles for FP, 512 for FXP. */
    {
        int is_flt_act = (act_type == HMX_ACT_HF) || (act_type == HMX_ACT_F8);
        hmx->mac_cycle_limit = is_flt_act ? 256 : 512;
    }

    /*
     * FP8 odd-byte select: Rs[0] picks the even (0) or odd (1) byte of
     * each 16-bit activation word for F8 expansion.
     */
    hmx->is_f8_odd = (act_type == HMX_ACT_F8) ? (rs & 1) : 0;

    /* Compute multi-tap convolution parameters */
    int act_mod = HMX_UNPACK_ACT_MOD(params);
    hmx_compute_act_params(hmx_cfg_from_env(env), hmx, rs, rt, act_fmt,
                           act_mod, act_type);

    /*
     * Load second activation crouton for multi-tap Y convolution: when
     * (blocks == 1) and the y-tap range isn't degenerate.
     */
    int need_second = (hmx->blocks == 1) &&
        ((hmx->y_stop != hmx->y_start) || (hmx->y_start != 0));

    HmxMem mem;

    g_assert(act_type != HMX_ACT_F8 || hmx_cfg_from_env(env)->mx_fp8_en);
    hmx_mem_map(&mem, env, base_addr, HMX_ACT_CROUTON_SIZE, MMU_DATA_LOAD, ra);
    hmx_load_act_crouton(hmx, &mem, 0, act_type);
    if (need_second) {
        hmx_mem_map(&mem, env, base_addr + hmx->dY, HMX_ACT_CROUTON_SIZE,
                    MMU_DATA_LOAD, ra);
        hmx_load_act_crouton(hmx, &mem, HMX_ACT_CROUTON_SIZE, act_type);
    }
}

