/*
 * QCS6490 PCIe Controller
 *
 * Based on Qualcomm SM8250 PCIe controller which uses Synopsys DesignWare
 * PCIe IP with Qualcomm-specific PARF (PCIe Auxiliary Register Framework).
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci-host/qcs6490-pcie.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "hw/resettable.h"

/* PARF register read handler */
static uint64_t qcs6490_pcie_parf_read(void *opaque, hwaddr addr, unsigned size)
{
    QCS6490PCIeHost *s = QCS6490_PCIE_HOST(opaque);
    uint32_t val = 0;

    switch (addr) {
    case PARF_SYS_CTRL:
        val = s->parf_sys_ctrl;
        break;
    case PARF_PM_CTRL:
        val = s->parf_pm_ctrl;
        break;
    case PARF_PCS_DEEMPH:
        val = s->parf_pcs_deemph;
        break;
    case PARF_PCS_SWING:
        val = s->parf_pcs_swing;
        break;
    case PARF_PHY_CTRL:
        val = s->parf_phy_ctrl;
        break;
    case PARF_PHY_REFCLK:
        val = s->parf_phy_refclk;
        break;
    case PARF_CONFIG_BITS:
        val = s->parf_config_bits;
        break;
    case PARF_DBI_BASE_ADDR:
        val = s->parf_dbi_base_addr;
        break;
    case PARF_SLV_ADDR_SPACE_SIZE:
        val = s->parf_slv_addr_space_size;
        break;
    case PARF_MHI_CLOCK_RESET_CTRL:
        val = s->parf_mhi_clock_reset_ctrl;
        break;
    case PARF_AXI_MSTR_WR_ADDR_HALT:
        val = s->parf_axi_mstr_wr_addr_halt;
        break;
    case PARF_AXI_MSTR_WR_ADDR_HALT_V2:
        val = s->parf_axi_mstr_wr_addr_halt_v2;
        break;
    case PARF_Q2A_FLUSH:
        val = s->parf_q2a_flush;
        break;
    case PARF_LTSSM:
        val = s->parf_ltssm;
        break;
    case PARF_INT_ALL_STATUS:
        val = s->parf_int_all_status;
        break;
    case PARF_INT_ALL_CLEAR:
        val = s->parf_int_all_clear;
        break;
    case PARF_INT_ALL_MASK:
        val = s->parf_int_all_mask;
        break;
    case PARF_SID_OFFSET:
        val = s->parf_sid_offset;
        break;
    case PARF_BDF_TRANSLATE_CFG:
        val = s->parf_bdf_translate_cfg;
        break;
    case PARF_DBI_BASE_ADDR_V2:
        val = s->parf_dbi_base_addr_v2;
        break;
    case PARF_DBI_BASE_ADDR_V2_HI:
        val = s->parf_dbi_base_addr_v2_hi;
        break;
    case PARF_SLV_ADDR_SPACE_SIZE_V2:
        val = s->parf_slv_addr_space_size_v2;
        break;
    case PARF_SLV_ADDR_SPACE_SIZE_V2_HI:
        val = s->parf_slv_addr_space_size_v2_hi;
        break;
    case PARF_NO_SNOOP_OVERIDE:
        val = s->parf_no_snoop_overide;
        break;
    case PARF_ATU_BASE_ADDR:
        val = s->parf_atu_base_addr;
        break;
    case PARF_ATU_BASE_ADDR_HI:
        val = s->parf_atu_base_addr_hi;
        break;
    case PARF_DEVICE_TYPE:
        val = s->parf_device_type;
        break;
    case PARF_BDF_TO_SID_CFG:
        val = s->parf_bdf_to_sid_cfg;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "qcs6490-pcie: PARF read from unknown register 0x%"
                      HWADDR_PRIx "\n", addr);
        break;
    }

    qemu_log_mask(LOG_TRACE, "qcs6490-pcie: PARF read 0x%" HWADDR_PRIx
                  " = 0x%08x\n", addr, val);
    return val;
}

