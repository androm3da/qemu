/*
 * QTest testcase for QCS6490 GCC (Global Clock Controller)
 *
 * Copyright (c) Qualcomm Technologies, Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"

/* QCS6490 memory map */
#define QCS6490_GCC_BASE      0x00100000
#define QCS6490_GCC_SIZE      0x001f0000

/* Key GCC register offsets based on Linux gcc-sm8450.c driver */
#define GCC_GPLL_CTL          0x62000
#define GCC_GPLL_STATUS       0x62018

/* Always-on CBCR registers */
#define GCC_CAMERA_AHB_CBCR   0x36004
#define GCC_CAMERA_XO_CBCR    0x36020
#define GCC_DISP_AHB_CBCR     0x37004
#define GCC_DISP_XO_CBCR      0x3701c
#define GCC_GPU_CFG_AHB_CBCR  0x81004
#define GCC_VIDEO_AHB_CBCR    0x42004
#define GCC_VIDEO_XO_CBCR     0x42028

/* QUP UART RCG registers */
#define GCC_QUPV3_WRAP0_S0_CMD_RCGR  0x17148
#define GCC_QUPV3_WRAP0_S0_CFG_RCGR  0x1714C

/* QUP UART CBCR registers */
#define GCC_QUPV3_WRAP0_S0_CBCR      0x17144
#define GCC_QUPV3_WRAP0_S1_CBCR      0x17274
#define GCC_QUPV3_WRAP0_S5_CBCR      0x17734  /* Debug UART typically */

/* CBCR bit definitions */
#define CBCR_CLOCK_ENABLE     (1 << 0)
#define CBCR_HW_CTL          (1 << 1)
#define CBCR_CLK_ARES        (1 << 2)
#define CBCR_FORCE_MEM_CORE_ON (1 << 14)
#define CBCR_CLK_OFF         (1 << 31)

/* RCG CMD bit definitions */
#define RCG_CMD_UPDATE       (1 << 0)
#define RCG_CMD_ROOT_EN      (1 << 1)
#define RCG_CMD_DIRTY_CFG    (1 << 4)
#define RCG_CMD_ROOT_OFF     (1 << 31)

/* RCG CFG bit definitions */
#define RCG_CFG_SRC_SEL_MASK 0x700
#define RCG_CFG_SRC_SEL_SHIFT 8
#define RCG_CFG_SRC_DIV_MASK 0x1f

/* Clock source selections */
#define CLK_SRC_BI_TCXO      0
#define CLK_SRC_GPLL0_OUT_MAIN 1
#define CLK_SRC_GPLL0_OUT_EVEN 6

/* PLL status bits */
#define PLL_LOCK_DET         (1 << 16)
#define PLL_ACTIVE_FLAG      (1 << 30)
#define PLL_LOCK_STATUS      (1 << 31)

static void test_gcc_basic_registers(void)
{
    QTestState *qts = qtest_init("-machine qcs6490");
    uint32_t val;

    /* Test PLL status register - should show PLL as locked and active */
    val = qtest_readl(qts, QCS6490_GCC_BASE + GCC_GPLL_STATUS);
    g_assert(val & PLL_LOCK_DET);
    g_assert(val & PLL_ACTIVE_FLAG);
    g_assert(val & PLL_LOCK_STATUS);

    /* Test PLL control register - should be writable */
    qtest_writel(qts, QCS6490_GCC_BASE + GCC_GPLL_CTL, 0x12345678);
    val = qtest_readl(qts, QCS6490_GCC_BASE + GCC_GPLL_CTL);
    g_assert_cmpuint(val, ==, 0x12345678);

    qtest_quit(qts);
}

static void test_gcc_always_on_clocks(void)
{
    QTestState *qts = qtest_init("-machine qcs6490");
    uint32_t val;

    /* Test always-on clocks are enabled by default */
    val = qtest_readl(qts, QCS6490_GCC_BASE + GCC_CAMERA_AHB_CBCR);
    g_assert(val & CBCR_CLOCK_ENABLE);
    g_assert(!(val & CBCR_CLK_OFF));

    val = qtest_readl(qts, QCS6490_GCC_BASE + GCC_DISP_AHB_CBCR);
    g_assert(val & CBCR_CLOCK_ENABLE);
    g_assert(!(val & CBCR_CLK_OFF));

    val = qtest_readl(qts, QCS6490_GCC_BASE + GCC_GPU_CFG_AHB_CBCR);
    g_assert(val & CBCR_CLOCK_ENABLE);
    g_assert(!(val & CBCR_CLK_OFF));

    val = qtest_readl(qts, QCS6490_GCC_BASE + GCC_VIDEO_AHB_CBCR);
    g_assert(val & CBCR_CLOCK_ENABLE);
    g_assert(!(val & CBCR_CLK_OFF));

    qtest_quit(qts);
}

