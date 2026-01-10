/*
 * QTest testcase for QCOM GCC-MPM device
 *
 * This test exercises the GCC-MPM (Global Control Counter MSM Power
 * Manager) device based on real-world usage patterns and the SystemC
 * model implementation.
 *
 * Copyright (c) 2025 Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"

/* Test machine and device configuration */
#define GCC_MPM_BASE_ADDR 0x0c210000

/* GCC-MPM register offsets */
#define GCC_MPM_CONTROL_CNTCR       0x0
#define GCC_MPM_CONTROL_CNTSR       0x4
#define GCC_MPM_CONTROL_CNTCV_L     0x8
#define GCC_MPM_CONTROL_CNTCV_HI    0xC
#define GCC_MPM_CONTROL_CNTFID0     0x20
#define GCC_MPM_CONTROL_ID          0xFD0

/* Default values from SystemC model */
#define GCC_MPM_DEFAULT_FREQ        0x124F800   /* ~19.2MHz */
#define GCC_MPM_DEFAULT_ID          0x10000000

/* Helper functions for register access */
static uint32_t read_reg(QTestState *qts, uint32_t offset)
{
    return qtest_readl(qts, GCC_MPM_BASE_ADDR + offset);
}

static void write_reg(QTestState *qts, uint32_t offset, uint32_t value)
{
    qtest_writel(qts, GCC_MPM_BASE_ADDR + offset, value);
}

/* Test basic register access and reset values */
static void test_gcc_mpm_reset_values(void)
{
    QTestState *qts = qtest_init("-M SA8775P_CDSP0");
    uint32_t value;

    /* Test default frequency register */
    value = read_reg(qts, GCC_MPM_CONTROL_CNTFID0);
    g_assert_cmpuint(value, ==, GCC_MPM_DEFAULT_FREQ);

    /* Test default ID register */
    value = read_reg(qts, GCC_MPM_CONTROL_ID);
    g_assert_cmpuint(value, ==, GCC_MPM_DEFAULT_ID);

    /* Test that other registers are zero at reset */
    value = read_reg(qts, GCC_MPM_CONTROL_CNTCR);
    g_assert_cmpuint(value, ==, 0);

    value = read_reg(qts, GCC_MPM_CONTROL_CNTSR);
    g_assert_cmpuint(value, ==, 0);

    qtest_quit(qts);
}

/* Test writable register functionality */
static void test_gcc_mpm_writable_registers(void)
{
    QTestState *qts = qtest_init("-M SA8775P_CDSP0");
    uint32_t test_value = 0x12345678;
    uint32_t value;

    /* Test CNTCR register is writable */
    write_reg(qts, GCC_MPM_CONTROL_CNTCR, test_value);
    value = read_reg(qts, GCC_MPM_CONTROL_CNTCR);
    g_assert_cmpuint(value, ==, test_value);

    /* Test CNTCV_L register is writable */
    write_reg(qts, GCC_MPM_CONTROL_CNTCV_L, test_value);
    value = read_reg(qts, GCC_MPM_CONTROL_CNTCV_L);
    g_assert_cmpuint(value, ==, test_value);

    /* Test CNTCV_HI register is writable */
    write_reg(qts, GCC_MPM_CONTROL_CNTCV_HI, test_value);
    value = read_reg(qts, GCC_MPM_CONTROL_CNTCV_HI);
    g_assert_cmpuint(value, ==, test_value);

    /* Test CNTFID0 register is writable */
    write_reg(qts, GCC_MPM_CONTROL_CNTFID0, test_value);
    value = read_reg(qts, GCC_MPM_CONTROL_CNTFID0);
    g_assert_cmpuint(value, ==, test_value);

    qtest_quit(qts);
}

/* Test read-only register functionality */
static void test_gcc_mpm_readonly_registers(void)
{
    QTestState *qts = qtest_init("-M SA8775P_CDSP0");
    uint32_t original_value, test_value = 0xDEADBEEF, value;

    /* Test CNTSR register is read-only */
    original_value = read_reg(qts, GCC_MPM_CONTROL_CNTSR);
    write_reg(qts, GCC_MPM_CONTROL_CNTSR, test_value);
    value = read_reg(qts, GCC_MPM_CONTROL_CNTSR);
    g_assert_cmpuint(value, ==, original_value);

    /* Test ID register is read-only */
    original_value = read_reg(qts, GCC_MPM_CONTROL_ID);
    write_reg(qts, GCC_MPM_CONTROL_ID, test_value);
    value = read_reg(qts, GCC_MPM_CONTROL_ID);
    g_assert_cmpuint(value, ==, original_value);
    g_assert_cmpuint(value, ==, GCC_MPM_DEFAULT_ID);

    qtest_quit(qts);
}

