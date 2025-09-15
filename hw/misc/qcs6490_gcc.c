/*
 * Qualcomm QCS6490 Global Clock Controller (GCC) Device Model
 *
 * Copyright (c) 2025 Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This device model implements the Global Clock Controller for QCS6490 SoC,
 * providing clock enable/disable, frequency configuration, and PLL management.
 */

#include "qemu/osdep.h"
#include "hw/misc/qcs6490_gcc.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"

/* Clock branch lookup table */
static const struct {
    uint32_t cbcr_offset;
    uint32_t cmd_rcgr_offset;
    uint32_t cfg_rcgr_offset;
    int branch_index;
    const char *name;
} gcc_clock_map[] = {
    /* QUP0 Serial Clocks */
    { GCC_QUPV3_WRAP0_S0_CLK_CBCR, GCC_QUPV3_WRAP0_S0_CMD_RCGR,
      GCC_QUPV3_WRAP0_S0_CFG_RCGR, 0, "qup0_s0" },
    { GCC_QUPV3_WRAP0_S1_CLK_CBCR, 0, 0, 1, "qup0_s1" },
    { GCC_QUPV3_WRAP0_S2_CLK_CBCR, 0, 0, 2, "qup0_s2" },
    { GCC_QUPV3_WRAP0_S3_CLK_CBCR, 0, 0, 3, "qup0_s3" },
    { GCC_QUPV3_WRAP0_S4_CLK_CBCR, 0, 0, 4, "qup0_s4" },
    { GCC_QUPV3_WRAP0_S5_CLK_CBCR, 0, 0, 5, "qup0_s5" },
    { GCC_QUPV3_WRAP0_S6_CLK_CBCR, 0, 0, 6, "qup0_s6" },
    { GCC_QUPV3_WRAP0_S7_CLK_CBCR, 0, 0, 7, "qup0_s7" },

    /* QUP1 Serial Clocks */
    { GCC_QUPV3_WRAP1_S0_CLK_CBCR, 0, 0, 8, "qup1_s0" },
    { GCC_QUPV3_WRAP1_S1_CLK_CBCR, 0, 0, 9, "qup1_s1" },
    { GCC_QUPV3_WRAP1_S2_CLK_CBCR, 0, 0, 10, "qup1_s2" },
    { GCC_QUPV3_WRAP1_S3_CLK_CBCR, 0, 0, 11, "qup1_s3" },
    { GCC_QUPV3_WRAP1_S4_CLK_CBCR, 0, 0, 12, "qup1_s4" },
    { GCC_QUPV3_WRAP1_S5_CLK_CBCR, 0, 0, 13, "qup1_s5" },

    /* PCIe Clocks */
    { GCC_PCIE_0_PIPE_CLK, 0, 0, 14, "pcie_0_pipe" },
    { GCC_PCIE_0_AUX_CLK, 0, 0, 15, "pcie_0_aux" },
    { GCC_PCIE_0_CFG_AHB_CLK, 0, 0, 16, "pcie_0_cfg_ahb" },
    { GCC_PCIE_0_MSTR_AXI_CLK, 0, 0, 17, "pcie_0_mstr_axi" },
    { GCC_PCIE_0_SLV_AXI_CLK, 0, 0, 18, "pcie_0_slv_axi" },
    { GCC_PCIE_0_SLV_Q2A_AXI_CLK, 0, 0, 19, "pcie_0_slv_q2a_axi" },
    { GCC_AGGRE_NOC_PCIE_TBU_CLK, 0, 0, 20, "aggre_noc_pcie_tbu" },
    { GCC_DDRSS_PCIE_SF_TBU_CLK, 0, 0, 21, "ddrss_pcie_sf_tbu" },
};

static int gcc_find_clock_branch(uint32_t offset)
{
    for (int i = 0; i < ARRAY_SIZE(gcc_clock_map); i++) {
        if (gcc_clock_map[i].cbcr_offset == offset ||
            gcc_clock_map[i].cmd_rcgr_offset == offset ||
            gcc_clock_map[i].cfg_rcgr_offset == offset) {
            return gcc_clock_map[i].branch_index;
        }
    }
    return -1;
}

static void gcc_update_clock_status(QCS6490GCCState *s, int branch_idx)
{
    GCCClockBranch *branch = &s->clock_branches[branch_idx];

    /* Update clock off status based on enable bit */
    if (branch->cbcr & CBCR_CLK_ENABLE) {
        branch->cbcr &= ~CBCR_CLK_OFF;  /* Clear OFF bit when enabled */
        branch->enabled = true;
    } else {
        branch->cbcr |= CBCR_CLK_OFF;   /* Set OFF bit when disabled */
        branch->enabled = false;
    }
}

