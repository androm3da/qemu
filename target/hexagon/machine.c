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

static bool hexagon_hvm_needed(void *opaque)
{
    HexagonCPU *cpu = opaque;

    return cpu->hvm != NULL;
}

static const VMStateDescription vmstate_hexagon_hvm_map = {
    .name = "cpu/hvm_map",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = hexagon_hvm_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(env.hvm_map.type, HexagonCPU),
        VMSTATE_UINT8(env.hvm_map.extra, HexagonCPU),
        VMSTATE_BOOL(env.hvm_map.identity, HexagonCPU),
        VMSTATE_UINT32(env.hvm_map.ptb, HexagonCPU),
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
        &vmstate_hexagon_hvm_map,
        NULL
    },
};