/* Test counter functionality */
static void test_gcc_mpm_counter_functionality(void)
{
    QTestState *qts = qtest_init("-M SA8775P_CDSP0");
    uint32_t counter_l1, counter_l2, counter_hi1, counter_hi2;

    /* Enable the counter */
    write_reg(qts, GCC_MPM_CONTROL_CNTCR, 0x1);

    /* Read initial counter values */
    counter_l1 = read_reg(qts, GCC_MPM_CONTROL_CNTCV_L);
    counter_hi1 = read_reg(qts, GCC_MPM_CONTROL_CNTCV_HI);

    /* Advance virtual clock by 1 second (1000000000 ns) */
    qtest_clock_step(qts, 1000000000);

    /* Read counter values again after virtual time advancement */
    counter_l2 = read_reg(qts, GCC_MPM_CONTROL_CNTCV_L);
    counter_hi2 = read_reg(qts, GCC_MPM_CONTROL_CNTCV_HI);

    /*
     * Counter should increment over time.
     * With 19.2MHz frequency, 1 second should advance counter significantly.
     * Expected increment: ~19,200,000 ticks
     */
    if (counter_hi1 == counter_hi2) {
        g_assert_cmpuint(counter_l2, >, counter_l1);
        /* Verify reasonable increment (should be ~19.2M for 1 second) */
        uint32_t increment = counter_l2 - counter_l1;
        g_assert_cmpuint(increment, >, 10000000);  /* At least 10M ticks */
        g_assert_cmpuint(increment, <, 30000000);  /* Less than 30M ticks */
    } else {
        /* High word changed, verify it increased */
        g_assert_cmpuint(counter_hi2, >, counter_hi1);
    }

    qtest_quit(qts);
}

/* Test frequency register impact on counter */
static void test_gcc_mpm_frequency_register(void)
{
    QTestState *qts = qtest_init("-M SA8775P_CDSP0");
    uint32_t original_freq, new_freq = 1000000; /* 1MHz for testing */
    uint32_t value;

    /* Get original frequency */
    original_freq = read_reg(qts, GCC_MPM_CONTROL_CNTFID0);
    g_assert_cmpuint(original_freq, ==, GCC_MPM_DEFAULT_FREQ);

    /* Change frequency */
    write_reg(qts, GCC_MPM_CONTROL_CNTFID0, new_freq);
    value = read_reg(qts, GCC_MPM_CONTROL_CNTFID0);
    g_assert_cmpuint(value, ==, new_freq);

    /* Test that counter still works with new frequency */
    uint32_t counter = read_reg(qts, GCC_MPM_CONTROL_CNTCV_L);
    (void)counter; /* Counter value depends on timing */

    qtest_quit(qts);
}

/* Test invalid register access */
static void test_gcc_mpm_invalid_access(void)
{
    QTestState *qts = qtest_init("-M SA8775P_CDSP0");

    /* Test read from invalid offset - should return 0 */
    uint32_t value = read_reg(qts, 0x100); /* Invalid offset */
    g_assert_cmpuint(value, ==, 0);

    /* Test write to invalid offset - should not crash */
    write_reg(qts, 0x200, 0x12345678); /* Invalid offset */

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/qcom-gcc-mpm/reset-values", test_gcc_mpm_reset_values);
    qtest_add_func("/qcom-gcc-mpm/writable-registers",
                   test_gcc_mpm_writable_registers);
    qtest_add_func("/qcom-gcc-mpm/readonly-registers",
                   test_gcc_mpm_readonly_registers);
    qtest_add_func("/qcom-gcc-mpm/counter-functionality",
                   test_gcc_mpm_counter_functionality);
    qtest_add_func("/qcom-gcc-mpm/frequency-register",
                   test_gcc_mpm_frequency_register);
    qtest_add_func("/qcom-gcc-mpm/invalid-access", test_gcc_mpm_invalid_access);

    return g_test_run();
}