static uint64_t qcs6490_gcc_read(void *opaque, hwaddr offset, unsigned size)
{
    QCS6490GCCState *s = QCS6490_GCC_DEV(opaque);
    uint64_t value = 0;
    int branch_idx;

    switch (offset) {
    /* GPLL0 PLL Registers */
    case GCC_GPLL0_MODE:
        value = s->gpll0_mode;
        break;
    case GCC_GPLL0_L_VAL:
        value = s->gpll0_l_val;
        break;
    case GCC_GPLL0_ALPHA_VAL:
        value = s->gpll0_alpha_val;
        break;
    case GCC_GPLL0_USER_CTL:
        value = s->gpll0_user_ctl;
        break;
    case GCC_GPLL0_CONFIG_CTL:
        value = s->gpll0_config_ctl;
        break;
    case GCC_GPLL0_STATUS:
        value = s->gpll0_status;
        break;

    /* Additional PLL registers */
    case GCC_GPLL_CTL:
        value = s->gpll_ctl;
        break;
    case GCC_GPLL_STATUS_COMPAT:
        value = s->gpll_status_compat;
        break;

    /* Always-on CBCR registers */
    case GCC_CAMERA_AHB_CBCR:
        value = s->camera_ahb_cbcr;
        break;
    case GCC_CAMERA_XO_CBCR:
        value = s->camera_xo_cbcr;
        break;
    case GCC_DISP_AHB_CBCR:
        value = s->disp_ahb_cbcr;
        break;
    case GCC_DISP_XO_CBCR:
        value = s->disp_xo_cbcr;
        break;
    case GCC_GPU_CFG_AHB_CBCR:
        value = s->gpu_cfg_ahb_cbcr;
        break;
    case GCC_VIDEO_AHB_CBCR:
        value = s->video_ahb_cbcr;
        break;
    case GCC_VIDEO_XO_CBCR:
        value = s->video_xo_cbcr;
        break;

    /* PCIe CBCR registers */
    case GCC_PCIE_0_PIPE_CLK:
        value = s->pcie_0_pipe_clk;
        break;
    case GCC_PCIE_0_AUX_CLK:
        value = s->pcie_0_aux_clk;
        break;
    case GCC_PCIE_0_CFG_AHB_CLK:
        value = s->pcie_0_cfg_ahb_clk;
        break;
    case GCC_PCIE_0_MSTR_AXI_CLK:
        value = s->pcie_0_mstr_axi_clk;
        break;
    case GCC_PCIE_0_SLV_AXI_CLK:
        value = s->pcie_0_slv_axi_clk;
        break;
    case GCC_PCIE_0_SLV_Q2A_AXI_CLK:
        value = s->pcie_0_slv_q2a_axi_clk;
        break;
    case GCC_AGGRE_NOC_PCIE_TBU_CLK:
        value = s->aggre_noc_pcie_tbu_clk;
        break;
    case GCC_DDRSS_PCIE_SF_TBU_CLK:
        value = s->ddrss_pcie_sf_tbu_clk;
        break;

    default:
        /* Check if this is a clock branch register */
        branch_idx = gcc_find_clock_branch(offset);
        if (branch_idx >= 0) {
            GCCClockBranch *branch = &s->clock_branches[branch_idx];

            /* Determine which register type based on offset */
            for (int i = 0; i < ARRAY_SIZE(gcc_clock_map); i++) {
                if (gcc_clock_map[i].branch_index == branch_idx) {
                    if (offset == gcc_clock_map[i].cbcr_offset) {
                        value = branch->cbcr;
                    } else if (offset == gcc_clock_map[i].cmd_rcgr_offset) {
                        value = branch->cmd_rcgr;
                    } else if (offset == gcc_clock_map[i].cfg_rcgr_offset) {
                        value = branch->cfg_rcgr;
                    }
                    break;
                }
            }
        } else {
            /* Unknown register - return 0 */
            value = 0;
        }
        break;
    }

    return value;
}

