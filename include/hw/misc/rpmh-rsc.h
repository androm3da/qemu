/*
 * Qualcomm RPMH-RSC (Resource Power Manager Hardware - Resource State
 * Coordinator)
 *
 * Copyright (c) 2024 Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_RPMH_RSC_H
#define HW_MISC_RPMH_RSC_H

#include "hw/sysbus.h"
#include "hw/resettable.h"
#include "qom/object.h"

#define TYPE_RPMH_RSC "rpmh-rsc"
OBJECT_DECLARE_TYPE(RpmhRscState, RpmhRscClass, RPMH_RSC)

#define RPMH_RSC_REGISTER_SPACE_SIZE 0x20000  /* 128KB */
#define RPMH_RSC_MAX_TCS 32                   /* Maximum TCS per RSC */
#define RPMH_RSC_MAX_CMDS_PER_TCS 16          /* Maximum commands per TCS */

/* Command structure for TCS */
typedef struct {
    uint32_t addr;
    uint32_t data;
    uint32_t wait;
    uint32_t status;
} RpmhRscCommand;

/* TCS (Trigger Command Set) state */
typedef struct {
    uint32_t control;
    uint32_t status;
    uint32_t cmd_enable;
    bool triggered;
    RpmhRscCommand commands[RPMH_RSC_MAX_CMDS_PER_TCS];
} RpmhRscTcsState;

struct RpmhRscClass {
    SysBusDeviceClass parent_class;

    /* To store parent reset phases */
    ResettablePhases parent_phases;
};

struct RpmhRscState {
    SysBusDevice parent_obj;

    /* Separate memory regions for DRV and TCS register spaces */
    MemoryRegion drv_iomem;  /* DRV base registers */
    MemoryRegion tcs_iomem;  /* TCS registers */

    /* Device configuration */
    uint32_t version;
    uint32_t regs[16];  /* Register offsets array */

    /* DRV registers */
    uint32_t drv_solver_config;
    uint32_t drv_prnt_chld_config;
    uint32_t irq_enable;
    uint32_t irq_status;
    uint32_t tcs_base;

    /* TCS states */
    RpmhRscTcsState tcs_states[RPMH_RSC_MAX_TCS];
};

#endif /* HW_MISC_RPMH_RSC_H */
