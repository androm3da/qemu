/*
 * Hexagon Global Registers QOM Object
 *
 * Copyright(c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HEXAGON_GLOBALREG_H
#define HEXAGON_GLOBALREG_H

#include "hw/qdev-core.h"
#include "hw/sysbus.h"
#include "qom/object.h"
#include "target/hexagon/cpu.h"

#define TYPE_HEXAGON_GLOBALREG "hexagon-globalreg"
OBJECT_DECLARE_SIMPLE_TYPE(HexagonGlobalRegState, HEXAGON_GLOBALREG)

/* Common lock state structure */
typedef struct {
    uint32_t waiters_mask;   /* Mask of HTIDs waiting for this lock */
    uint32_t last_holder;    /* HTID of last lock holder (current if held, previous if free) */
} HexagonLockState;

struct HexagonGlobalRegState {
    SysBusDevice parent_obj;

    /* Array of system registers */
    target_ulong regs[NUM_SREGS];

    /* Global performance cycle counter base */
    uint64_t g_pcycle_base;

    /* Properties for global register reset values */
    uint32_t boot_evb;           /* Boot Exception Vector Base (HEX_SREG_EVB) */
    uint64_t config_table_addr;  /* Configuration table base */
    uint32_t dsp_rev;           /* DSP revision register (HEX_SREG_REV) */

    /* ISDB properties */
    bool isdben_etm_enable;     /* ISDB ETM enable bit */
    bool isdben_dfd_enable;     /* ISDB DFD enable bit */
    bool isdben_trusted;        /* ISDB trusted mode bit */
    bool isdben_secure;         /* ISDB secure mode bit */

    /* Hardware base addresses */
    uint32_t qtimer_base_addr;  /* QTimer hardware base address */

    /* Lock state tracking */
    HexagonLockState k0lock_state;   /* k0lock state */
    HexagonLockState tlblock_state;  /* tlblock state */
};

/* Public interface functions */
uint32_t hexagon_globalreg_read(HexagonCPU *cpu, uint32_t reg);
void hexagon_globalreg_write(HexagonCPU *cpu, uint32_t reg,
                             uint32_t value);
uint32_t hexagon_globalreg_masked_value(HexagonCPU *cpu, uint32_t reg,
                                        uint32_t value);
void hexagon_globalreg_write_masked(HexagonCPU *cpu, uint32_t reg,
                                    uint32_t value);
void hexagon_globalreg_reset(HexagonCPU *cpu);

/* Global performance cycle counter access */
uint64_t hexagon_globalreg_get_pcycle_base(HexagonCPU *cpu);
void hexagon_globalreg_set_pcycle_base(HexagonCPU *cpu, uint64_t value);

uint32_t hexagon_globalreg_get_boot_evb(HexagonCPU *cpu);

/* SYSCFG lock bit access functions */
bool hexagon_globalreg_set_k0lock(HexagonGlobalRegState *g_reg, bool value,
                                  uint32_t htid);
bool hexagon_globalreg_set_tlblock(HexagonGlobalRegState *g_reg, bool value,
                                   uint32_t htid);

/* Lock waiters mask management */
void hexagon_globalreg_clear_k0lock_waiter(HexagonGlobalRegState *g_reg,
                                           uint32_t htid);
void hexagon_globalreg_clear_tlblock_waiter(HexagonGlobalRegState *g_reg,
                                            uint32_t htid);


#endif /* HEXAGON_GLOBALREG_H */
