/*
 * Qualcomm QCS6490 Global Clock Controller (GCC) Device Model
 *
 * Copyright (c) 2025 Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This device model implements the Global Clock Controller for QCS6490 SoC,
 * providing clock enable/disable, frequency configuration, and PLL management.
 */

#ifndef HW_MISC_QCS6490_GCC_H
#define HW_MISC_QCS6490_GCC_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_QCS6490_GCC_DEV "qcs6490-gcc"
OBJECT_DECLARE_SIMPLE_TYPE(QCS6490GCCState, QCS6490_GCC_DEV)

/* GCC Memory Map - Base: 0x00100000, Size: 2MB */
#define QCS6490_GCC_BASE_ADDR    0x00100000
#define QCS6490_GCC_SIZE         0x200000    /* 2MB */

/* Key Register Offsets */
#define GCC_GPLL0_MODE           0x00000     /* GPLL0 PLL Mode */
#define GCC_GPLL0_L_VAL          0x00004     /* GPLL0 L Value */
#define GCC_GPLL0_ALPHA_VAL      0x00008     /* GPLL0 Alpha Value */
#define GCC_GPLL0_USER_CTL       0x0000C     /* GPLL0 User Control */
#define GCC_GPLL0_CONFIG_CTL     0x00010     /* GPLL0 Config Control */
#define GCC_GPLL0_STATUS         0x00014     /* GPLL0 Status */

/* Additional registers for test compatibility */
#define GCC_GPLL_CTL             0x62000     /* General PLL Control */
#define GCC_GPLL_STATUS_COMPAT   0x62018     /* General PLL Status */

/* Always-on CBCR registers */
#define GCC_CAMERA_AHB_CBCR      0x36004
#define GCC_CAMERA_XO_CBCR       0x36020
#define GCC_DISP_AHB_CBCR        0x37004
#define GCC_DISP_XO_CBCR         0x3701c
#define GCC_GPU_CFG_AHB_CBCR     0x81004
#define GCC_VIDEO_AHB_CBCR       0x42004
#define GCC_VIDEO_XO_CBCR        0x42028

/* PCIe Clock Branch Control Registers */
#define GCC_PCIE_0_PIPE_CLK      0x7B018     /* PCIe 0 PIPE Clock */
#define GCC_PCIE_0_AUX_CLK       0x7B024     /* PCIe 0 AUX Clock */
#define GCC_PCIE_0_CFG_AHB_CLK   0x7B030     /* PCIe 0 Config AHB Clock */
#define GCC_PCIE_0_MSTR_AXI_CLK  0x7B03C     /* PCIe 0 Master AXI Clock */
#define GCC_PCIE_0_SLV_AXI_CLK   0x7B048     /* PCIe 0 Slave AXI Clock */
#define GCC_PCIE_0_SLV_Q2A_AXI_CLK 0x7B054   /* PCIe 0 Slave Q2A AXI Clock */
#define GCC_AGGRE_NOC_PCIE_TBU_CLK 0x16004   /* PCIe TBU Clock */
#define GCC_DDRSS_PCIE_SF_TBU_CLK  0x16010   /* PCIe SF TBU Clock */

/* UART Clock Branch Control Registers */
#define GCC_QUPV3_WRAP0_S0_CLK_CBCR    0x17144    /* QUP0 Serial 0 */
#define GCC_QUPV3_WRAP0_S1_CLK_CBCR    0x17274    /* QUP0 Serial 1 */
#define GCC_QUPV3_WRAP0_S2_CLK_CBCR    0x173A4    /* QUP0 Serial 2 */
#define GCC_QUPV3_WRAP0_S3_CLK_CBCR    0x174D4    /* QUP0 Serial 3 */
#define GCC_QUPV3_WRAP0_S4_CLK_CBCR    0x17604    /* QUP0 Serial 4 */
#define GCC_QUPV3_WRAP0_S5_CLK_CBCR    0x17734    /* QUP0 Serial 5 */
#define GCC_QUPV3_WRAP0_S6_CLK_CBCR    0x17864    /* QUP0 Serial 6 */
#define GCC_QUPV3_WRAP0_S7_CLK_CBCR    0x17994    /* QUP0 Serial 7 */

