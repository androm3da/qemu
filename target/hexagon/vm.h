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

#ifndef HEXAGON_VM_H
#define HEXAGON_VM_H

#include "cpu.h"

#if !defined(CONFIG_USER_ONLY)

/* VM Virtual Instruction Opcodes */
enum hex_virt_opc {
  HEX_VIRT_VMVERSION = 0x00,     /* Get VM version */
  HEX_VIRT_VMSETVEC = 0x01,      /* Set exception vector base */
  HEX_VIRT_VMGETSETREG = 0x02,   /* Get/set registers */
  HEX_VIRT_VMSETIE = 0x03,       /* Set interrupt enable */
  HEX_VIRT_VMGETIE = 0x04,       /* Get interrupt enable */
  HEX_VIRT_VMINTOP = 0x05,       /* Interrupt operations */
  HEX_VIRT_VMSWAP = 0x06,        /* Swap contexts */
  HEX_VIRT_VMVPID = 0x07,        /* Get virtual processor ID */
  HEX_VIRT_VMCACHE = 0x08,       /* Cache operations */
  HEX_VIRT_VMGETTIME = 0x09,     /* Get time */
  HEX_VIRT_VMSETTIME = 0x0A,     /* Set time */
  HEX_VIRT_VMRETURN = 0x0B,      /* Return from VM */
  HEX_VIRT_VMYIELD = 0x0C,       /* Yield processor */
  HEX_VIRT_VMSTOP = 0x0D,        /* Stop processor */
  HEX_VIRT_VMNEWMAP = 0x0E,      /* New mapping */
  HEX_VIRT_VMRESUME = 0x0F,      /* Resume processor */
  HEX_VIRT_VMCLEARMAP = 0x10,    /* Clear mapping */
  HEX_VIRT_VMDUMPMAP = 0x11,     /* Dump mapping */
  HEX_VIRT_VMGETINFO = 0x12,     /* Get system info */
  HEX_VIRT_VMTIMEROP = 0x13,     /* Timer operations */
  HEX_VIRT_VMSTART = 0x14,       /* Start processor */
  HEX_VIRT_VMSETREGS = 0x15,     /* Set guest registers */
  HEX_VIRT_VMGETREGS = 0x16,     /* Get guest registers */
  HEX_VIRT_VMWAIT = 0x17,        /* Wait for interrupt */
};

/*
 * Hexagon Virtual Machine Interface
 *
 * This header defines the interface for the Hexagon VM implementation
 * which provides hypervisor functionality for guest operating systems.
 */

/* Function declarations */

/**
 * hexagon_vm_instruction - Execute a VM virtual instruction
 * @env: CPU environment
 * @operand: VM instruction operand (from trap1 immediate)
 *
 * This function dispatches VM virtual instructions (trap1 with various
 * immediate values) to their appropriate handlers.
 */
void hexagon_vm_instruction(CPUHexagonState *env, uint32_t operand);

/**
 * hexagon_vm_enabled - Check if VM is enabled
 * @cpu: Hexagon CPU instance
 * @return: true if VM is enabled
 */
static inline bool hexagon_vm_enabled(HexagonCPU *cpu)
{
    return cpu->hexagon_vm;
}

#else /* CONFIG_USER_ONLY */

/* Stub for user mode */
static inline void hexagon_vm_instruction(CPUHexagonState *env,
                                          uint32_t operand) {
  /* Should never be called in user mode */
}

#endif /* !CONFIG_USER_ONLY */

#endif /* HEXAGON_VM_H */
