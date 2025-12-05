/*
 * Hexagon PCIe Host Controller
 *
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/hexagon/hexagon-pci-host.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_host.h"
#include "hw/sysbus.h"
#include "qemu/module.h"

static void hexagon_pci_host_realize(DeviceState *dev, Error **errp)
{
    /* Nothing special needed for basic PCI host */
}

static void hexagon_pci_host_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = hexagon_pci_host_realize;
    dc->desc = "Hexagon PCI Host Bridge";
}

static const TypeInfo hexagon_pci_host_info = {
    .name = TYPE_HEXAGON_PCI_HOST,
    .parent = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(HexagonPCIHostState),
    .class_init = hexagon_pci_host_class_init,
};

static void hexagon_pci_host_register_types(void)
{
    type_register_static(&hexagon_pci_host_info);
}

type_init(hexagon_pci_host_register_types)
