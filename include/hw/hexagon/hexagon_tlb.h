/*
 * Hexagon TLB QOM Object
 *
 * Copyright(c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HEXAGON_TLB_H
#define HEXAGON_TLB_H

#include "hw/qdev-core.h"
#include "hw/sysbus.h"
#include "qom/object.h"
#include "target/hexagon/cpu.h"

#define TYPE_HEXAGON_TLB "hexagon-tlb"
#define NO_ASID      (1 << 8)

OBJECT_DECLARE_SIMPLE_TYPE(HexagonTLBState, HEXAGON_TLB)

struct HexagonTLBState {
    SysBusDevice parent_obj;

    /* TLB entries array */
    uint64_t entries[MAX_TLB_ENTRIES];

    /* TLB configuration */
    uint32_t num_entries;           /* Actual number of TLB entries */

    /* TLB locking state (shared across threads) */
    uint32_t lock_state;            /* TLB lock status */
    uint32_t lock_count;            /* Lock reference count */
};

/* Public interface functions */
void hexagon_tlb_write_entry(HexagonCPU *cpu, uint32_t index, uint64_t value);
uint64_t hexagon_tlb_read_entry(HexagonCPU *cpu, uint32_t index);
uint32_t hexagon_tlb_lookup(HexagonCPU *cpu, uint32_t ssr, uint32_t va);
bool hexagon_tlb_find_match(HexagonCPU *cpu, target_ulong va,
                            MMUAccessType access_type, hwaddr *pa, int *prot,
                            int *size, int32_t *excp, int mmu_idx);
int hexagon_tlb_check_overlap(HexagonCPU *cpu, uint64_t entry, uint64_t index);
void hexagon_tlb_lock(HexagonCPU *cpu);
void hexagon_tlb_unlock(HexagonCPU *cpu);
void hexagon_tlb_reset(HexagonCPU *cpu);
void hexagon_tlb_dump(HexagonCPU *cpu);

/* Helper functions for TLB entry matching */
bool hex_tlb_entry_match_noperm(uint64_t entry, uint32_t asid, uint64_t va);

#endif /* HEXAGON_TLB_H */
