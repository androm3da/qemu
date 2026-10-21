/*
 * Definitions for hexagon virt board.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_HEXAGONVIRT_H
#define HW_HEXAGONVIRT_H

#include "hw/hexagon/hexagon.h"
#include "target/hexagon/cpu.h"

#define VIRTIO_DEV_COUNT 8

typedef struct HexagonVirtMachineInfo {
    const char *desc;
    const char *cpu_type;
    unsigned int cpus;
    Rev_t rev;
    const struct hexagon_machine_config *cfg;
} HexagonVirtMachineInfo;

struct HexagonVirtMachineState {
    HexagonCommonMachineState parent_obj;

    int fdt_size;
    MemoryRegion tcm;
};

struct HexagonVirtMachineClass {
    MachineClass parent_class;

    const HexagonVirtMachineInfo *info;
};

#define TYPE_HEXAGON_VIRT_MACHINE MACHINE_TYPE_NAME("hexagon-virt")
OBJECT_DECLARE_TYPE(HexagonVirtMachineState, HexagonVirtMachineClass,
                    HEXAGON_VIRT_MACHINE)

#endif /* HW_HEXAGONVIRT_H */
