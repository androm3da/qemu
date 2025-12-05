/*
 * Hexagon PCIe Endpoint Device
 *
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/hexagon/hexagon-pcie-ep.h"
#include "hw/hexagon/virt.h"
#include "hw/pci/pci.h"
#include "hw/pci/pcie.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/qdev-properties.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "system/address-spaces.h"
#include "migration/vmstate.h"

static uint64_t hexagon_pcie_ep_ctrl_read(void *opaque, hwaddr addr,
                                          unsigned size)
{
    HexagonPCIeEPState *s = HEXAGON_PCIE_EP(opaque);
    uint64_t ret = 0;

    switch (addr) {
    case HEXAGON_EP_REG_DEVICE_ID:
        ret = s->device_id;
        break;
    case HEXAGON_EP_REG_VERSION:
        ret = s->version;
        break;
    case HEXAGON_EP_REG_FEATURES:
        ret = s->features;
        break;
    case HEXAGON_EP_REG_STATUS:
        ret = s->status;
        break;
    case HEXAGON_EP_REG_CTRL:
        ret = s->ctrl;
        break;
    case HEXAGON_EP_REG_DOORBELL:
        ret = s->doorbell_value;
        break;
    case HEXAGON_EP_REG_IRQ_STATUS:
        ret = s->irq_status;
        break;
    case HEXAGON_EP_REG_IRQ_MASK:
        ret = s->irq_mask;
        break;
    case HEXAGON_EP_REG_DMA_SRC_LO:
        ret = s->dma_src & 0xffffffff;
        break;
    case HEXAGON_EP_REG_DMA_SRC_HI:
        ret = s->dma_src >> 32;
        break;
    case HEXAGON_EP_REG_DMA_DST_LO:
        ret = s->dma_dst & 0xffffffff;
        break;
    case HEXAGON_EP_REG_DMA_DST_HI:
        ret = s->dma_dst >> 32;
        break;
    case HEXAGON_EP_REG_DMA_LEN:
        ret = s->dma_len;
        break;
    case HEXAGON_EP_REG_DMA_CTRL:
        ret = s->dma_ctrl;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "hexagon-pcie-ep: invalid read at offset 0x%x\n",
                      (uint32_t)addr);
        break;
    }

    return ret;
}

static void hexagon_pcie_ep_ctrl_write(void *opaque, hwaddr addr,
                                       uint64_t val, unsigned size)
{
    HexagonPCIeEPState *s = HEXAGON_PCIE_EP(opaque);
    PCIDevice *pci_dev = PCI_DEVICE(s);

    switch (addr) {
    case HEXAGON_EP_REG_CTRL:
        if (val & HEXAGON_EP_CTRL_RESET) {
            /* Reset Hexagon processor */
            qemu_log_mask(LOG_UNIMP,
                          "hexagon-pcie-ep: reset not implemented\n");
        }
        if (val & HEXAGON_EP_CTRL_BOOT) {
            /* Boot Hexagon processor */
            s->status |= HEXAGON_EP_STATUS_BOOTED;
        }
        if (val & HEXAGON_EP_CTRL_DMA_START) {
            /* Start DMA transfer */
            qemu_log_mask(LOG_UNIMP,
                          "hexagon-pcie-ep: DMA not implemented\n");
        }
        s->ctrl = val;
        break;
    case HEXAGON_EP_REG_DOORBELL:
        s->doorbell_value = val;
        /* Generate interrupt to Hexagon if enabled */
        if (s->irq_mask & (1 << 0)) {
            s->irq_status |= (1 << 0);
            if (msix_enabled(pci_dev)) {
                msix_notify(pci_dev, 0);
            } else if (msi_enabled(pci_dev)) {
                msi_notify(pci_dev, 0);
            }
        }
        break;
    case HEXAGON_EP_REG_IRQ_STATUS:
        s->irq_status &= ~val; /* Clear on write */
        break;
    case HEXAGON_EP_REG_IRQ_MASK:
        s->irq_mask = val;
        break;
    case HEXAGON_EP_REG_DMA_SRC_LO:
        s->dma_src = (s->dma_src & 0xffffffff00000000ULL) | val;
        break;
    case HEXAGON_EP_REG_DMA_SRC_HI:
        s->dma_src = (s->dma_src & 0xffffffff) | (val << 32);
        break;
    case HEXAGON_EP_REG_DMA_DST_LO:
        s->dma_dst = (s->dma_dst & 0xffffffff00000000ULL) | val;
        break;
    case HEXAGON_EP_REG_DMA_DST_HI:
        s->dma_dst = (s->dma_dst & 0xffffffff) | (val << 32);
        break;
    case HEXAGON_EP_REG_DMA_LEN:
        s->dma_len = val;
        break;
    case HEXAGON_EP_REG_DMA_CTRL:
        s->dma_ctrl = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "hexagon-pcie-ep: invalid write at offset 0x%x\n",
                      (uint32_t)addr);
        break;
    }
}

