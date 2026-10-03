/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "cpu.h"
#include "system/cpus.h"
#include "exec/cpu-interrupt.h"
#include "accel/tcg/cpu-loop.h"
#include "hw/hexagon/hexagon_globalreg.h"
#include "hex_lock.h"

static int lock_wakeup_interrupt(HexagonGlobalLock which)
{
    switch (which) {
    case HEXAGON_GLOBAL_LOCK_K0:
        return CPU_INTERRUPT_K0_UNLOCK;
    case HEXAGON_GLOBAL_LOCK_TLB:
        return CPU_INTERRUPT_TLB_UNLOCK;
    default:
        g_assert_not_reached();
    }
}

static void wake_lock_cpu(uint32_t htid, HexagonGlobalLock which)
{
    CPUState *cs = qemu_get_cpu(htid);

    g_assert(cs);
    cpu_interrupt(cs, lock_wakeup_interrupt(which));
    qemu_cpu_kick(cs);
}

void hexagon_lock(CPUHexagonState *env, HexagonGlobalLock which)
{
    HexagonCPU *cpu = env_archcpu(env);
    CPUState *cs = env_cpu(env);

    BQL_LOCK_GUARD();
    if (!cpu->globalregs ||
        hexagon_globalreg_lock(cpu->globalregs, which, env->threadId)) {
        env->next_PC += 4;
        return;
    }

    /* Re-execute the lock instruction after the handoff wakeup. */
    env->gpr[HEX_REG_PC] = env->next_PC - 4;
    cpu_interrupt(cs, CPU_INTERRUPT_HALT);
    cpu_loop_exit(cs);
}

void hexagon_unlock(CPUHexagonState *env, HexagonGlobalLock which)
{
    HexagonCPU *cpu = env_archcpu(env);
    int next;

    BQL_LOCK_GUARD();
    if (!cpu->globalregs) {
        return;
    }

    next = hexagon_globalreg_unlock(cpu->globalregs, which);
    if (next >= 0) {
        wake_lock_cpu(next, which);
    }
}

bool hexagon_locks_owned(CPUHexagonState *env)
{
    HexagonCPU *cpu = env_archcpu(env);

    g_assert(bql_locked());
    return cpu->globalregs &&
        (hexagon_globalreg_lock_owned(cpu->globalregs,
                                     HEXAGON_GLOBAL_LOCK_K0,
                                     env->threadId) ||
         hexagon_globalreg_lock_owned(cpu->globalregs,
                                     HEXAGON_GLOBAL_LOCK_TLB,
                                     env->threadId));
}

bool hexagon_locks_waiting(CPUHexagonState *env)
{
    HexagonCPU *cpu = env_archcpu(env);

    g_assert(bql_locked());
    return cpu->globalregs &&
        (hexagon_globalreg_lock_waiting(cpu->globalregs,
                                       HEXAGON_GLOBAL_LOCK_K0,
                                       env->threadId) ||
         hexagon_globalreg_lock_waiting(cpu->globalregs,
                                       HEXAGON_GLOBAL_LOCK_TLB,
                                       env->threadId));
}
