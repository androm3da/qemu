/*
 * QUP wrapper (GENI SE QUP) device
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_QUP_WRAPPER_H
#define HW_MISC_QUP_WRAPPER_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_QUP_WRAPPER "qup-wrapper"
OBJECT_DECLARE_SIMPLE_TYPE(QupWrapperState, QUP_WRAPPER)

/* Size of the wrapper's own register block */
#define QUP_WRAPPER_MMIO_SIZE 0x6000

struct QupWrapperState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    /* Properties */
    uint32_t hw_version;
};

#endif /* HW_MISC_QUP_WRAPPER_H */