static const MemoryRegionOps hexagon_pcie_ep_ctrl_ops = {
    .read = hexagon_pcie_ep_ctrl_read,
    .write = hexagon_pcie_ep_ctrl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t hexagon_pcie_ep_doorbell_read(void *opaque, hwaddr addr,
                                              unsigned size)
{
    return 0;
}

static void hexagon_pcie_ep_doorbell_write(void *opaque, hwaddr addr,
                                          uint64_t val, unsigned size)
{
    HexagonPCIeEPState *s = HEXAGON_PCIE_EP(opaque);
    PCIDevice *pci_dev = PCI_DEVICE(s);

    /* Doorbell from host to Hexagon */
    /* Different doorbells can trigger different interrupts */
    int vector = addr / 4;

    if (vector < HEXAGON_EP_MSIX_VECTORS) {
        if (msix_enabled(pci_dev)) {
            msix_notify(pci_dev, vector);
        } else if (msi_enabled(pci_dev)) {
            msi_notify(pci_dev, 0);
        }
    }
}

static const MemoryRegionOps hexagon_pcie_ep_doorbell_ops = {
    .read = hexagon_pcie_ep_doorbell_read,
    .write = hexagon_pcie_ep_doorbell_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void hexagon_pcie_ep_realize(PCIDevice *pci_dev, Error **errp)
{
    HexagonPCIeEPState *s = HEXAGON_PCIE_EP(pci_dev);
    Error *local_err = NULL;

    /* Initialize control registers */
    s->device_id = PCI_DEVICE_ID_HEXAGON_EP;
    s->version = 0x00010000; /* Version 1.0 */
    s->features = HEXAGON_EP_FEAT_MSI | HEXAGON_EP_FEAT_MSIX |
                  HEXAGON_EP_FEAT_DMA | HEXAGON_EP_FEAT_VIRTIO;
    s->status = HEXAGON_EP_STATUS_READY;

    /* Initialize BAR0: Control/Status registers */
    memory_region_init_io(&s->ctrl_mmio, OBJECT(s),
                         &hexagon_pcie_ep_ctrl_ops, s,
                         "hexagon-pcie-ep-ctrl", 256);
    pci_register_bar(pci_dev, HEXAGON_EP_BAR_CTRL,
                     PCI_BASE_ADDRESS_SPACE_MEMORY, &s->ctrl_mmio);

    /* Initialize BAR1: DDR memory (4GB) */
    memory_region_init(&s->ddr_mmio, OBJECT(s),
                       "hexagon-pcie-ep-ddr", 4ULL * 1024 * 1024 * 1024);
    pci_register_bar(pci_dev, HEXAGON_EP_BAR_DDR,
                     PCI_BASE_ADDRESS_SPACE_MEMORY |
                     PCI_BASE_ADDRESS_MEM_PREFETCH |
                     PCI_BASE_ADDRESS_MEM_TYPE_64,
                     &s->ddr_mmio);

    /* Initialize BAR2: TCM memory (256KB) */
    memory_region_init(&s->tcm_mmio, OBJECT(s),
                       "hexagon-pcie-ep-tcm", 256 * 1024);
    pci_register_bar(pci_dev, HEXAGON_EP_BAR_TCM,
                     PCI_BASE_ADDRESS_SPACE_MEMORY, &s->tcm_mmio);

    /* Initialize BAR3: VTCM memory (256KB) */
    memory_region_init(&s->vtcm_mmio, OBJECT(s),
                       "hexagon-pcie-ep-vtcm", 256 * 1024);
    pci_register_bar(pci_dev, HEXAGON_EP_BAR_VTCM,
                     PCI_BASE_ADDRESS_SPACE_MEMORY, &s->vtcm_mmio);

    /* Initialize BAR4: Shared memory for virtio (1MB) */
    memory_region_init(&s->shared_mmio, OBJECT(s),
                       "hexagon-pcie-ep-shared", 1024 * 1024);
    pci_register_bar(pci_dev, HEXAGON_EP_BAR_SHARED,
                     PCI_BASE_ADDRESS_SPACE_MEMORY, &s->shared_mmio);

    /* Initialize BAR5: Doorbell registers */
    memory_region_init_io(&s->doorbell_mmio, OBJECT(s),
                         &hexagon_pcie_ep_doorbell_ops, s,
                         "hexagon-pcie-ep-doorbell", 4096);
    pci_register_bar(pci_dev, HEXAGON_EP_BAR_DOORBELL,
                     PCI_BASE_ADDRESS_SPACE_MEMORY, &s->doorbell_mmio);

    /* Initialize MSI */
    int ret = msi_init(pci_dev, 0x50, 1, true, false, &local_err);
    if (ret) {
        /* MSI not supported, clear error and continue */
        error_free(local_err);
        local_err = NULL;
        s->features &= ~HEXAGON_EP_FEAT_MSI;
    }

    /* Initialize MSI-X */
    ret = msix_init(pci_dev, HEXAGON_EP_MSIX_VECTORS,
                    &s->ctrl_mmio, HEXAGON_EP_BAR_CTRL, 0x100,
                    &s->ctrl_mmio, HEXAGON_EP_BAR_CTRL, 0x200,
                    0x00, &local_err);
    if (ret) {
        /* MSI-X not supported, clear error and continue */
        error_free(local_err);
        local_err = NULL;
        s->features &= ~HEXAGON_EP_FEAT_MSIX;
    }

    /* Enable PCIe capabilities */
    if (!pci_is_express(pci_dev)) {
        pcie_endpoint_cap_init(pci_dev, 0x80);
    }
}

static void hexagon_pcie_ep_exit(PCIDevice *pci_dev)
{
    HexagonPCIeEPState *s = HEXAGON_PCIE_EP(pci_dev);

    msix_uninit(pci_dev, &s->ctrl_mmio, &s->ctrl_mmio);
    msi_uninit(pci_dev);
}

static void hexagon_pcie_ep_reset(DeviceState *dev)
{
    HexagonPCIeEPState *s = HEXAGON_PCIE_EP(dev);

    s->status = HEXAGON_EP_STATUS_READY;
    s->ctrl = 0;
    s->irq_status = 0;
    s->irq_mask = 0;
    s->dma_src = 0;
    s->dma_dst = 0;
    s->dma_len = 0;
    s->dma_ctrl = 0;
    s->doorbell_value = 0;
}

static const VMStateDescription vmstate_hexagon_pcie_ep = {
    .name = "hexagon-pcie-endpoint",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, HexagonPCIeEPState),
        VMSTATE_UINT32(device_id, HexagonPCIeEPState),
        VMSTATE_UINT32(version, HexagonPCIeEPState),
        VMSTATE_UINT32(features, HexagonPCIeEPState),
        VMSTATE_UINT32(status, HexagonPCIeEPState),
        VMSTATE_UINT32(ctrl, HexagonPCIeEPState),
        VMSTATE_UINT32(irq_status, HexagonPCIeEPState),
        VMSTATE_UINT32(irq_mask, HexagonPCIeEPState),
        VMSTATE_UINT64(dma_src, HexagonPCIeEPState),
        VMSTATE_UINT64(dma_dst, HexagonPCIeEPState),
        VMSTATE_UINT32(dma_len, HexagonPCIeEPState),
        VMSTATE_UINT32(dma_ctrl, HexagonPCIeEPState),
        VMSTATE_UINT32(doorbell_value, HexagonPCIeEPState),
        VMSTATE_END_OF_LIST()
    }
};

static void hexagon_pcie_ep_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pdc = PCI_DEVICE_CLASS(klass);

    pdc->realize = hexagon_pcie_ep_realize;
    pdc->exit = hexagon_pcie_ep_exit;
    pdc->vendor_id = PCI_VENDOR_ID_QUALCOMM;
    pdc->device_id = PCI_DEVICE_ID_HEXAGON_EP;
    pdc->revision = 0x00;
    pdc->class_id = PCI_CLASS_PROCESSOR_CO;
    pdc->subsystem_vendor_id = PCI_VENDOR_ID_QUALCOMM;
    pdc->subsystem_id = PCI_DEVICE_ID_HEXAGON_EP;

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "Hexagon PCIe Endpoint";
    device_class_set_legacy_reset(dc, hexagon_pcie_ep_reset);
    dc->vmsd = &vmstate_hexagon_pcie_ep;
}

static const TypeInfo hexagon_pcie_ep_info = {
    .name = TYPE_HEXAGON_PCIE_EP,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(HexagonPCIeEPState),
    .class_init = hexagon_pcie_ep_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { }
    },
};

static void hexagon_pcie_ep_register_types(void)
{
    type_register_static(&hexagon_pcie_ep_info);
}

type_init(hexagon_pcie_ep_register_types)
