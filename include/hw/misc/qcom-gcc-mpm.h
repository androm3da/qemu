/*
 * Qualcomm GCC-MPM (Global Control Counter MSM Power Manager)
 *
 * This device implements the GCC-MPM controller which provides timer/counter
 * functionality for system timing and memory protection management.
 *
 * Copyright (c) 2025 Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_QCOM_GCC_MPM_H
#define HW_MISC_QCOM_GCC_MPM_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_QCOM_GCC_MPM "qcom-gcc-mpm"
OBJECT_DECLARE_SIMPLE_TYPE(QcomGccMpmState, QCOM_GCC_MPM)

#define GCC_MPM_REGION_SIZE 0x1000

/* GCC-MPM register offsets */
#define GCC_MPM_CONTROL_CNTCR       0x0
#define GCC_MPM_CONTROL_CNTSR       0x4
#define GCC_MPM_CONTROL_CNTCV_L     0x8
#define GCC_MPM_CONTROL_CNTCV_HI    0xC
#define GCC_MPM_CONTROL_CNTFID0     0x20
#define GCC_MPM_CONTROL_ID          0xFD0

/* Default values */
#define GCC_MPM_DEFAULT_FREQ        0x124F800   /* ~19.2MHz */
#define GCC_MPM_DEFAULT_ID          0x10000000

typedef struct QcomGccMpmState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    uint32_t regs[GCC_MPM_REGION_SIZE / 4];
    int64_t counter_offset;
} QcomGccMpmState;

#endif