static void test_gcc_cbcr_enable_disable(void)
{
    QTestState *qts = qtest_init("-machine qcs6490");
    uint32_t val;

    /* Test clock enable/disable on a UART clock */
    /* Start with clock enabled (default state) */
    val = qtest_readl(qts, QCS6490_GCC_BASE + GCC_QUPV3_WRAP0_S0_CBCR);
    g_assert(val & CBCR_CLOCK_ENABLE);
    g_assert(!(val & CBCR_CLK_OFF));

    /* Disable clock */
    qtest_writel(qts, QCS6490_GCC_BASE + GCC_QUPV3_WRAP0_S0_CBCR, 0);
    val = qtest_readl(qts, QCS6490_GCC_BASE + GCC_QUPV3_WRAP0_S0_CBCR);
    g_assert(!(val & CBCR_CLOCK_ENABLE));
    g_assert(val & CBCR_CLK_OFF);

    /* Re-enable clock */
    qtest_writel(qts, QCS6490_GCC_BASE + GCC_QUPV3_WRAP0_S0_CBCR, CBCR_CLOCK_ENABLE);
    val = qtest_readl(qts, QCS6490_GCC_BASE + GCC_QUPV3_WRAP0_S0_CBCR);
    g_assert(val & CBCR_CLOCK_ENABLE);
    g_assert(!(val & CBCR_CLK_OFF));

    qtest_quit(qts);
}

static void test_gcc_rcg_configuration(void)
{
    QTestState *qts = qtest_init("-machine qcs6490");
    uint32_t cmd_val, cfg_val;

    /* Test RCG command register */
    cmd_val = qtest_readl(qts, QCS6490_GCC_BASE + GCC_QUPV3_WRAP0_S0_CMD_RCGR);
    g_assert(cmd_val & RCG_CMD_ROOT_EN);  /* Should be enabled by default */
    g_assert(!(cmd_val & RCG_CMD_UPDATE)); /* UPDATE should not be set initially */

    /* Test RCG config register - check default source selection */
    cfg_val = qtest_readl(qts, QCS6490_GCC_BASE + GCC_QUPV3_WRAP0_S0_CFG_RCGR);
    uint32_t src_sel = (cfg_val & RCG_CFG_SRC_SEL_MASK) >> RCG_CFG_SRC_SEL_SHIFT;
    g_assert_cmpuint(src_sel, ==, CLK_SRC_BI_TCXO); /* Default to 19.2MHz crystal */

    /* Test changing clock source */
    uint32_t new_cfg = (cfg_val & ~RCG_CFG_SRC_SEL_MASK) | 
                       (CLK_SRC_GPLL0_OUT_EVEN << RCG_CFG_SRC_SEL_SHIFT);
    qtest_writel(qts, QCS6490_GCC_BASE + GCC_QUPV3_WRAP0_S0_CFG_RCGR, new_cfg);
    
    /* Trigger update */
    qtest_writel(qts, QCS6490_GCC_BASE + GCC_QUPV3_WRAP0_S0_CMD_RCGR, 
                 RCG_CMD_ROOT_EN | RCG_CMD_UPDATE);
    
    /* Verify update completed (UPDATE bit should be cleared) */
    cmd_val = qtest_readl(qts, QCS6490_GCC_BASE + GCC_QUPV3_WRAP0_S0_CMD_RCGR);
    g_assert(!(cmd_val & RCG_CMD_UPDATE));
    g_assert(!(cmd_val & RCG_CMD_DIRTY_CFG));

    /* Verify config was applied */
    cfg_val = qtest_readl(qts, QCS6490_GCC_BASE + GCC_QUPV3_WRAP0_S0_CFG_RCGR);
    src_sel = (cfg_val & RCG_CFG_SRC_SEL_MASK) >> RCG_CFG_SRC_SEL_SHIFT;
    g_assert_cmpuint(src_sel, ==, CLK_SRC_GPLL0_OUT_EVEN);

    qtest_quit(qts);
}

static void test_gcc_uart_clocks(void)
{
    QTestState *qts = qtest_init("-machine qcs6490");
    uint32_t val;

    /* Test multiple UART clocks are enabled (critical for console) */
    val = qtest_readl(qts, QCS6490_GCC_BASE + GCC_QUPV3_WRAP0_S0_CBCR);
    g_assert(val & CBCR_CLOCK_ENABLE);

    val = qtest_readl(qts, QCS6490_GCC_BASE + GCC_QUPV3_WRAP0_S1_CBCR);
    g_assert(val & CBCR_CLOCK_ENABLE);

    /* Test debug UART (typically UART5) */
    val = qtest_readl(qts, QCS6490_GCC_BASE + GCC_QUPV3_WRAP0_S5_CBCR);
    g_assert(val & CBCR_CLOCK_ENABLE);
    g_assert(!(val & CBCR_CLK_OFF));

    qtest_quit(qts);
}

static void test_gcc_register_boundaries(void)
{
    QTestState *qts = qtest_init("-machine qcs6490");
    uint32_t val;

    /* Test reading from unimplemented registers returns 0 */
    val = qtest_readl(qts, QCS6490_GCC_BASE + 0x100000); /* Beyond implemented range */
    g_assert_cmpuint(val, ==, 0);

    /* Test that writes to unimplemented registers don't crash */
    qtest_writel(qts, QCS6490_GCC_BASE + 0x100000, 0xdeadbeef);
    val = qtest_readl(qts, QCS6490_GCC_BASE + 0x100000);
    g_assert_cmpuint(val, ==, 0); /* Should still read as 0 */

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/qcs6490/gcc/basic-registers", test_gcc_basic_registers);
    qtest_add_func("/qcs6490/gcc/always-on-clocks", test_gcc_always_on_clocks);
    qtest_add_func("/qcs6490/gcc/cbcr-enable-disable", test_gcc_cbcr_enable_disable);
    qtest_add_func("/qcs6490/gcc/rcg-configuration", test_gcc_rcg_configuration);
    qtest_add_func("/qcs6490/gcc/uart-clocks", test_gcc_uart_clocks);
    qtest_add_func("/qcs6490/gcc/register-boundaries", test_gcc_register_boundaries);

    return g_test_run();
}