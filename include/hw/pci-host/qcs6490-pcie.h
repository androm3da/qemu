/*
 * QCS6490 PCIe Controller
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_PCI_HOST_QCS6490_PCIE_H
#define HW_PCI_HOST_QCS6490_PCIE_H

#include "hw/pci/pci_bridge.h"
#include "hw/pci/pcie_host.h"
#include "qom/object.h"

#define TYPE_QCS6490_PCIE_HOST "qcs6490-pcie-host"
#define TYPE_QCS6490_PCIE_ROOT "qcs6490-pcie-root"

typedef struct QCS6490PCIeHost QCS6490PCIeHost;
typedef struct QCS6490PCIeRoot QCS6490PCIeRoot;

OBJECT_DECLARE_SIMPLE_TYPE(QCS6490PCIeHost, QCS6490_PCIE_HOST)
OBJECT_DECLARE_SIMPLE_TYPE(QCS6490PCIeRoot, QCS6490_PCIE_ROOT)

/* PARF (PCIe Auxiliary Register Framework) Registers */
#define PARF_SYS_CTRL                   0x00
#define PARF_PM_CTRL                    0x20
#define PARF_PCS_DEEMPH                 0x34
#define PARF_PCS_SWING                  0x38
#define PARF_PHY_CTRL                   0x40
#define PARF_PHY_REFCLK                 0x4c
#define PARF_CONFIG_BITS                0x50
#define PARF_DBI_BASE_ADDR              0x168
#define PARF_SLV_ADDR_SPACE_SIZE        0x16c
#define PARF_MHI_CLOCK_RESET_CTRL       0x174
#define PARF_AXI_MSTR_WR_ADDR_HALT      0x178
#define PARF_AXI_MSTR_WR_ADDR_HALT_V2   0x1a8
#define PARF_Q2A_FLUSH                  0x1ac
#define PARF_LTSSM                      0x1b0
#define PARF_INT_ALL_STATUS             0x224
#define PARF_INT_ALL_CLEAR              0x228
#define PARF_INT_ALL_MASK               0x22c
#define PARF_SID_OFFSET                 0x234
#define PARF_BDF_TRANSLATE_CFG          0x24c
#define PARF_DBI_BASE_ADDR_V2           0x350
#define PARF_DBI_BASE_ADDR_V2_HI        0x354
#define PARF_SLV_ADDR_SPACE_SIZE_V2     0x358
#define PARF_SLV_ADDR_SPACE_SIZE_V2_HI  0x35c
#define PARF_NO_SNOOP_OVERIDE           0x3d4
#define PARF_ATU_BASE_ADDR              0x634
#define PARF_ATU_BASE_ADDR_HI           0x638
#define PARF_DEVICE_TYPE                0x1000
#define PARF_BDF_TO_SID_TABLE_N         0x2000
#define PARF_BDF_TO_SID_CFG             0x2c00

/* PARF Register Field Definitions */

/* PARF_SYS_CTRL register fields */
#define MAC_PHY_POWERDOWN_IN_P2_D_MUX_EN    BIT(29)
#define MST_WAKEUP_EN                       BIT(13)
#define SLV_WAKEUP_EN                       BIT(12)
#define MSTR_ACLK_CGC_DIS                   BIT(10)
#define SLV_ACLK_CGC_DIS                    BIT(9)
#define CORE_CLK_CGC_DIS                    BIT(6)
#define AUX_PWR_DET                         BIT(4)
#define L23_CLK_RMV_DIS                     BIT(2)
#define L1_CLK_RMV_DIS                      BIT(1)

/* PARF_PM_CTRL register fields */
#define REQ_NOT_ENTR_L1                     BIT(5)

/* PARF_PCS_DEEMPH register fields */
#define PCS_DEEMPH_TX_DEEMPH_GEN1_SHIFT     16
#define PCS_DEEMPH_TX_DEEMPH_GEN1_MASK      0x3f
#define PCS_DEEMPH_TX_DEEMPH_GEN2_3_5DB_SHIFT 8
#define PCS_DEEMPH_TX_DEEMPH_GEN2_3_5DB_MASK  0x3f
#define PCS_DEEMPH_TX_DEEMPH_GEN2_6DB_SHIFT 0
#define PCS_DEEMPH_TX_DEEMPH_GEN2_6DB_MASK  0x3f

/* PARF_PCS_SWING register fields */
#define PCS_SWING_TX_SWING_FULL_SHIFT       8
#define PCS_SWING_TX_SWING_FULL_MASK        0x7f
#define PCS_SWING_TX_SWING_LOW_SHIFT        0
#define PCS_SWING_TX_SWING_LOW_MASK         0x7f

/* PARF_PHY_CTRL register fields */
#define PHY_CTRL_PHY_TX0_TERM_OFFSET_SHIFT  16
#define PHY_CTRL_PHY_TX0_TERM_OFFSET_MASK   0x1f
#define PHY_TEST_PWR_DOWN                   BIT(0)