/* PARF register write handler */
static void qcs6490_pcie_parf_write(void *opaque, hwaddr addr,
                                    uint64_t val, unsigned size)
{
    QCS6490PCIeHost *s = QCS6490_PCIE_HOST(opaque);

    qemu_log_mask(LOG_TRACE, "qcs6490-pcie: PARF write 0x%" HWADDR_PRIx
                  " = 0x%08x\n", addr, (uint32_t)val);

    switch (addr) {
    case PARF_SYS_CTRL:
        s->parf_sys_ctrl = val;
        break;
    case PARF_PM_CTRL:
        s->parf_pm_ctrl = val;
        break;
    case PARF_PCS_DEEMPH:
        s->parf_pcs_deemph = val;
        break;
    case PARF_PCS_SWING:
        s->parf_pcs_swing = val;
        break;
    case PARF_PHY_CTRL:
        s->parf_phy_ctrl = val;
        break;
    case PARF_PHY_REFCLK:
        s->parf_phy_refclk = val;
        break;
    case PARF_CONFIG_BITS:
        s->parf_config_bits = val;
        break;
    case PARF_DBI_BASE_ADDR:
        s->parf_dbi_base_addr = val;
        break;
    case PARF_SLV_ADDR_SPACE_SIZE:
        s->parf_slv_addr_space_size = val;
        break;
    case PARF_MHI_CLOCK_RESET_CTRL:
        s->parf_mhi_clock_reset_ctrl = val;
        /* Simulate clock enable/disable based on written bits */
        if (val & AHB_CLK_EN) {
            s->clocks[QCS6490_PCIE_CLK_AUX] = true;
        }
        if (val & MSTR_AXI_CLK_EN) {
            s->clocks[QCS6490_PCIE_CLK_BUS_MASTER] = true;
        }
        break;
    case PARF_AXI_MSTR_WR_ADDR_HALT:
        s->parf_axi_mstr_wr_addr_halt = val;
        break;
    case PARF_AXI_MSTR_WR_ADDR_HALT_V2:
        s->parf_axi_mstr_wr_addr_halt_v2 = val;
        break;
    case PARF_Q2A_FLUSH:
        s->parf_q2a_flush = val;
        break;
    case PARF_LTSSM:
        s->parf_ltssm = val;
        if (val & LTSSM_EN) {
            s->ltssm_enabled = true;
            /* Simulate link coming up after LTSSM enable */
            s->link_up = true;
            s->parf_int_all_status |= PARF_INT_ALL_LINK_UP;
        } else {
            s->ltssm_enabled = false;
            s->link_up = false;
            s->parf_int_all_status &= ~PARF_INT_ALL_LINK_UP;
        }
        break;
    case PARF_INT_ALL_STATUS:
        /* Status register - ignore writes */
        break;
    case PARF_INT_ALL_CLEAR:
        s->parf_int_all_clear = val;
        /* Clear status bits based on clear register */
        s->parf_int_all_status &= ~val;
        break;
    case PARF_INT_ALL_MASK:
        s->parf_int_all_mask = val;
        break;
    case PARF_SID_OFFSET:
        s->parf_sid_offset = val;
        break;
    case PARF_BDF_TRANSLATE_CFG:
        s->parf_bdf_translate_cfg = val;
        break;
    case PARF_DBI_BASE_ADDR_V2:
        s->parf_dbi_base_addr_v2 = val;
        break;
    case PARF_DBI_BASE_ADDR_V2_HI:
        s->parf_dbi_base_addr_v2_hi = val;
        break;
    case PARF_SLV_ADDR_SPACE_SIZE_V2:
        s->parf_slv_addr_space_size_v2 = val;
        break;
    case PARF_SLV_ADDR_SPACE_SIZE_V2_HI:
        s->parf_slv_addr_space_size_v2_hi = val;
        break;
    case PARF_NO_SNOOP_OVERIDE:
        s->parf_no_snoop_overide = val;
        break;
    case PARF_ATU_BASE_ADDR:
        s->parf_atu_base_addr = val;
        break;
    case PARF_ATU_BASE_ADDR_HI:
        s->parf_atu_base_addr_hi = val;
        break;
    case PARF_DEVICE_TYPE:
        s->parf_device_type = val;
        break;
    case PARF_BDF_TO_SID_CFG:
        s->parf_bdf_to_sid_cfg = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "qcs6490-pcie: PARF write to unknown register 0x%"
                      HWADDR_PRIx " = 0x%08x\n", addr, (uint32_t)val);
        break;
    }
}

