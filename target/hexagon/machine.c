/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "cpu.h"
#include "cpu_helper.h"

static int hexagon_cpu_post_load(void *opaque, int version_id)
{
    HexagonCPU *cpu = opaque;
    CPUHexagonState *env = &cpu->env;

    hexagon_hvx_select_context(env, env->t_sreg[HEX_SREG_SSR]);

    return 0;
}

/*
 * HMX state.  The MAC product cache (fp_mac_cache, fp_mac_valid) only
 * lives within one rate-8 batch of one instruction, so it is not migrated.
 */
static const VMStateDescription vmstate_hmx_xfp = {
    .name = "cpu/hmx/xfp",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_INT64(sig, HmxXfp),
        VMSTATE_INT32(exp, HmxXfp),
        VMSTATE_UINT32(status.val, HmxXfp),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_hmx_acc_fxp = {
    .name = "cpu/hmx/acc_fxp",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_2DARRAY(data, HmxAccFxp, HMX_SPATIAL_DIM_FXP,
                        HMX_OUTPUT_CHANNELS, 0, vmstate_info_int32, int32_t),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_hmx_acc_fp = {
    .name = "cpu/hmx/acc_fp",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64_2DARRAY(data, HmxAccFp, HMX_SPATIAL_DIM_FP,
                               HMX_OUTPUT_CHANNELS),
        VMSTATE_STRUCT_2DARRAY(xfp_data, HmxAccFp, HMX_SPATIAL_DIM_FP,
                               HMX_OUTPUT_CHANNELS, 0, vmstate_hmx_xfp,
                               HmxXfp),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_hmx_acc_set = {
    .name = "cpu/hmx/acc_set",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(fxp_primary, HmxAccSet, 0, vmstate_hmx_acc_fxp,
                       HmxAccFxp),
        VMSTATE_STRUCT(fp_primary, HmxAccSet, 0, vmstate_hmx_acc_fp,
                       HmxAccFp),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_hmx_cvt_fxp = {
    .name = "cpu/hmx/cvt_fxp",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT16_2DARRAY(data, HmxCvtStateFxp, HMX_SPATIAL_DIM_FXP,
                               HMX_OUTPUT_CHANNELS),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_hmx_cvt_fp = {
    .name = "cpu/hmx/cvt_fp",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT16_2DARRAY(data, HmxCvtStateFp, HMX_SPATIAL_DIM_FP,
                               HMX_OUTPUT_CHANNELS),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_hmx_state = {
    .name = "cpu/hmx/state",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(acc, HmxState, HMX_NUM_ACC_SETS, 0,
                             vmstate_hmx_acc_set, HmxAccSet),
        VMSTATE_STRUCT_ARRAY(cvt_fxp, HmxState, HMX_NUM_CVT_AGES, 0,
                             vmstate_hmx_cvt_fxp, HmxCvtStateFxp),
        VMSTATE_STRUCT_ARRAY(cvt_fp, HmxState, HMX_NUM_CVT_AGES, 0,
                             vmstate_hmx_cvt_fp, HmxCvtStateFp),
        VMSTATE_STRUCT(cvt_future_fxp, HmxState, 0, vmstate_hmx_cvt_fxp,
                       HmxCvtStateFxp),
        VMSTATE_UINT64_2DARRAY(bias_raw, HmxState, HMX_NUM_BIAS_SETS,
                               HMX_OUTPUT_CHANNELS),
        VMSTATE_UINT32(current_acc_set, HmxState),
        VMSTATE_BUFFER(act_buffer, HmxState),
        VMSTATE_UINT32(act_rs, HmxState),
        VMSTATE_UINT32(act_type, HmxState),
        VMSTATE_UINT8(is_f8_odd, HmxState),
        VMSTATE_UINT8(fp8_odd_sel, HmxState),
        VMSTATE_UINT8(is_bf16, HmxState),
        VMSTATE_UINT32(format_offset, HmxState),
        VMSTATE_UINT32(tile_x_mask, HmxState),
        VMSTATE_UINT32(tile_y_mask, HmxState),
        VMSTATE_UINT32(tile_x_inc, HmxState),
        VMSTATE_UINT32(tile_y_inc, HmxState),
        VMSTATE_UINT32(fx, HmxState),
        VMSTATE_UINT32(fy, HmxState),
        VMSTATE_INT32(ch_start, HmxState),
        VMSTATE_INT32(ch_stop, HmxState),
        VMSTATE_UINT32(y_start, HmxState),
        VMSTATE_UINT32(y_stop, HmxState),
        VMSTATE_UINT32(y_dilate, HmxState),
        VMSTATE_UINT32(blocks, HmxState),
        VMSTATE_INT32(dY, HmxState),
        VMSTATE_UINT32(group_conv, HmxState),
        VMSTATE_UINT32(group_size, HmxState),
        VMSTATE_UINT32(group_count, HmxState),
        VMSTATE_INT32(mac_cycle_limit, HmxState),
        VMSTATE_UINT32(cvt_fxp_pending, HmxState),
        VMSTATE_UINT32(cvt_fxp_pending_age, HmxState),
        VMSTATE_UINT32(cvt_acc_clear_pending, HmxState),
        VMSTATE_UINT32(cvt_acc_clear_set, HmxState),
        VMSTATE_END_OF_LIST()
    },
};

static bool hexagon_hmx_needed(void *opaque)
{
    HexagonCPU *cpu = opaque;

    return cpu->hmx != NULL;
}

static const VMStateDescription vmstate_hexagon_hmx = {
    .name = "cpu/hmx",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = hexagon_hmx_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_POINTER(hmx, HexagonCPU, vmstate_hmx_state, HmxState),
        VMSTATE_END_OF_LIST()
    },
};

const VMStateDescription vmstate_hexagon_cpu = {
    .name = "cpu",
    .version_id = 3,
    .minimum_version_id = 2,
    .post_load = hexagon_cpu_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(env.gpr, HexagonCPU, TOTAL_PER_THREAD_REGS),
        VMSTATE_UINT32_ARRAY(env.pred, HexagonCPU, NUM_PREGS),
        VMSTATE_UINT32_ARRAY(env.t_sreg, HexagonCPU, NUM_SREGS),
        VMSTATE_UINT32_ARRAY(env.greg, HexagonCPU, NUM_GREGS),
        VMSTATE_UINT32(env.tlb_lock_state, HexagonCPU),
        VMSTATE_UINT32(env.k0_lock_state, HexagonCPU),
        VMSTATE_UINT32(env.tlb_lock_count, HexagonCPU),
        VMSTATE_UINT32(env.k0_lock_count, HexagonCPU),
        VMSTATE_UINT32(env.threadId, HexagonCPU),
        VMSTATE_UINT32(env.cause_code, HexagonCPU),
        VMSTATE_UNUSED(sizeof(uint64_t)),
        VMSTATE_UINT32(env.imprecise_exception, HexagonCPU),
        VMSTATE_UINT32_V(env.cfi_active, HexagonCPU, 3),

        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_hexagon_hmx,
        NULL
    },
};
