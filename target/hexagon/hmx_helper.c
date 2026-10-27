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

static inline uint32_t hmx_mem_ld32(const HmxMem *m, uint32_t off)
{
    if (m->host) {
        return ldl_le_p(m->host + off);
    }
    return cpu_ldl_le_data_ra(m->env, m->base + off, m->ra);
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

/* Room for the largest accumulator array: 32 x 32 XFP cells of 16 bytes. */
#define HMX_UNDO_MAX_BYTES (16 * 1024)

/*
 * Undo log for a multiply.  A multiply accumulates as it goes and reads
 * its weights and activation croutons lazily, so a fault taken part-way
 * through (a TLB miss the guest handles before the instruction restarts)
 * would make the restart add the products already accumulated a second
 * time.  The multiply saves the accumulators it can change up front, and
 * hmx_mem_map_load() puts them back before it raises the fault.  Only
 * accumulator arrays are saved, since everything else a multiply changes is
 * rebuilt when it runs again.
 */
typedef struct HmxUndo {
    HmxState *hmx;
    int32_t mac_cycle_limit;
    void *region[2];
    size_t len;
    uint8_t saved[2][HMX_UNDO_MAX_BYTES];
} HmxUndo;

/* Save @len bytes at @r0, and at @r1 unless it is NULL (the deep modifier). */
static void hmx_undo_begin(HmxUndo *undo, HmxState *hmx, void *r0, void *r1,
                           size_t len)
{
    g_assert(len <= sizeof(undo->saved[0]));
    undo->hmx = hmx;
    undo->mac_cycle_limit = hmx->mac_cycle_limit;
    undo->region[0] = r0;
    undo->region[1] = r1;
    undo->len = len;
    memcpy(undo->saved[0], r0, len);
    if (r1) {
        memcpy(undo->saved[1], r1, len);
    }
}

/*
 * Map a window that a multiply is about to read.  If the read would fault,
 * restore the accumulators and raise the fault, so that the restart sees
 * the state the instruction started with.
 */
static void hmx_mem_map_load(HmxMem *m, CPUHexagonState *env, HmxUndo *undo,
                             uint32_t base, uint32_t len, uintptr_t ra)
{
    int mmu_idx = cpu_mmu_index(env_cpu(env), false);
    void *host;
    int flags;

    g_assert(len <= -(base | TARGET_PAGE_MASK));
    flags = probe_access_flags(env, base, len, MMU_DATA_LOAD, mmu_idx, true,
                               &host, ra);
    if (flags & TLB_INVALID_MASK) {
        undo->hmx->mac_cycle_limit = undo->mac_cycle_limit;
        memcpy(undo->region[0], undo->saved[0], undo->len);
        if (undo->region[1]) {
            memcpy(undo->region[1], undo->saved[1], undo->len);
        }
        probe_access(env, base, len, MMU_DATA_LOAD, mmu_idx, ra);
        /* The page became accessible meanwhile: run the instruction again. */
        cpu_loop_exit_restore(env_cpu(env), ra);
    }

    m->env = env;
    m->base = base;
    m->ra = ra;
    m->host = flags ? NULL : host;
}

/*
 * Take one MAC cycle from the budget that an activation load grants to the
 * multiplies that follow it.  Returns false once it is spent, and the
 * rest of that multiply is dropped.
 */
static inline bool hmx_take_mac_cycle(HmxState *hmx)
{
    if (hmx->mac_cycle_limit <= 0) {
        hmx->mac_cycle_limit = 0;
        return false;
    }
    hmx->mac_cycle_limit--;
    return true;
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

/*
 * Weight memory layout in VTCM (per 128B vector):
 *
 * Each 128B vector contains 32 output channels x 4 bytes.  Word at
 * offset (och * 4) contains weights for output channel 'och'.  Within
 * each 4-byte word, bytes map to input channels:
 *   byte[0] = input_ch_in_group 0
 *   byte[1] = input_ch_in_group 1
 *   byte[2] = input_ch_in_group 2
 *   byte[3] = input_ch_in_group 3
 *
 * For byte weights: 4 input channels per vector.
 * For nibble weights: 8 input channels per vector (lo/hi nibbles).
 * For crumb weights: 16 input channels per vector (4 crumbs/byte).
 * For bit weights: 32 input channels per vector (8 bits/byte).
 */

/*
 * Bulk-load one weight vector as little-endian int32 words.
 *
 * Zero-fill storage past mx_cols so fixed-size downstream loops consume
 * zero contributions for inactive output channels.
 */
static inline void hmx_preload_weight_vec(const HmxConfig *hmx_cfg,
                                           CPUHexagonState *env,
                                           uint32_t wei_base,
                                           int vec_idx,
                                           uint32_t *wei_words,
                                           HmxUndo *undo, uintptr_t ra)
{
    uint32_t base =
        wei_base + vec_idx * (hmx_cfg->mx_cols * HMX_OUTPUT_WORD_BYTES);

    HmxMem mem;

    hmx_mem_map_load(&mem, env, undo, base,
                     hmx_cfg->mx_cols * HMX_OUTPUT_WORD_BYTES, ra);
    for (int i = 0; i < hmx_cfg->mx_cols; i++) {
        wei_words[i] = hmx_mem_ld32(&mem, i * HMX_OUTPUT_WORD_BYTES);
    }
    for (int i = hmx_cfg->mx_cols; i < HMX_OUTPUT_CHANNELS; i++) {
        wei_words[i] = 0;
    }
}

/* Byte: 4 stream indices per vector */
static inline void hmx_extract_weights_byte(
    const uint32_t *wei_words, int sub_idx, int8_t *weights)
{
    int shift = sub_idx * 8;
    for (int o = 0; o < HMX_OUTPUT_CHANNELS; o++) {
        weights[o] = (int8_t)((wei_words[o] >> shift) & 0xFF);
    }
}

/*
 * Sign-magnitude: same packing as byte, different interpretation.
 * Positive (bit7=0): result = magnitude.  Negative (bit7=1):
 * result = 0x7F ^ in = ~magnitude = -(magnitude+1), mapping
 * [0x80..0xFF] to [-1..-128] with no negative zero.
 */
static inline void hmx_extract_weights_sm(
    const uint32_t *wei_words, int sub_idx, int8_t *weights)
{
    int shift = sub_idx * 8;
    for (int o = 0; o < HMX_OUTPUT_CHANNELS; o++) {
        int8_t v = (int8_t)((wei_words[o] >> shift) & 0xFF);
        weights[o] = (((v >> 7) & 0x7F) ^ v);
    }
}

/*
 * Nibble: 8 stream indices per vector.  Extract 4-bit value and sign
 * extend: values 8-15 become -8..-1.
 */
static inline void hmx_extract_weights_nibble(
    const uint32_t *wei_words, int sub_idx, int8_t *weights)
{
    int byte_in_word = sub_idx % 4;
    int nibble_sel = sub_idx / 4;
    int shift = byte_in_word * 8 + nibble_sel * 4;
    for (int o = 0; o < HMX_OUTPUT_CHANNELS; o++) {
        weights[o] = (int8_t)sextract32(wei_words[o] >> shift, 0, 4);
    }
}

/*
 * Crumb: 16 stream indices per vector.  Extract 2-bit value and sign
 * extend: {0,1,2,3} -> {0,1,-2,-1}.
 */
static inline void hmx_extract_weights_crumb(
    const uint32_t *wei_words, int sub_idx, int8_t *weights)
{
    int byte_in_word = sub_idx % 4;
    int crumb_sel = sub_idx / 4;
    int shift = byte_in_word * 8 + crumb_sel * 2;
    for (int o = 0; o < HMX_OUTPUT_CHANNELS; o++) {
        weights[o] = (int8_t)sextract32(wei_words[o] >> shift, 0, 2);
    }
}

/* Signed crumb: lookup table {0,1,2,3} -> {2,1,-2,-1} */
static inline void hmx_extract_weights_signed_crumb(
    const uint32_t *wei_words, int sub_idx, int8_t *weights)
{
    static const int8_t sc_map[4] = { 2, 1, -2, -1 };
    int byte_in_word = sub_idx % 4;
    int crumb_sel = sub_idx / 4;
    int shift = byte_in_word * 8 + crumb_sel * 2;
    for (int o = 0; o < HMX_OUTPUT_CHANNELS; o++) {
        weights[o] = sc_map[(wei_words[o] >> shift) & 0x3];
    }
}

/* Bit (unsigned 1-bit): 32 stream indices per vector */
static inline void hmx_extract_weights_bit(
    const uint32_t *wei_words, int sub_idx, int8_t *weights)
{
    int byte_in_word = sub_idx % 4;
    int bit_sel = sub_idx / 4;
    int shift = byte_in_word * 8 + bit_sel;
    for (int o = 0; o < HMX_OUTPUT_CHANNELS; o++) {
        weights[o] = (wei_words[o] >> shift) & 0x1;
    }
}

/* Signed bit: {0,1} -> {+1,-1} */
static inline void hmx_extract_weights_signed_bit(
    const uint32_t *wei_words, int sub_idx, int8_t *weights)
{
    int byte_in_word = sub_idx % 4;
    int bit_sel = sub_idx / 4;
    int shift = byte_in_word * 8 + bit_sel;
    for (int o = 0; o < HMX_OUTPUT_CHANNELS; o++) {
        weights[o] = ((wei_words[o] >> shift) & 0x1) ? -1 : 1;
    }
}

/*
 * Byte weights are always signed int8_t at every revision; treating
 * them as unsigned on v71+ gives all-0xff output for filters with bytes
 * > 0x7f.
 */
static inline void hmx_mac32(int32_t *acc_row, const int8_t *weights,
                              int32_t act_x_negate)
{
    for (int o = 0; o < HMX_OUTPUT_CHANNELS; o++) {
        acc_row[o] += act_x_negate * (int32_t)weights[o];
    }
}

/* Range-limited MAC for group convolution. */
static inline void hmx_mac_group(int32_t *acc_row, const int8_t *weights,
                                  int32_t act_x_negate,
                                  int grp_start, int grp_end)
{
    for (int o = grp_start; o < grp_end; o++) {
        acc_row[o] += act_x_negate * (int32_t)weights[o];
    }
}

/* Weight extraction function pointer type */
typedef void (*hmx_extract_fn)(const uint32_t *, int, int8_t *);

/* Stream indices packed per 128-byte weight vector, indexed by wei_type */
static const int hmx_channels_per_vec[8] = {
    4, 4, 8, 16, 16, 32, 32, 8
};

/* Extraction function dispatch table, indexed by wei_type */
static const hmx_extract_fn hmx_extract_table[8] = {
    hmx_extract_weights_byte,          /* HMX_WEI_B    */
    hmx_extract_weights_sm,            /* HMX_WEI_SM   */
    hmx_extract_weights_nibble,        /* HMX_WEI_N    */
    hmx_extract_weights_crumb,         /* HMX_WEI_C    */
    hmx_extract_weights_signed_crumb,  /* HMX_WEI_SC   */
    hmx_extract_weights_bit,           /* HMX_WEI_B1   */
    hmx_extract_weights_signed_bit,    /* HMX_WEI_SB1  */
    hmx_extract_weights_nibble,        /* HMX_WEI_N_2X */
};

/*
 * Reload activation crouton for a given block index.  Only used when
 * blocks > 1 (deep activation mode with more than one 2KB crouton).
 */
static void hmx_reload_act_crouton(CPUHexagonState *env, HmxState *hmx,
                                   uint32_t base_addr, int crouton_idx,
                                   int act_type, HmxUndo *undo, uintptr_t ra)
{
    HmxMem mem;

    hmx_mem_map_load(&mem, env, undo,
                     base_addr + crouton_idx * HMX_ACT_CROUTON_SIZE,
                     HMX_ACT_CROUTON_SIZE, ra);
    hmx_load_act_crouton(hmx, &mem, 0, act_type);
}

/*
 * The tap and spatial parameters of a multiply: what the activation load
 * left in HmxState, as modified by the weight modifier.  Shared by the FXP
 * and FP multiplies.
 */
typedef struct HmxTaps {
    int deep;               /* DP: accumulate into both accumulator sets */
    int drop;               /* DR: drop positions that carry past the tile */
    int format_offset;
    int format_mask;
    int32_t tile_x_mask;
    int32_t tile_y_mask;
    int x_tap_count;
    int y_tap_count;
    int x_count;
    int y_count;
    int x_tap_array[HMX_MAX_TAP_ARRAY];
    int y_tap_array[HMX_MAX_TAP_ARRAY];
    int intra_x_array[HMX_MAX_TAP_ARRAY];
    int intra_y_array[HMX_MAX_TAP_ARRAY];
} HmxTaps;

static void hmx_compute_taps(const HmxState *hmx, int wei_mod, HmxTaps *t)
{
    uint32_t x_start = 0;
    uint32_t x_stop = 0;
    int x_dilate = 0;

    t->deep = 0;
    t->drop = 0;

    switch (wei_mod) {
    case HMX_MOD_NORMAL:
        x_stop = hmx->fx;
        break;
    case HMX_MOD_SINGLE:
        x_start = hmx->fx;
        x_stop = hmx->fx;
        break;
    case HMX_MOD_DR:
        x_stop = hmx->fx;
        t->drop = 1;
        break;
    case HMX_MOD_DP:
        t->deep = 1;
        x_stop = 0;
        break;
    case HMX_MOD_ABOVE:
        x_start = hmx->fx;
        x_stop = hmx->tile_x_mask;
        break;
    case HMX_MOD_DI:
        x_stop = hmx->fx;
        x_dilate = 1;
        break;
    }

    t->format_offset = hmx->format_offset;
    t->tile_x_mask = hmx->tile_x_mask;
    t->tile_y_mask = hmx->tile_y_mask;
    t->format_mask = (1 << t->format_offset) - 1;

    int32_t tile_x_mask_msb = (int32_t)(t->tile_x_mask | (1u << 31));
    int32_t tile_y_mask_msb = (int32_t)(t->tile_y_mask | (1u << 31));

    t->x_tap_count = hmx_compute_indices(
        x_start, x_stop, hmx->tile_x_inc, tile_x_mask_msb,
        x_dilate, t->x_tap_array, HMX_MAX_TAP_ARRAY);
    t->y_tap_count = hmx_compute_indices(
        hmx->y_start, hmx->y_stop, hmx->tile_y_inc, tile_y_mask_msb,
        hmx->y_dilate, t->y_tap_array, HMX_MAX_TAP_ARRAY);
    t->x_count = hmx_compute_indices(
        0, 0x7FFFFFFF, hmx->tile_x_inc, tile_x_mask_msb,
        0, t->intra_x_array, HMX_MAX_TAP_ARRAY);
    t->y_count = hmx_compute_indices(
        0, 0x7FFFFFFF, hmx->tile_y_inc, tile_y_mask_msb,
        0, t->intra_y_array, HMX_MAX_TAP_ARRAY);
}

/*
 * Compute per-crouton channel range for deep activation mode.
 *
 *   - First crouton (idx=0): [ch_start, 32)
 *   - Middle croutons:       [0, 32)
 *   - Last crouton (idx=blocks-1): [0, ch_stop)
 */
static void hmx_crouton_ch_range(int crouton_idx, int num_croutons,
                                 int orig_ch_start, int orig_ch_stop,
                                 int *out_start, int *out_stop)
{
    if (num_croutons <= 1) {
        *out_start = orig_ch_start;
        *out_stop = orig_ch_stop;
        return;
    }
    if (crouton_idx == 0) {
        *out_start = orig_ch_start;
        *out_stop = 32;
    } else if (crouton_idx == num_croutons - 1) {
        *out_start = 0;
        *out_stop = orig_ch_stop;
    } else {
        *out_start = 0;
        *out_stop = 32;
    }
}

static void hmx_fxp_spatial_mac(
    HmxState *hmx, const int8_t *weights,
    int y_count, int x_count,
    const int *intra_y_array, const int *intra_x_array,
    int y_tap, int x_tap,
    int32_t tile_x_mask, int32_t tile_y_mask,
    int ch_addr, int drop, int deep,
    int current_acc, int format_mask, int negate,
    int grp_count, int out_start, int out_end)
{
    for (int iy = 0; iy < y_count; iy++) {
        int intra_y = intra_y_array[iy];
        int32_t y_ovf = 0;
        int32_t act_y = hmx_inc_with_spatial_mask_ovf(
            y_tap, intra_y, tile_y_mask, &y_ovf);
        act_y = (act_y & 0x7FFFFFFF) + y_ovf * 0x800;

        for (int ix = 0; ix < x_count; ix++) {
            int intra_x = intra_x_array[ix];
            int32_t x_ovf = 0;
            int32_t output_idx = hmx_inc_with_spatial_mask_ovf(
                x_tap, intra_x, tile_x_mask, &x_ovf);
            int acc_sel = x_ovf
                ? (current_acc ^ 1) & 1
                : current_acc & 1;

            output_idx |= intra_y;

            int spatial = ((output_idx >> 5) & ~format_mask)
                        | (output_idx & format_mask);

            if (x_ovf && (drop || deep)) {
                continue;
            }

            if (spatial < 0 || spatial >= HMX_SPATIAL_DIM_FXP) {
                continue;
            }

            int32_t act_idx = (act_y + intra_x + ch_addr) & 0xFFF;
            uint8_t act_val = hmx->act_buffer[act_idx];

            HmxAccFxp *acc = &hmx->acc[acc_sel].fxp_primary;
            int32_t act_x_neg = (int32_t)act_val * negate;

            if (grp_count > 1) {
                hmx_mac_group(acc->data[spatial], weights, act_x_neg,
                              out_start, out_end);
            } else {
                hmx_mac32(acc->data[spatial], weights, act_x_neg);
            }
        }
    }
}

/*
 * A weight vector at wei_base + vec_idx * (mx_cols * 4) is valid only
 * while that byte offset stays within the Rt range and the MAC cycle
 * budget.  There is no VTCM device to bound-check against (see
 * hw/hexagon/hexagon.h's cfgtable.vtcm_size_kb for where its size would
 * come from), so VTCM is treated as unbounded.  The N_2X type packs two
 * output channels per word, so it needs one less weight_count.
 */
static int hmx_fxp_max_valid_vec(CPUHexagonState *env, uint32_t rs,
                                 uint32_t rt, int wei_type)
{
    const HmxConfig *hmx_cfg = hmx_cfg_from_env(env);
    const uint32_t mx_cols = hmx_cfg->mx_cols;
    const uint32_t mx_rows = hmx_cfg->mx_rows;
    const uint32_t last_wgt_mask = mx_cols * HMX_OUTPUT_WORD_BYTES - 1u;

    int cpv = hmx_channels_per_vec[wei_type];
    int output_ch_scale = (wei_type == HMX_WEI_N_2X) ? 2 : 1;
    int weight_count = ctz32((uint32_t)cpv) - 2; /* log2(cpv/4) */
    if (output_ch_scale == 2) {
        weight_count -= 1;
    }
    if (weight_count < 0) {
        weight_count = 0;
    }

    /* Weight decompression buffer limit, as a byte offset. */
    uint64_t limit = (uint64_t)mx_rows * mx_cols;
    limit = limit * mx_cols;
    limit = limit * 9 / 8;
    int wgtc_mode = (rs >> 4) & 1;
    if (wgtc_mode == 0) {
        limit >>= weight_count;
    }

    /* Rt range, rounded up to the end of the vector it lands in. */
    uint64_t max_wgt_off = (uint64_t)rt | last_wgt_mask;

    if (max_wgt_off >= limit) {
        max_wgt_off = limit - 1;
    }

    /* 512 FXP MAC cycles, not applied for compressed weights. */
    uint32_t mac_cycle_limit = 512;
    uint32_t fxp_max_wgt = HMX_FXP_WEIGHTS_PER_WORD * mx_cols;
    uint64_t mac_off =
        ((uint64_t)mac_cycle_limit * fxp_max_wgt) >> weight_count;
    if (mac_off >= 1) {
        mac_off -= 1;
    }
    if (wgtc_mode != 1 && mac_off < max_wgt_off) {
        max_wgt_off = mac_off;
    }

    return (int)(max_wgt_off / (mx_cols * HMX_OUTPUT_WORD_BYTES));
}

/*
 * M8_mxmem_wei_* - load one weight stream and run the FXP matrix
 * multiply against the activation crouton latched by the preceding
 * HELPER(hmx_act_load) call.
 *
 * Loop order: crouton -> y_tap -> deep_blk -> x_tap -> channel ->
 * (preload+extract) -> spatial_y -> spatial_x -> MAC32.  Weights are
 * preloaded once per 128B vector and extracted once per stream index;
 * the same extracted weights are then reused across all spatial
 * positions.
 */
void HELPER(hmx_matmul_fxp)(CPUHexagonState *env, uint32_t rs, uint32_t rt,
                            uint32_t params)
{
    HmxState *hmx = env->hmx_state;
    int wei_type = HMX_UNPACK_WEI_TYPE(params);
    int wei_mod = HMX_UNPACK_MOD(params);
    int current_acc = hmx->current_acc_set;
    uintptr_t ra = GETPC();
    const HmxConfig *hmx_cfg = hmx_cfg_from_env(env);

    /* Align to one weight vector. */
    uint32_t wei_base = rs & ~(hmx_cfg->mx_cols * HMX_OUTPUT_WORD_BYTES - 1);
    int max_valid_vec = hmx_fxp_max_valid_vec(env, rs, rt, wei_type);
    /* Rs[5] (weight negate) only applies to the FP matmul, not FXP. */
    int negate = 1;

    /* The deep modifier accumulates into the other set as well. */
    HmxUndo undo;
    hmx_undo_begin(&undo, hmx, &hmx->acc[current_acc].fxp_primary,
                   wei_mod == HMX_MOD_DP
                       ? &hmx->acc[current_acc ^ 1].fxp_primary : NULL,
                   sizeof(HmxAccFxp));

    /* Tap parameters, determined by the activation load and the modifier. */
    HmxTaps taps;

    hmx_compute_taps(hmx, wei_mod, &taps);
    int format_offset = taps.format_offset;

    int ch_start = hmx->ch_start;
    int ch_stop = hmx->ch_stop;

    /*
     * Sub-byte weight types iterate over all 32 input channels
     * regardless of the activation instruction's channel range.
     */
    int cpv = hmx_channels_per_vec[wei_type];
    if (cpv > 4) {
        ch_start = 0;
        ch_stop = 32;
    }

    int wgt_stream_idx = 0;
    hmx_extract_fn extract = hmx_extract_table[wei_type];
    uint32_t wei_words[HMX_OUTPUT_CHANNELS];
    int prev_vec_idx = -1;

    /*
     * Deep mode processes twice the weight kernels: the first set of
     * 32 channels accumulates to the current accumulator set, the second
     * to the other one.
     */
    int num_deep_blk = taps.deep ? 2 : 1;

    int num_croutons = hmx->blocks;
    uint32_t act_base = hmx->act_rs & 0xFFFFF800;

    for (int crouton_idx = 0; crouton_idx < num_croutons; crouton_idx++) {
        if (num_croutons > 1) {
            hmx_reload_act_crouton(env, hmx, act_base, crouton_idx,
                                   hmx->act_type, &undo, ra);
        }

        int crouton_ch_start, crouton_ch_stop;
        hmx_crouton_ch_range(crouton_idx, num_croutons, ch_start, ch_stop,
                             &crouton_ch_start, &crouton_ch_stop);

        for (int ytd = 0; ytd < taps.y_tap_count; ytd++) {
            int y_tap = taps.y_tap_array[ytd];

            for (int deep_blk = 0; deep_blk < num_deep_blk; deep_blk++) {
                for (int xtd = 0; xtd < taps.x_tap_count; xtd++) {
                    int x_tap = taps.x_tap_array[xtd];

                    for (int ch = crouton_ch_start; ch < crouton_ch_stop;
                         ch++) {
                        /*
                         * One MAC cycle per cpv channels, i.e. per
                         * weight vector consumed.  An exhausted budget
                         * drops the rest of the multiply.
                         */
                        if (((ch - crouton_ch_start) % cpv) == 0 &&
                            !hmx_take_mac_cycle(hmx)) {
                            goto mac_budget_exhausted;
                        }

                        int saved_wgt = wgt_stream_idx;
                        int grp_count = hmx->group_count;
                        int grp_size = hmx->group_size;

                        for (int grp = 0; grp < grp_count; grp++) {
                            wgt_stream_idx = saved_wgt;

                            int ch_in = ch + grp * grp_size;
                            int ch_addr = ch_in << format_offset;
                            int out_start = grp * grp_size;
                            int out_end = out_start + grp_size;

                            int vec_idx = wgt_stream_idx / cpv;
                            if (vec_idx > max_valid_vec) {
                                wgt_stream_idx++;
                                continue;
                            }

                            if (vec_idx != prev_vec_idx) {
                                hmx_preload_weight_vec(
                                    hmx_cfg, env, wei_base, vec_idx,
                                    wei_words, &undo, ra);
                                prev_vec_idx = vec_idx;
                            }

                            int8_t weights[HMX_OUTPUT_CHANNELS];
                            int sub_idx = wgt_stream_idx % cpv;
                            extract(wei_words, sub_idx, weights);

                            hmx_fxp_spatial_mac(
                                hmx, weights, taps.y_count, taps.x_count,
                                taps.intra_y_array, taps.intra_x_array,
                                y_tap, x_tap,
                                taps.tile_x_mask, taps.tile_y_mask,
                                ch_addr, taps.drop, taps.deep, current_acc,
                                taps.format_mask, negate, grp_count,
                                out_start, out_end);

                            wgt_stream_idx++;
                        }
                    }
                }
                if (taps.deep) {
                    current_acc = (current_acc ^ 1) & 1;
                }
            }
        }
    }
mac_budget_exhausted:
    return;
}