/* PARF_PHY_REFCLK register fields */
#define PHY_REFCLK_SSP_EN                   BIT(16)
#define PHY_REFCLK_USE_PAD                  BIT(12)

/* PARF_CONFIG_BITS register fields */
#define PHY_RX0_EQ_SHIFT                    24
#define PHY_RX0_EQ_MASK                     0x7

/* PARF_LTSSM register fields */
#define LTSSM_EN                            BIT(8)

/* PARF_INT_ALL_{STATUS/CLEAR/MASK} register fields */
#define PARF_INT_ALL_LINK_UP                BIT(13)
#define PARF_INT_MSI_DEV_0_7_SHIFT          23
#define PARF_INT_MSI_DEV_0_7_MASK           0xff

/* PARF_DEVICE_TYPE register fields */
#define DEVICE_TYPE_RC                      0x4

/* PARF_BDF_TO_SID_CFG fields */
#define BDF_TO_SID_BYPASS                   BIT(0)

/* PARF_MHI_CLOCK_RESET_CTRL register fields */
#define AHB_CLK_EN                          BIT(0)
#define MSTR_AXI_CLK_EN                     BIT(1)
#define BYPASS                              BIT(4)

/* PARF_AXI_MSTR_WR_ADDR_HALT register fields */
#define EN                                  BIT(31)

/* PARF_NO_SNOOP_OVERIDE register fields */
#define WR_NO_SNOOP_OVERIDE_EN              BIT(1)
#define RD_NO_SNOOP_OVERIDE_EN              BIT(3)

/* Default register values */
#define SLV_ADDR_SPACE_SZ                   0x80000000

/* Clock definitions */
enum qcs6490_pcie_clocks {
    QCS6490_PCIE_CLK_PIPE = 0,
    QCS6490_PCIE_CLK_AUX,
    QCS6490_PCIE_CLK_CFG,
    QCS6490_PCIE_CLK_BUS_MASTER,
    QCS6490_PCIE_CLK_BUS_SLAVE,
    QCS6490_PCIE_CLK_SLAVE_Q2A,
    QCS6490_PCIE_CLK_REF,
    QCS6490_PCIE_CLK_TBU,
    QCS6490_PCIE_CLK_DDRSS_SF_TBU,
    QCS6490_PCIE_CLK_COUNT
};

/* PHY register space size */
#define QCS6490_PCIE_PHY_REG_COUNT          64

struct QCS6490PCIeRoot {
    PCIDevice parent_obj;
};

typedef struct QCS6490PCIeHost {
    PCIExpressHost parent_obj;

    /* Memory regions */
    MemoryRegion io_mmio;
    MemoryRegion io_ioport;
    MemoryRegion parf_regs;
    MemoryRegion elbi_regs;
    MemoryRegion dbi_regs;
    MemoryRegion atu_regs;
    MemoryRegion mhi_regs;

    /* PARF Registers State */
    uint32_t parf_sys_ctrl;
    uint32_t parf_pm_ctrl;
    uint32_t parf_pcs_deemph;
    uint32_t parf_pcs_swing;
    uint32_t parf_phy_ctrl;
    uint32_t parf_phy_refclk;
    uint32_t parf_config_bits;
    uint32_t parf_dbi_base_addr;
    uint32_t parf_slv_addr_space_size;
    uint32_t parf_mhi_clock_reset_ctrl;
    uint32_t parf_axi_mstr_wr_addr_halt;
    uint32_t parf_axi_mstr_wr_addr_halt_v2;
    uint32_t parf_q2a_flush;
    uint32_t parf_ltssm;
    uint32_t parf_int_all_status;
    uint32_t parf_int_all_clear;
    uint32_t parf_int_all_mask;
    uint32_t parf_sid_offset;
    uint32_t parf_bdf_translate_cfg;
    uint32_t parf_dbi_base_addr_v2;
    uint32_t parf_dbi_base_addr_v2_hi;
    uint32_t parf_slv_addr_space_size_v2;
    uint32_t parf_slv_addr_space_size_v2_hi;
    uint32_t parf_no_snoop_overide;
    uint32_t parf_atu_base_addr;
    uint32_t parf_atu_base_addr_hi;
    uint32_t parf_device_type;
    uint32_t parf_bdf_to_sid_cfg;

    /* Clock simulation state */
    bool clocks[QCS6490_PCIE_CLK_COUNT];

    /* QMP PHY registers simulation */
    uint32_t phy_registers[QCS6490_PCIE_PHY_REG_COUNT];

    /* Link state */
    bool link_up;
    bool ltssm_enabled;

    /* IRQ state */
    qemu_irq irq[PCI_NUM_PINS];
    int irq_num[PCI_NUM_PINS];

    /* Child device */
    QCS6490PCIeRoot root;
} QCS6490PCIeHost;

/* Function declarations */
void qcs6490_pcie_set_irq_num(QCS6490PCIeHost *s, int index, int gsi);

#endif /* HW_PCI_HOST_QCS6490_PCIE_H */
