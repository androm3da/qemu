/*
 * Hexagon VM State Object - Global state shared across all vCPUs
 *
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_HEXAGON_VM_H
#define HW_HEXAGON_VM_H

#include "qom/object.h"

#define TYPE_HEXAGON_VM "hexagon-vm"
OBJECT_DECLARE_SIMPLE_TYPE(HexagonVMState, HEXAGON_VM)

struct HexagonVMState {
  /*< private >*/
  Object parent_obj;

  /*< public >*/
  /* VM state shared across all hardware threads/vCPUs */
  uint32_t next_vm_id;    /* Next virtual processor ID to allocate */
  uint32_t max_vm_id;     /* Maximum number of virtual processors */
  bool vm_enabled;        /* Whether VM functionality is enabled */
  uint32_t next_vm_index; /* Next available TLB index for VM entries */
  uint32_t guest_ptb;     /* Guest page table base for VM operations */
};

/* Forward declaration for CPUHexagonState */
typedef struct CPUArchState CPUHexagonState;

/* VM page table conversion function */
extern void hex_convert_vm_page_table(CPUHexagonState *env, uint32_t ptb_addr);

/*
 * HexagonVM Public Interface Functions
 * Following QOM encapsulation patterns to manage VM state
 */

/**
 * hexagon_vm_is_enabled: Check if VM functionality is enabled
 * @vm_state: the HexagonVM object
 * Returns: true if VM features are enabled, false otherwise
 */
bool hexagon_vm_is_enabled(HexagonVMState *vm_state);

/**
 * hexagon_vm_get_next_vm_id: Get next available VM ID and increment counter
 * @vm_state: the HexagonVM object
 * Returns: next available VM ID, or 0 if no more IDs available
 */
uint32_t hexagon_vm_get_next_vm_id(HexagonVMState *vm_state);

/**
 * hexagon_vm_get_tlb_index: Get current TLB index for VM entries
 * @vm_state: the HexagonVM object
 * Returns: current next_vm_index value
 */
uint32_t hexagon_vm_get_tlb_index(HexagonVMState *vm_state);

/**
 * hexagon_vm_set_tlb_index: Set the TLB index for VM entries
 * @vm_state: the HexagonVM object
 * @index: new TLB index value
 */
void hexagon_vm_set_tlb_index(HexagonVMState *vm_state, uint32_t index);

/**
 * hexagon_vm_reset_tlb_index: Reset TLB index to initial value
 * @vm_state: the HexagonVM object
 */
void hexagon_vm_reset_tlb_index(HexagonVMState *vm_state);

/**
 * hexagon_vm_set_guest_ptb: Set the guest page table base address
 * @vm_state: the HexagonVM object
 * @ptb_addr: guest page table base address
 */
void hexagon_vm_set_guest_ptb(HexagonVMState *vm_state, uint32_t ptb_addr);

/**
 * hexagon_vm_get_guest_ptb: Get the guest page table base address
 * @vm_state: the HexagonVM object
 * Returns: current guest page table base address
 */
uint32_t hexagon_vm_get_guest_ptb(HexagonVMState *vm_state);

#endif /* HW_HEXAGON_VM_H */