#define GCC_QUPV3_WRAP1_S0_CLK_CBCR    0x18144    /* QUP1 Serial 0 */
#define GCC_QUPV3_WRAP1_S1_CLK_CBCR    0x18274    /* QUP1 Serial 1 */
#define GCC_QUPV3_WRAP1_S2_CLK_CBCR    0x183A4    /* QUP1 Serial 2 */
#define GCC_QUPV3_WRAP1_S3_CLK_CBCR    0x184D4    /* QUP1 Serial 3 */
#define GCC_QUPV3_WRAP1_S4_CLK_CBCR    0x18604    /* QUP1 Serial 4 */
#define GCC_QUPV3_WRAP1_S5_CLK_CBCR    0x18734    /* QUP1 Serial 5 */

/* Root Clock Generator (RCG) Registers */
#define GCC_QUPV3_WRAP0_S0_CMD_RCGR    0x17148    /* QUP0 S0 Command */
#define GCC_QUPV3_WRAP0_S0_CFG_RCGR    0x1714C    /* QUP0 S0 Config */

/* Clock Branch Control Register (CBCR) Bit Fields */
#define CBCR_CLK_ENABLE              (1 << 0)   /* Clock Enable */
#define CBCR_CLK_OFF                 (1 << 31)  /* Clock Off Status */

/* PLL Mode Register Bit Fields */
#define PLL_MODE_OUTCTRL             (1 << 0)   /* PLL Output Control */
#define PLL_MODE_BYPASSNL            (1 << 1)   /* PLL Bypass */
#define PLL_MODE_RESET_N             (1 << 2)   /* PLL Reset */
#define PLL_MODE_LOCK_DET            (1 << 31)  /* PLL Lock Detect */

/* RCG Command Register Bit Fields */
#define RCG_CMD_UPDATE               (1 << 0)   /* Update in Progress */
#define RCG_CMD_ROOT_EN              (1 << 1)   /* Root Clock Enable */

/* Maximum number of clock branches */
#define GCC_MAX_CLOCK_BRANCHES       64

typedef struct {
    uint32_t cbcr;           /* Clock Branch Control Register */
    uint32_t cmd_rcgr;       /* Command RCG Register */
    uint32_t cfg_rcgr;       /* Config RCG Register */
    bool enabled;            /* Clock enable state */
    const char *name;        /* Clock name for debugging */
} GCCClockBranch;

struct QCS6490GCCState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    /* GPLL0 PLL Registers */
    uint32_t gpll0_mode;
    uint32_t gpll0_l_val;
    uint32_t gpll0_alpha_val;
    uint32_t gpll0_user_ctl;
    uint32_t gpll0_config_ctl;
    uint32_t gpll0_status;

    /* Additional PLL registers */
    uint32_t gpll_ctl;
    uint32_t gpll_status_compat;

    /* Always-on CBCR registers */
    uint32_t camera_ahb_cbcr;
    uint32_t camera_xo_cbcr;
    uint32_t disp_ahb_cbcr;
    uint32_t disp_xo_cbcr;
    uint32_t gpu_cfg_ahb_cbcr;
    uint32_t video_ahb_cbcr;
    uint32_t video_xo_cbcr;

    /* PCIe CBCR registers */
    uint32_t pcie_0_pipe_clk;
    uint32_t pcie_0_aux_clk;
    uint32_t pcie_0_cfg_ahb_clk;
    uint32_t pcie_0_mstr_axi_clk;
    uint32_t pcie_0_slv_axi_clk;
    uint32_t pcie_0_slv_q2a_axi_clk;
    uint32_t aggre_noc_pcie_tbu_clk;
    uint32_t ddrss_pcie_sf_tbu_clk;

    /* Clock Branches */
    GCCClockBranch clock_branches[GCC_MAX_CLOCK_BRANCHES];

    /* Always-on clocks */
    bool xo_clk_enabled;     /* Crystal Oscillator */
    bool sleep_clk_enabled;  /* Sleep Clock */
};

#endif /* HW_MISC_QCS6490_GCC_H */