static void qcs6490_gcc_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned size)
{
    QCS6490GCCState *s = QCS6490_GCC_DEV(opaque);
    int branch_idx;

    switch (offset) {
    /* GPLL0 PLL Registers */
    case GCC_GPLL0_MODE:
        s->gpll0_mode = value & 0xFFFFFFFF;
        /* Update PLL lock status when mode changes */
        if (s->gpll0_mode & (PLL_MODE_OUTCTRL | PLL_MODE_BYPASSNL |
                             PLL_MODE_RESET_N)) {
            s->gpll0_status |= PLL_MODE_LOCK_DET;  /* Simulate PLL lock */
        } else {
            s->gpll0_status &= ~PLL_MODE_LOCK_DET;
        }
        break;
    case GCC_GPLL0_L_VAL:
        s->gpll0_l_val = value & 0xFFFF;  /* 16-bit L value */
        break;
    case GCC_GPLL0_ALPHA_VAL:
        s->gpll0_alpha_val = value & 0xFFFFFFFF;
        break;
    case GCC_GPLL0_USER_CTL:
        s->gpll0_user_ctl = value & 0xFFFFFFFF;
        break;
    case GCC_GPLL0_CONFIG_CTL:
        s->gpll0_config_ctl = value & 0xFFFFFFFF;
        break;

    /* Additional PLL registers */
    case GCC_GPLL_CTL:
        s->gpll_ctl = value & 0xFFFFFFFF;
        break;
    case GCC_GPLL_STATUS_COMPAT:
        s->gpll_status_compat = value & 0xFFFFFFFF;
        break;

    /* Always-on CBCR registers */
    case GCC_CAMERA_AHB_CBCR:
        s->camera_ahb_cbcr = value & 0xFFFFFFFF;
        break;
    case GCC_CAMERA_XO_CBCR:
        s->camera_xo_cbcr = value & 0xFFFFFFFF;
        break;
    case GCC_DISP_AHB_CBCR:
        s->disp_ahb_cbcr = value & 0xFFFFFFFF;
        break;
    case GCC_DISP_XO_CBCR:
        s->disp_xo_cbcr = value & 0xFFFFFFFF;
        break;
    case GCC_GPU_CFG_AHB_CBCR:
        s->gpu_cfg_ahb_cbcr = value & 0xFFFFFFFF;
        break;
    case GCC_VIDEO_AHB_CBCR:
        s->video_ahb_cbcr = value & 0xFFFFFFFF;
        break;
    case GCC_VIDEO_XO_CBCR:
        s->video_xo_cbcr = value & 0xFFFFFFFF;
        break;

    /* PCIe CBCR registers */
    case GCC_PCIE_0_PIPE_CLK:
        s->pcie_0_pipe_clk = value & 0xFFFFFFFF;
        break;
    case GCC_PCIE_0_AUX_CLK:
        s->pcie_0_aux_clk = value & 0xFFFFFFFF;
        break;
    case GCC_PCIE_0_CFG_AHB_CLK:
        s->pcie_0_cfg_ahb_clk = value & 0xFFFFFFFF;
        break;
    case GCC_PCIE_0_MSTR_AXI_CLK:
        s->pcie_0_mstr_axi_clk = value & 0xFFFFFFFF;
        break;
    case GCC_PCIE_0_SLV_AXI_CLK:
        s->pcie_0_slv_axi_clk = value & 0xFFFFFFFF;
        break;
    case GCC_PCIE_0_SLV_Q2A_AXI_CLK:
        s->pcie_0_slv_q2a_axi_clk = value & 0xFFFFFFFF;
        break;
    case GCC_AGGRE_NOC_PCIE_TBU_CLK:
        s->aggre_noc_pcie_tbu_clk = value & 0xFFFFFFFF;
        break;
    case GCC_DDRSS_PCIE_SF_TBU_CLK:
        s->ddrss_pcie_sf_tbu_clk = value & 0xFFFFFFFF;
        break;

    default:
        /* Check if this is a clock branch register */
        branch_idx = gcc_find_clock_branch(offset);
        if (branch_idx >= 0) {
            GCCClockBranch *branch = &s->clock_branches[branch_idx];

            /* Determine which register type based on offset */
            for (int i = 0; i < ARRAY_SIZE(gcc_clock_map); i++) {
                if (gcc_clock_map[i].branch_index == branch_idx) {
                    if (offset == gcc_clock_map[i].cbcr_offset) {
                        branch->cbcr = value & 0xFFFFFFFF;
                        gcc_update_clock_status(s, branch_idx);
                    } else if (offset == gcc_clock_map[i].cmd_rcgr_offset) {
                        branch->cmd_rcgr = value & 0xFFFFFFFF;
                        /* Clear UPDATE bit immediately for completion */
                        branch->cmd_rcgr &= ~RCG_CMD_UPDATE;
                    } else if (offset == gcc_clock_map[i].cfg_rcgr_offset) {
                        branch->cfg_rcgr = value & 0xFFFFFFFF;
                    }
                    break;
                }
            }
        }
        break;
    }
}

