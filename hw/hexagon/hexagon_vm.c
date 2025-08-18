/*
 * Hexagon VM State Object - Global state shared across all vCPUs
 *
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/hexagon/hexagon_vm.h"
#include "exec/cpu-common.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qom/object.h"
#include "target/hexagon/cpu.h"
#include "target/hexagon/hex_mmu.h"

static void hexagon_vm_init(Object *obj)
{
  HexagonVMState *vms = HEXAGON_VM(obj);

  /* Initialize VM state */
  vms->next_vm_id = 1;
  vms->max_vm_id = 64;
  vms->vm_enabled = true;
  vms->next_vm_index = 2; /* Reserve slots 0-1 for other uses */
}

static void hexagon_vm_finalize(Object *obj)
{
  /* No specific cleanup needed */
}

static void hexagon_vm_class_init(ObjectClass *oc, const void *data)
{
  /* No specific class initialization needed for now */
}

static const TypeInfo hexagon_vm_type_info = {
    .name = TYPE_HEXAGON_VM,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(HexagonVMState),
    .instance_init = hexagon_vm_init,
    .instance_finalize = hexagon_vm_finalize,
    .class_init = hexagon_vm_class_init,
};

void hex_convert_vm_page_table(CPUHexagonState *env, uint32_t ptb_addr)
{
  qemu_log_mask(CPU_LOG_MMU, "Converting VM page table from 0x%08x\n",
                ptb_addr);

  HexagonCPU *cpu = env_archcpu(env);
  uint32_t tlb_index = hexagon_vm_get_tlb_index(cpu->vm);
  const uint32_t MAX_TLB_INDEX = 63;

  /* Store the page table base in VM state */
  hexagon_vm_set_guest_ptb(cpu->vm, ptb_addr);

  /* Walk through VM page table entries and convert valid ones */
  for (uint32_t va = 0; va <= 0xFFC00000; va += 0x400000) {
    uint32_t pte_addr =
        ptb_addr + ((va >> 22) << 2); /* Each PTE is 4 bytes, covers 4MB */
    uint32_t vmpte;

    /* Read the VM page table entry */
    if (cpu_memory_rw_debug(env_cpu(env), pte_addr, (uint8_t *)&vmpte, 4, 0) !=
        0) {
        continue; /* Skip if can't read */
    }

    /* Skip invalid entries (page size 7 = invalid) */
    uint32_t pgsize = vmpte & 0x7;
    if (pgsize == 7) {
        continue;
    }

    /* Check if we have available TLB slots */
    if (tlb_index > MAX_TLB_INDEX) {
        qemu_log_mask(CPU_LOG_MMU, "No more TLB slots available\n");
      break;
    }

    /* Convert VMPTE to TLB PTE format */
    uint64_t tlb_pte = 0;

    /* Extract fields from VMPTE format */
    uint32_t phys_addr = vmpte & 0xFFFFF000; /* Physical address [31:12] */
    uint32_t x = (vmpte >> 11) & 1;          /* Execute bit */
    uint32_t w = (vmpte >> 10) & 1;          /* Write bit */
    uint32_t r = (vmpte >> 9) & 1;           /* Read bit */
    uint32_t ccc = (vmpte >> 6) & 7;         /* Cache control */
    uint32_t u = (vmpte >> 5) & 1;           /* User bit */

    /* Build TLB PTE in correct format */
    tlb_pte |= 1ULL << 63; /* PTE_V (bit 63) */
    tlb_pte |= (uint64_t)(va >> TARGET_PAGE_BITS)
               << 32;               /* PTE_VPN (bits 32-51) */
    tlb_pte |= (uint64_t)x << 31;   /* PTE_X (bit 31) */
    tlb_pte |= (uint64_t)w << 30;   /* PTE_W (bit 30) */
    tlb_pte |= (uint64_t)r << 29;   /* PTE_R (bit 29) */
    tlb_pte |= (uint64_t)u << 28;   /* PTE_U (bit 28) */
    tlb_pte |= (uint64_t)ccc << 24; /* PTE_C (bits 24-27) */

    /* Set page size encoding */
    uint32_t tlb_pgsize_type;
    switch (pgsize) {
    case 5:                /* 4M in VMPTE becomes PGSIZE_4M in TLB */
      tlb_pgsize_type = 4; /* PGSIZE_4M */
      break;
    case 4:                /* 1M in VMPTE becomes PGSIZE_1M in TLB */
      tlb_pgsize_type = 3; /* PGSIZE_1M */
      break;
    default:
      qemu_log_mask(CPU_LOG_MMU, "Unsupported page size %d in VMPTE\n", pgsize);
      continue;
    }

    /* Set the bit at the correct position for page size encoding */
    tlb_pte |= 1ULL << tlb_pgsize_type;

    /*
     * Set physical page descriptor - bits 0-23 according to
     * reg_fields_def.h.inc
     */
    tlb_pte |= (uint64_t)(phys_addr >> TARGET_PAGE_BITS) & 0xFFFFFF;

    /* Write to next available TLB slot */
    hex_tlbw(env, tlb_index, tlb_pte);
    qemu_log_mask(CPU_LOG_MMU,
                  "TLB[%d]: VA=0x%08x -> PA=0x%08x (VMPTE=0x%08x)\n", tlb_index,
                  va, phys_addr, vmpte);

    tlb_index++;
  }

  /* Update next available index */
  hexagon_vm_set_tlb_index(cpu->vm, tlb_index);
}

/*
 * HexagonVM Public Interface Implementation
 * Following QOM encapsulation patterns
 */

bool hexagon_vm_is_enabled(HexagonVMState *vm_state)
{
  return vm_state->vm_enabled;
}

uint32_t hexagon_vm_get_next_vm_id(HexagonVMState *vm_state)
{
  if (!vm_state->vm_enabled) {
    return 0; /* Invalid VM ID */
  }

  if (vm_state->next_vm_id >= vm_state->max_vm_id) {
    return 0; /* No more VM IDs available */
  }

  return vm_state->next_vm_id++;
}

uint32_t hexagon_vm_get_tlb_index(HexagonVMState *vm_state)
{
  return vm_state->next_vm_index;
}

void hexagon_vm_set_tlb_index(HexagonVMState *vm_state, uint32_t index)
{
  vm_state->next_vm_index = index;
}

void hexagon_vm_reset_tlb_index(HexagonVMState *vm_state)
{
  vm_state->next_vm_index = 2; /* Reserve slots 0-1 for other uses */
}

void hexagon_vm_set_guest_ptb(HexagonVMState *vm_state, uint32_t ptb_addr)
{
  vm_state->guest_ptb = ptb_addr;
}

uint32_t hexagon_vm_get_guest_ptb(HexagonVMState *vm_state)
{
  return vm_state->guest_ptb;
}

static void hexagon_vm_register_types(void)
{
  type_register_static(&hexagon_vm_type_info);
}

type_init(hexagon_vm_register_types)
