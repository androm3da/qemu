/*
 * Hexagon PCIe Host Controller
 *
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_HEXAGON_PCI_HOST_H
#define HW_HEXAGON_PCI_HOST_H

#include "hw/pci/pci_host.h"
#include "qom/object.h"

#define TYPE_HEXAGON_PCI_HOST "hexagon-pci-host"
OBJECT_DECLARE_SIMPLE_TYPE(HexagonPCIHostState, HEXAGON_PCI_HOST)

struct HexagonPCIHostState {
    PCIHostState parent_obj;
};

#endif /* HW_HEXAGON_PCI_HOST_H */
