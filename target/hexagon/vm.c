// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright(c) 2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "cpu_helper.h"
#include "exec/helper-proto.h"
#include "exec/cputlb.h"
#include "qemu/log.h"
#include "arch.h"
#include "internal.h"
#include "macros.h"
#include "sys_macros.h"
#include "vm.h"

#if !defined(CONFIG_USER_ONLY)

/*
 * Hexagon Virtual Machine Implementation
 *
 * This implements the Hexagon VM specification which provides a hypervisor
 * interface for running guest operating systems on Hexagon DSP cores.
 *
 * This is a simplified implementation for basic functionality.
 */


/* VM Version */
#define HEXAGON_VM_VERSION 0x800

/* Main VM instruction dispatcher */
void hexagon_vm_instruction(CPUHexagonState *env, uint32_t operand)
{
    CPUState *cs = env_cpu(env);
    HexagonCPU *cpu = HEXAGON_CPU(cs);

    /* Check if VM state is available - if not, return error */
    if (!cpu->vm) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "VM instruction 0x%02x called but no VM state\n",
                      operand);
        env->gpr[HEX_REG_R00] = (uint32_t)-1; /* Return error */
        return;
    }

    switch (operand) {
    case HEX_VIRT_VMVERSION:
        env->gpr[HEX_REG_R00] = HEXAGON_VM_VERSION;
        break;

    case HEX_VIRT_VMSETREGS:
        /* Set guest registers G0-G3 from R0-R3 */
        if (env->greg) {
            env->greg[0] = env->gpr[HEX_REG_R00];
            env->greg[1] = env->gpr[HEX_REG_R01];
            env->greg[2] = env->gpr[HEX_REG_R02];
            env->greg[3] = env->gpr[HEX_REG_R03];
        }
        break;

    case HEX_VIRT_VMGETREGS:
        /* Get guest registers G0-G3 to R0-R3 */
        if (env->greg) {
            env->gpr[HEX_REG_R00] = env->greg[0];
            env->gpr[HEX_REG_R01] = env->greg[1];
            env->gpr[HEX_REG_R02] = env->greg[2];
            env->gpr[HEX_REG_R03] = env->greg[3];
        } else {
            env->gpr[HEX_REG_R00] = 0;
            env->gpr[HEX_REG_R01] = 0;
            env->gpr[HEX_REG_R02] = 0;
            env->gpr[HEX_REG_R03] = 0;
        }
        break;

    case HEX_VIRT_VMGETINFO:
        /* Return build ID */
        env->gpr[HEX_REG_R00] = 0x0001;
        break;

    default:
        /* Invalid VM instruction */
        qemu_log_mask(LOG_UNIMP, "Unknown VM instruction 0x%02x\n", operand);
        env->gpr[HEX_REG_R00] = (uint32_t)-1;
        break;
    }
}

/* Helper functions for VM operations */
void HELPER(vmnewmap)(CPUHexagonState *env, uint32_t r1, uint32_t r0,
                      uint32_t r2)
{
    /* VM new map - simplified implementation */
    env->gpr[HEX_REG_R00] = 0; /* Success */
}

uint32_t HELPER(vmgetinfo)(CPUHexagonState *env, uint32_t info)
{
    /* Return VM info based on requested info type */
    switch (info) {
    case 0: /* VM Version */
        return HEXAGON_VM_VERSION;
    case 1: /* Build ID */
        return 0x0001;
    default:
        return 0;
    }
}

void HELPER(vm_trace)(uint32_t val)
{
    /* VM trace - for debugging */
    qemu_log_mask(CPU_LOG_TB_IN_ASM, "VM trace: 0x%08x\n", val);
}

void HELPER(vm_entry_trace)(CPUHexagonState *env, uint32_t operand,
                            uint32_t r0, uint32_t r1)
{
    qemu_log_mask(CPU_LOG_TB_IN_ASM,
                  "VM entry: op=0x%02x r0=0x%08x r1=0x%08x\n",
                  operand, r0, r1);
}

void HELPER(vm_exit_trace)(CPUHexagonState *env, uint32_t operand,
                           uint32_t r0, uint32_t r1)
{
    qemu_log_mask(CPU_LOG_TB_IN_ASM, "VM exit: op=0x%02x r0=0x%08x r1=0x%08x\n",
                  operand, r0, r1);
}

uint32_t HELPER(vmsetie)(CPUHexagonState *env, uint32_t val)
{
    /* Set interrupt enable - simplified */
    return 0;
}

uint32_t HELPER(vmgetie)(CPUHexagonState *env)
{
    /* Get interrupt enable - simplified */
    return 0;
}

uint32_t HELPER(vmstart)(CPUHexagonState *env, uint32_t tid,
                         uint32_t start_addr)
{
    /* Start thread - simplified */
    qemu_log_mask(LOG_UNIMP, "vmstart: tid=%d addr=0x%08x\n", tid, start_addr);
    return 0; /* Success */
}

void HELPER(vmgettime_trace)(uint32_t hi, uint32_t lo)
{
    qemu_log_mask(CPU_LOG_TB_IN_ASM, "vmgettime: 0x%08x%08x\n", hi, lo);
}

void HELPER(vmrte_trace)(uint32_t r0, uint32_t r1)
{
    qemu_log_mask(CPU_LOG_TB_IN_ASM, "vmrte: r0=0x%08x r1=0x%08x\n", r0, r1);
}

void HELPER(vmrte_pc_trace)(uint32_t pc)
{
    qemu_log_mask(CPU_LOG_TB_IN_ASM, "vmrte: pc=0x%08x\n", pc);
}

void HELPER(vmrte_handler)(CPUHexagonState *env)
{
    /* VM return to exception handler - simplified */
    /* Just return without doing anything complex for now */
    env->gpr[HEX_REG_R00] = 0;
}

void HELPER(mmu_mode_change)(CPUHexagonState *env)
{
    /* MMU mode change - flush TLB */
    CPUState *cs = env_cpu(env);
    tlb_flush(cs);
}

#endif /* !CONFIG_USER_ONLY */
