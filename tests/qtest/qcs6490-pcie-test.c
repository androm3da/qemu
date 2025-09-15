/*
 * QTest testcase for QCS6490 PCIe Controller
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"

/* QCS6490 memory map */
#define QCS6490_PCIE_BASE     0x01c08000

/* PARF (PCIe Auxiliary Register Framework) register offsets */
#define PARF_SYS_CTRL         0x00
#define PARF_PM_CTRL          0x20
#define PARF_PCS_DEEMPH       0x34
#define PARF_PCS_SWING        0x38
#define PARF_PHY_CTRL         0x40
#define PARF_PHY_REFCLK       0x4c
#define PARF_CONFIG_BITS      0x50
#define PARF_LTSSM            0x1b0
#define PARF_INT_ALL_STATUS   0x224
#define PARF_INT_ALL_CLEAR    0x228
#define PARF_INT_ALL_MASK     0x22c
#define PARF_MHI_CLOCK_RESET_CTRL 0x174
#define PARF_DEVICE_TYPE      0x1000
#define PARF_BDF_TO_SID_CFG   0x2c00

/* Register field definitions */
#define PHY_TEST_PWR_DOWN     (1 << 0)
#define LTSSM_EN              (1 << 8)
#define PARF_INT_ALL_LINK_UP  (1 << 13)
#define DEVICE_TYPE_RC        0x4
#define BDF_TO_SID_BYPASS     (1 << 0)
#define AHB_CLK_EN            (1 << 0)
#define MSTR_AXI_CLK_EN       (1 << 1)

/* Helper to create QCS6490 test instance */
static QTestState *qtest_init_qcs6490(void)
{
    return qtest_init("-machine qcs6490");
}

static void test_parf_register_reset_values(void)
{
    QTestState *qts = qtest_init_qcs6490();
    uint32_t val;

    /* Test PHY_CTRL reset value (should have PHY_TEST_PWR_DOWN set) */
    val = qtest_readl(qts, QCS6490_PCIE_BASE + PARF_PHY_CTRL);
    g_assert(val & PHY_TEST_PWR_DOWN);

    /* Test DEVICE_TYPE reset value (should be Root Complex) */
    val = qtest_readl(qts, QCS6490_PCIE_BASE + PARF_DEVICE_TYPE);
    g_assert_cmpuint(val, ==, DEVICE_TYPE_RC);

    /* Test BDF_TO_SID_CFG reset value (should have bypass enabled) */
    val = qtest_readl(qts, QCS6490_PCIE_BASE + PARF_BDF_TO_SID_CFG);
    g_assert(val & BDF_TO_SID_BYPASS);

    /* Test LTSSM reset value (should be disabled) */
    val = qtest_readl(qts, QCS6490_PCIE_BASE + PARF_LTSSM);
    g_assert_cmpuint(val & LTSSM_EN, ==, 0);

    /* Test interrupt status reset value (should be clear) */
    val = qtest_readl(qts, QCS6490_PCIE_BASE + PARF_INT_ALL_STATUS);
    g_assert_cmpuint(val & PARF_INT_ALL_LINK_UP, ==, 0);

    qtest_quit(qts);
}

static void test_parf_register_read_write(void)
{
    QTestState *qts = qtest_init_qcs6490();
    uint32_t val;
    uint32_t test_val = 0x12345678;

    /* Test read/write to SYS_CTRL register */
    qtest_writel(qts, QCS6490_PCIE_BASE + PARF_SYS_CTRL, test_val);
    val = qtest_readl(qts, QCS6490_PCIE_BASE + PARF_SYS_CTRL);
    g_assert_cmpuint(val, ==, test_val);

    /* Test read/write to PM_CTRL register */
    qtest_writel(qts, QCS6490_PCIE_BASE + PARF_PM_CTRL, test_val);
    val = qtest_readl(qts, QCS6490_PCIE_BASE + PARF_PM_CTRL);
    g_assert_cmpuint(val, ==, test_val);

    /* Test read/write to PCS_DEEMPH register */
    qtest_writel(qts, QCS6490_PCIE_BASE + PARF_PCS_DEEMPH, test_val);
    val = qtest_readl(qts, QCS6490_PCIE_BASE + PARF_PCS_DEEMPH);
    g_assert_cmpuint(val, ==, test_val);

    /* Test read/write to PCS_SWING register */
    qtest_writel(qts, QCS6490_PCIE_BASE + PARF_PCS_SWING, test_val);
    val = qtest_readl(qts, QCS6490_PCIE_BASE + PARF_PCS_SWING);
    g_assert_cmpuint(val, ==, test_val);

    qtest_quit(qts);
}

