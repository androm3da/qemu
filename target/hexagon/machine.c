/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "cpu.h"
#include "cpu_helper.h"
#include "hlx/hlx.h"
#include "hw/hexagon/hexagon_hlx.h"

static int hexagon_cpu_post_load(void *opaque, int version_id)
{
    HexagonCPU *cpu = opaque;
    CPUHexagonState *env = &cpu->env;

    hexagon_hvx_select_context(env, env->t_sreg[HEX_SREG_SSR]);
    env->LRegs_live = env->lregs_fallback;
    if (cpu->hlx) {
        HLXVector *ctx0 = hexagon_hlx_context_lregs(cpu->hlx, 0);
        if (ctx0) {
            env->LRegs_live = ctx0;
        }
    }

    return 0;
}

static int get_hlxvector(QEMUFile *f, void *pv, size_t size,
                         const VMStateField *field)
{
    HLXVector *v = pv;
    for (int i = 0; i < HLX_VEC_SIZE_BYTES / 8; i++) {
        v->ud[i] = qemu_get_be64(f);
    }
    return 0;
}

static int put_hlxvector(QEMUFile *f, void *pv, size_t size,
                         const VMStateField *field, JSONWriter *vmdesc)
{
    HLXVector *v = pv;
    for (int i = 0; i < HLX_VEC_SIZE_BYTES / 8; i++) {
        qemu_put_be64(f, v->ud[i]);
    }
    return 0;
}

static const VMStateInfo vmstate_info_hlxvector = {
    .name = "hlxvector",
    .get  = get_hlxvector,
    .put  = put_hlxvector,
};

#define VMSTATE_HLXVECTOR_ARRAY(_f, _s, _n) \
    VMSTATE_SUB_ARRAY(_f, _s, 0, _n, 0, vmstate_info_hlxvector, HLXVector)
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
        VMSTATE_UINT64(env.t_cycle_count, HexagonCPU),
        VMSTATE_UINT32(env.imprecise_exception, HexagonCPU),
        VMSTATE_UINT32_V(env.cfi_active, HexagonCPU, 3),
        VMSTATE_HLXVECTOR_ARRAY(env.future_LRegs, HexagonCPU, NUM_LREGS),

        VMSTATE_END_OF_LIST()
    },
};
