/*
 * Hexagon PCIe Endpoint Device
 *
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HEXAGON_PCIE_EP_H
#define HEXAGON_PCIE_EP_H

#include "hw/pci/pci_device.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "qom/object.h"

#define TYPE_HEXAGON_PCIE_EP "hexagon-pcie-endpoint"
OBJECT_DECLARE_SIMPLE_TYPE(HexagonPCIeEPState, HEXAGON_PCIE_EP)

/* PCI vendor/device IDs */
#define PCI_VENDOR_ID_QUALCOMM  0x17cb
#define PCI_DEVICE_ID_HEXAGON_EP 0x0100

/* Number of BARs for different memory regions */
#define HEXAGON_EP_BAR_COUNT 6

/* BAR indices */
#define HEXAGON_EP_BAR_CTRL     0  /* Control/status registers */
#define HEXAGON_EP_BAR_DDR      1  /* DDR memory */
#define HEXAGON_EP_BAR_TCM      2  /* TCM memory */
#define HEXAGON_EP_BAR_VTCM     3  /* VTCM memory */
#define HEXAGON_EP_BAR_SHARED   4  /* Shared memory for virtio queues */
#define HEXAGON_EP_BAR_DOORBELL 5  /* Doorbell registers */

/* Control/Status register offsets */
#define HEXAGON_EP_REG_DEVICE_ID    0x00
#define HEXAGON_EP_REG_VERSION      0x04
#define HEXAGON_EP_REG_FEATURES     0x08
#define HEXAGON_EP_REG_STATUS       0x0C
#define HEXAGON_EP_REG_CTRL         0x10
#define HEXAGON_EP_REG_DOORBELL     0x14
#define HEXAGON_EP_REG_IRQ_STATUS   0x20
#define HEXAGON_EP_REG_IRQ_MASK     0x24
#define HEXAGON_EP_REG_DMA_SRC_LO   0x30
#define HEXAGON_EP_REG_DMA_SRC_HI   0x34
#define HEXAGON_EP_REG_DMA_DST_LO   0x38
#define HEXAGON_EP_REG_DMA_DST_HI   0x3C
#define HEXAGON_EP_REG_DMA_LEN      0x40
#define HEXAGON_EP_REG_DMA_CTRL     0x44

/* Feature bits */
#define HEXAGON_EP_FEAT_MSI     (1 << 0)
#define HEXAGON_EP_FEAT_MSIX    (1 << 1)
#define HEXAGON_EP_FEAT_DMA     (1 << 2)
#define HEXAGON_EP_FEAT_VIRTIO  (1 << 3)

/* Status bits */
#define HEXAGON_EP_STATUS_READY     (1 << 0)
#define HEXAGON_EP_STATUS_BOOTED    (1 << 1)
#define HEXAGON_EP_STATUS_DMA_BUSY  (1 << 2)

/* Control bits */
#define HEXAGON_EP_CTRL_RESET       (1 << 0)
#define HEXAGON_EP_CTRL_BOOT        (1 << 1)
#define HEXAGON_EP_CTRL_DMA_START   (1 << 2)

/* Number of MSI-X vectors */
#define HEXAGON_EP_MSIX_VECTORS 16

typedef struct HexagonPCIeEPState {
    /* Private */
    PCIDevice parent_obj;

    /* Public */

    /* Memory regions for BARs */
    MemoryRegion ctrl_mmio;      /* BAR0: Control/status registers */
    MemoryRegion ddr_mmio;       /* BAR1: DDR memory */
    MemoryRegion tcm_mmio;       /* BAR2: TCM memory */
    MemoryRegion vtcm_mmio;      /* BAR3: VTCM memory */
    MemoryRegion shared_mmio;    /* BAR4: Shared memory for virtio */
    MemoryRegion doorbell_mmio;  /* BAR5: Doorbell registers */

    /* Control/status registers */
    uint32_t device_id;
    uint32_t version;
    uint32_t features;
    uint32_t status;
    uint32_t ctrl;
    uint32_t irq_status;
    uint32_t irq_mask;

    /* DMA registers */
    uint64_t dma_src;
    uint64_t dma_dst;
    uint32_t dma_len;
    uint32_t dma_ctrl;

    /* Hexagon machine state pointer */
    void *hexagon_machine;

    /* Doorbell state */
    uint32_t doorbell_value;

    /* MSI-X support */
    bool msix_enabled;
} HexagonPCIeEPState;

#endif /* HEXAGON_PCIE_EP_H */