static const MemoryRegionOps qcs6490_pcie_parf_ops = {
    .read = qcs6490_pcie_parf_read,
    .write = qcs6490_pcie_parf_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

/* IRQ handling */
static void qcs6490_pcie_set_irq(void *opaque, int irq_num, int level)
{
    QCS6490PCIeHost *s = opaque;

    qemu_set_irq(s->irq[irq_num], level);
}

void qcs6490_pcie_set_irq_num(QCS6490PCIeHost *s, int index, int gsi)
{
    if (index >= PCI_NUM_PINS) {
        return;
    }

    s->irq_num[index] = gsi;
}

static PCIINTxRoute qcs6490_pcie_route_intx_pin_to_irq(void *opaque, int pin)
{
    PCIINTxRoute route;
    QCS6490PCIeHost *s = opaque;
    int gsi = s->irq_num[pin];

    route.irq = gsi;
    route.mode = gsi < 0 ? PCI_INTX_DISABLED : PCI_INTX_ENABLED;

    return route;
}

static int qcs6490_pcie_swizzle_map_irq_fn(PCIDevice *pci_dev, int pin)
{
    PCIBus *bus = pci_device_root_bus(pci_dev);

    return (PCI_SLOT(pci_dev->devfn) + pin) % bus->nirq;
}

static void qcs6490_pcie_host_realize(DeviceState *dev, Error **errp)
{
    QCS6490PCIeHost *s = QCS6490_PCIE_HOST(dev);
    PCIHostState *pci = PCI_HOST_BRIDGE(dev);
    PCIExpressHost *pex = PCIE_HOST_BRIDGE(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    int i;

    /* Initialize ECAM */
    pcie_host_mmcfg_init(pex, PCIE_MMCFG_SIZE_MAX);
    sysbus_init_mmio(sbd, &pex->mmio);

    /* Initialize MMIO and I/O regions */
    memory_region_init(&s->io_mmio, OBJECT(s),
                       "qcs6490-pcie-mmio", UINT64_MAX);
    memory_region_init(&s->io_ioport, OBJECT(s),
                       "qcs6490-pcie-ioport", 64 * 1024);

    sysbus_init_mmio(sbd, &s->io_mmio);
    sysbus_init_mmio(sbd, &s->io_ioport);

    /* Initialize PARF register region */
    memory_region_init_io(&s->parf_regs, OBJECT(s), &qcs6490_pcie_parf_ops,
                          s, "qcs6490-pcie-parf", 0x4000);
    sysbus_init_mmio(sbd, &s->parf_regs);

    /* Initialize IRQs */
    for (i = 0; i < PCI_NUM_PINS; i++) {
        sysbus_init_irq(sbd, &s->irq[i]);
        s->irq_num[i] = -1;
    }

    /* Create PCI bus */
    pci->bus = pci_register_root_bus(dev, "pcie.0", qcs6490_pcie_set_irq,
                                     qcs6490_pcie_swizzle_map_irq_fn,
                                     s, &s->io_mmio,
                                     &s->io_ioport, 0,
                                     PCI_NUM_PINS, TYPE_PCIE_BUS);

    pci_bus_set_route_irq_fn(pci->bus, qcs6490_pcie_route_intx_pin_to_irq);

    /* Realize root device */
    qdev_realize(DEVICE(&s->root), BUS(pci->bus), &error_fatal);
}

static void qcs6490_pcie_host_reset(Object *obj, ResetType type)
{
    QCS6490PCIeHost *s = QCS6490_PCIE_HOST(obj);
    int i;

    /* Reset PARF registers to default values */
    s->parf_sys_ctrl = 0;
    s->parf_pm_ctrl = 0;
    s->parf_pcs_deemph = 0;
    s->parf_pcs_swing = 0;
    s->parf_phy_ctrl = PHY_TEST_PWR_DOWN; /* PHY starts powered down */
    s->parf_phy_refclk = 0;
    s->parf_config_bits = 0;
    s->parf_dbi_base_addr = 0;
    s->parf_slv_addr_space_size = SLV_ADDR_SPACE_SZ;
    s->parf_mhi_clock_reset_ctrl = 0;
    s->parf_axi_mstr_wr_addr_halt = 0;
    s->parf_axi_mstr_wr_addr_halt_v2 = 0;
    s->parf_q2a_flush = 0;
    s->parf_ltssm = 0;
    s->parf_int_all_status = 0;
    s->parf_int_all_clear = 0;
    s->parf_int_all_mask = 0;
    s->parf_sid_offset = 0;
    s->parf_bdf_translate_cfg = 0;
    s->parf_dbi_base_addr_v2 = 0;
    s->parf_dbi_base_addr_v2_hi = 0;
    s->parf_slv_addr_space_size_v2 = 0;
    s->parf_slv_addr_space_size_v2_hi = SLV_ADDR_SPACE_SZ;
    s->parf_no_snoop_overide = 0;
    s->parf_atu_base_addr = 0;
    s->parf_atu_base_addr_hi = 0;
    s->parf_device_type = DEVICE_TYPE_RC; /* Root Complex */
    s->parf_bdf_to_sid_cfg = BDF_TO_SID_BYPASS;

    /* Reset clocks to disabled state */
    for (i = 0; i < QCS6490_PCIE_CLK_COUNT; i++) {
        s->clocks[i] = false;
    }

    /* Reset PHY registers */
    for (i = 0; i < QCS6490_PCIE_PHY_REG_COUNT; i++) {
        s->phy_registers[i] = 0;
    }

    /* Reset link state */
    s->link_up = false;
    s->ltssm_enabled = false;
}

static const char *qcs6490_pcie_host_root_bus_path(PCIHostState *host_bridge,
                                                   PCIBus *rootbus)
{
    return "0000:00";
}

static void qcs6490_pcie_host_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIHostBridgeClass *hc = PCI_HOST_BRIDGE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    hc->root_bus_path = qcs6490_pcie_host_root_bus_path;
    dc->realize = qcs6490_pcie_host_realize;
    rc->phases.enter = qcs6490_pcie_host_reset;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->fw_name = "pci";
    dc->desc = "QCS6490 PCIe host bridge";
}

static void qcs6490_pcie_host_initfn(Object *obj)
{
    QCS6490PCIeHost *s = QCS6490_PCIE_HOST(obj);
    QCS6490PCIeRoot *root = &s->root;

    /* Initialize PARF register values to ensure they're set without reset */
    s->parf_phy_ctrl = PHY_TEST_PWR_DOWN; /* PHY starts powered down */
    s->parf_device_type = DEVICE_TYPE_RC; /* Root Complex */
    s->parf_bdf_to_sid_cfg = BDF_TO_SID_BYPASS;
    s->parf_slv_addr_space_size = SLV_ADDR_SPACE_SZ;

    object_initialize_child(obj, "qcs6490_pcie_root", root,
                            TYPE_QCS6490_PCIE_ROOT);
    qdev_prop_set_int32(DEVICE(root), "addr", PCI_DEVFN(0, 0));
    qdev_prop_set_bit(DEVICE(root), "multifunction", false);
}

static const TypeInfo qcs6490_pcie_host_info = {
    .name       = TYPE_QCS6490_PCIE_HOST,
    .parent     = TYPE_PCIE_HOST_BRIDGE,
    .instance_size = sizeof(QCS6490PCIeHost),
    .instance_init = qcs6490_pcie_host_initfn,
    .class_init = qcs6490_pcie_host_class_init,
};

/* QCS6490 PCIe Root Device Implementation */

static const VMStateDescription vmstate_qcs6490_pcie_root = {
    .name = "qcs6490_pcie_root",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, QCS6490PCIeRoot),
        VMSTATE_END_OF_LIST()
    }
};

static void qcs6490_pcie_root_class_init(ObjectClass *klass, const void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->desc = "QCS6490 PCIe host bridge root device";
    dc->vmsd = &vmstate_qcs6490_pcie_root;

    /* Use Qualcomm vendor ID and SM8250 PCIe device ID */
    k->vendor_id = 0x17cb;  /* Qualcomm */
    k->device_id = 0x010b;  /* SM8250 PCIe Root Complex */
    k->revision = 0;
    k->class_id = PCI_CLASS_BRIDGE_HOST;

    /* Not user creatable */
    dc->user_creatable = false;
}

static const TypeInfo qcs6490_pcie_root_info = {
    .name = TYPE_QCS6490_PCIE_ROOT,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(QCS6490PCIeRoot),
    .class_init = qcs6490_pcie_root_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void qcs6490_pcie_register(void)
{
    type_register_static(&qcs6490_pcie_root_info);
    type_register_static(&qcs6490_pcie_host_info);
}

type_init(qcs6490_pcie_register)