static void test_ltssm_enable_link_simulation(void)
{
    QTestState *qts = qtest_init_qcs6490();
    uint32_t val;

    /* Initially LTSSM should be disabled and link down */
    val = qtest_readl(qts, QCS6490_PCIE_BASE + PARF_LTSSM);
    g_assert_cmpuint(val & LTSSM_EN, ==, 0);

    val = qtest_readl(qts, QCS6490_PCIE_BASE + PARF_INT_ALL_STATUS);
    g_assert_cmpuint(val & PARF_INT_ALL_LINK_UP, ==, 0);

    /* Enable LTSSM */
    qtest_writel(qts, QCS6490_PCIE_BASE + PARF_LTSSM, LTSSM_EN);

    /* Verify LTSSM is enabled */
    val = qtest_readl(qts, QCS6490_PCIE_BASE + PARF_LTSSM);
    g_assert(val & LTSSM_EN);

    /* Check that link comes up */
    val = qtest_readl(qts, QCS6490_PCIE_BASE + PARF_INT_ALL_STATUS);
    g_assert(val & PARF_INT_ALL_LINK_UP);

    /* Disable LTSSM */
    qtest_writel(qts, QCS6490_PCIE_BASE + PARF_LTSSM, 0);

    /* Verify link goes down */
    val = qtest_readl(qts, QCS6490_PCIE_BASE + PARF_INT_ALL_STATUS);
    g_assert_cmpuint(val & PARF_INT_ALL_LINK_UP, ==, 0);

    qtest_quit(qts);
}

static void test_interrupt_clear_functionality(void)
{
    QTestState *qts = qtest_init_qcs6490();
    uint32_t val;

    /* Enable LTSSM to generate link up interrupt */
    qtest_writel(qts, QCS6490_PCIE_BASE + PARF_LTSSM, LTSSM_EN);

    /* Verify link up interrupt is set */
    val = qtest_readl(qts, QCS6490_PCIE_BASE + PARF_INT_ALL_STATUS);
    g_assert(val & PARF_INT_ALL_LINK_UP);

    /* Clear the interrupt */
    qtest_writel(qts, QCS6490_PCIE_BASE + PARF_INT_ALL_CLEAR,
                 PARF_INT_ALL_LINK_UP);

    /* Verify interrupt is cleared */
    val = qtest_readl(qts, QCS6490_PCIE_BASE + PARF_INT_ALL_STATUS);
    g_assert_cmpuint(val & PARF_INT_ALL_LINK_UP, ==, 0);

    qtest_quit(qts);
}

static void test_vendor_device_id(void)
{
    QTestState *qts = qtest_init_qcs6490();
    uint32_t val;

    /* Read vendor/device ID from ECAM space at 0x01c00000 */
    val = qtest_readl(qts, 0x01c00000);

    /* Verify Qualcomm vendor ID (0x17cb) and SM8250 device ID (0x010b) */
    g_assert_cmpuint(val & 0xFFFF, ==, 0x17cb);        /* Vendor ID */
    g_assert_cmpuint((val >> 16) & 0xFFFF, ==, 0x010b); /* Device ID */

    qtest_quit(qts);
}

static void test_clock_management_simulation(void)
{
    QTestState *qts = qtest_init_qcs6490();
    uint32_t val;

    /* Read initial MHI clock/reset control register */
    val = qtest_readl(qts, QCS6490_PCIE_BASE + PARF_MHI_CLOCK_RESET_CTRL);
    g_assert_cmpuint(val, ==, 0);

    /* Enable AHB and Master AXI clocks */
    qtest_writel(qts, QCS6490_PCIE_BASE + PARF_MHI_CLOCK_RESET_CTRL,
                 AHB_CLK_EN | MSTR_AXI_CLK_EN);

    /* Verify clocks are enabled */
    val = qtest_readl(qts, QCS6490_PCIE_BASE + PARF_MHI_CLOCK_RESET_CTRL);
    g_assert(val & AHB_CLK_EN);
    g_assert(val & MSTR_AXI_CLK_EN);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/aarch64/qcs6490-pcie/parf-register-reset-values",
                   test_parf_register_reset_values);
    qtest_add_func("/aarch64/qcs6490-pcie/parf-register-read-write",
                   test_parf_register_read_write);
    qtest_add_func("/aarch64/qcs6490-pcie/ltssm-enable-link-simulation",
                   test_ltssm_enable_link_simulation);
    qtest_add_func("/aarch64/qcs6490-pcie/interrupt-clear-functionality",
                   test_interrupt_clear_functionality);
    qtest_add_func("/aarch64/qcs6490-pcie/vendor-device-id",
                   test_vendor_device_id);
    qtest_add_func("/aarch64/qcs6490-pcie/clock-management-simulation",
                   test_clock_management_simulation);

    return g_test_run();
}
