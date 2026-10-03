/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/qemu-print.h"
#include "cpu.h"
#include "system/cpus.h"
#include "internal.h"
#include "exec/cpu-interrupt.h"
#include "accel/tcg/cpu-loop.h"
#include "cpu_helper.h"
#include "exec/cputlb.h"
#include "hex_mmu.h"
#include "macros.h"
#include "sys_macros.h"
#include "hw/hexagon/hexagon_tlb.h"
#include "hw/hexagon/hexagon_globalreg.h"
#include "hex_lock.h"

static inline void hex_log_tlbw(uint32_t index, uint64_t entry)
{
    qemu_log_mask(CPU_LOG_MMU,
                  "tlbw[%03" PRIu32 "]: 0x%016" PRIx64 "\n",
                  index, entry);
}

void hex_tlbw(CPUHexagonState *env, uint32_t index, uint64_t value)
{
    uint32_t myidx = fTLB_NONPOW2WRAP(fTLB_IDXMASK(index));
    HexagonTLBState *tlb = env_archcpu(env)->tlb;
    uint64_t old_entry = hexagon_tlb_read(tlb, myidx);

    bool old_entry_valid = extract64(old_entry, 63, 1);
    if (old_entry_valid && hexagon_cpu_mmu_enabled(env)) {
        CPUState *cs = env_cpu(env);
        tlb_flush(cs);
    }
    hexagon_tlb_write(tlb, myidx, value);
    hex_log_tlbw(myidx, value);
}

void hex_mmu_on(CPUHexagonState *env)
{
    CPUState *cs = env_cpu(env);
    qemu_log_mask(CPU_LOG_MMU, "Hexagon MMU turned on!\n");
    tlb_flush(cs);
}

void hex_mmu_off(CPUHexagonState *env)
{
    CPUState *cs = env_cpu(env);
    qemu_log_mask(CPU_LOG_MMU, "Hexagon MMU turned off!\n");
    tlb_flush(cs);
}

void hex_mmu_mode_change(CPUHexagonState *env)
{
    qemu_log_mask(CPU_LOG_MMU, "Hexagon mode change!\n");
    CPUState *cs = env_cpu(env);
    tlb_flush(cs);
}

bool hex_tlb_find_match(CPUHexagonState *env, uint32_t VA,
                        MMUAccessType access_type, hwaddr *PA, int *prot,
                        uint64_t *size, int32_t *excp, int mmu_idx)
{
    HexagonCPU *cpu = env_archcpu(env);
    uint32_t ssr = env->t_sreg[HEX_SREG_SSR];
    uint8_t asid = GET_SSR_FIELD(SSR_ASID, ssr);
    int cause_code = 0;
    bool found;

    env->imprecise_exception = 0;
    found = hexagon_tlb_find_match(cpu->tlb, asid, VA, access_type,
                                   PA, prot, size, excp, &cause_code,
                                   mmu_idx);
    if (*excp == HEX_EVENT_IMPRECISE) {
        env->imprecise_exception = *excp;
        *excp = 0;
    }
    if (cause_code) {
        env->cause_code = cause_code;
    }
    return found;
}

/* Called from tlbp instruction */
uint32_t hex_tlb_lookup(CPUHexagonState *env, uint32_t ssr, uint32_t VA)
{
    HexagonCPU *cpu = env_archcpu(env);
    uint8_t asid = GET_SSR_FIELD(SSR_ASID, ssr);
    uint32_t imprecise_exception = 0;
    int cause_code = 0;

    uint32_t result = hexagon_tlb_lookup(cpu->tlb, asid, VA,
                                         &imprecise_exception, &cause_code);
    env->imprecise_exception = imprecise_exception;
    if (cause_code) {
        env->cause_code = cause_code;
    }
    return result;
}

/*
 * Return codes:
 * 0 or positive             index of match
 * -1                        multiple matches
 * -2                        no match
 */
int hex_tlb_check_overlap(CPUHexagonState *env, uint64_t entry, uint64_t index)
{
    HexagonCPU *cpu = env_archcpu(env);
    return hexagon_tlb_check_overlap(cpu->tlb, entry, index);
}

#ifdef CONFIG_HMP
void dump_mmu(MonitorHMP *hmp, CPUHexagonState *env)
{
    HexagonCPU *cpu = env_archcpu(env);
    hexagon_tlb_dump(hmp, cpu->tlb);
}
#endif

void hex_tlb_lock(CPUHexagonState *env)
{
    qemu_log_mask(CPU_LOG_MMU, "hex_tlb_lock: " TARGET_FMT_ld "\n",
                  env->threadId);
    hexagon_lock(env, HEXAGON_GLOBAL_LOCK_TLB);
}

void hex_tlb_unlock(CPUHexagonState *env)
{
    hexagon_unlock(env, HEXAGON_GLOBAL_LOCK_TLB);
}