static const MemoryRegionOps qcs6490_gcc_ops = {
    .read = qcs6490_gcc_read,
    .write = qcs6490_gcc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void qcs6490_gcc_reset_enter(Object *obj, ResetType type)
{
    QCS6490GCCState *s = QCS6490_GCC_DEV(obj);

    /* Initialize GPLL0 PLL to enabled and locked state */
    s->gpll0_mode = PLL_MODE_OUTCTRL | PLL_MODE_BYPASSNL | PLL_MODE_RESET_N;
    s->gpll0_l_val = 0x1F;      /* L=31, typical for 800MHz */
    s->gpll0_alpha_val = 0x4000; /* Alpha value for fractional */
    s->gpll0_user_ctl = 0x0100; /* User control settings */
    s->gpll0_config_ctl = 0x20485699; /* Standard config */
    s->gpll0_status = PLL_MODE_LOCK_DET; /* PLL locked */

    /* Initialize additional PLL registers */
    s->gpll_ctl = 0x00000001;  /* PLL enabled */
    s->gpll_status_compat = PLL_MODE_LOCK_DET | (1 << 16) | (1 << 30);
    /* PLL locked and active */

    /* Initialize always-on CBCR registers - clocks enabled by default */
    s->camera_ahb_cbcr = CBCR_CLK_ENABLE;
    s->camera_xo_cbcr = CBCR_CLK_ENABLE;
    s->disp_ahb_cbcr = CBCR_CLK_ENABLE;
    s->disp_xo_cbcr = CBCR_CLK_ENABLE;
    s->gpu_cfg_ahb_cbcr = CBCR_CLK_ENABLE;
    s->video_ahb_cbcr = CBCR_CLK_ENABLE;
    s->video_xo_cbcr = CBCR_CLK_ENABLE;

    /* Initialize PCIe CBCR registers - start disabled */
    s->pcie_0_pipe_clk = CBCR_CLK_OFF;
    s->pcie_0_aux_clk = CBCR_CLK_OFF;
    s->pcie_0_cfg_ahb_clk = CBCR_CLK_OFF;
    s->pcie_0_mstr_axi_clk = CBCR_CLK_OFF;
    s->pcie_0_slv_axi_clk = CBCR_CLK_OFF;
    s->pcie_0_slv_q2a_axi_clk = CBCR_CLK_OFF;
    s->aggre_noc_pcie_tbu_clk = CBCR_CLK_OFF;
    s->ddrss_pcie_sf_tbu_clk = CBCR_CLK_OFF;

    /* Initialize clock branches */
    for (int i = 0; i < ARRAY_SIZE(gcc_clock_map); i++) {
        int branch_idx = gcc_clock_map[i].branch_index;
        GCCClockBranch *branch = &s->clock_branches[branch_idx];

        /* Initialize with clock enabled for critical UARTs and PCIe */
        if (branch_idx < 8) {  /* QUP0 clocks - needed for early boot */
            branch->cbcr = CBCR_CLK_ENABLE;  /* Clock enabled, OFF bit clear */
            branch->enabled = true;
        } else if (branch_idx >= 14 && branch_idx <= 21) {  /* PCIe clocks */
            branch->cbcr = CBCR_CLK_ENABLE;  /* PCIe clocks enabled */
            branch->enabled = true;
        } else {
            branch->cbcr = CBCR_CLK_OFF;     /* Clock disabled */
            branch->enabled = false;
        }

        branch->cmd_rcgr = RCG_CMD_ROOT_EN;  /* Root clock enabled */
        branch->cfg_rcgr = 0x0000;           /* Default config */
        branch->name = gcc_clock_map[i].name;
    }

    /* Always-on clocks */
    s->xo_clk_enabled = true;
    s->sleep_clk_enabled = true;
}

static void qcs6490_gcc_realize(DeviceState *dev, Error **errp)
{
    QCS6490GCCState *s = QCS6490_GCC_DEV(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &qcs6490_gcc_ops, s,
                          TYPE_QCS6490_GCC_DEV, QCS6490_GCC_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void qcs6490_gcc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = qcs6490_gcc_realize;
    dc->desc = "Qualcomm QCS6490 Global Clock Controller";
    rc->phases.enter = qcs6490_gcc_reset_enter;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo qcs6490_gcc_info = {
    .name = TYPE_QCS6490_GCC_DEV,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(QCS6490GCCState),
    .class_init = qcs6490_gcc_class_init,
};

static void qcs6490_gcc_register_types(void)
{
    type_register_static(&qcs6490_gcc_info);
}

type_init(qcs6490_gcc_register_types)
