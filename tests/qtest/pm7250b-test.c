/*
 * PM7250B PMIC test
 *
 * Copyright (c) 2025 Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/module.h"

#define SPMI_CONTROLLER_BASE 0x0c440000
#define SPMI_ARB_REG_CHN_OFFSET 0x8000
#define PM7250B_SLAVE_ID 3
#define PM7250B_BASE (SPMI_CONTROLLER_BASE + SPMI_ARB_REG_CHN_OFFSET + \
                      (PM7250B_SLAVE_ID * 0x1000))
/* PM7250B registers - slave 3 range is 0x000-0xFFF */
#define PM7250B_CHGR_BASE  (PM7250B_BASE + 0x200)
#define PM7250B_L1_BASE    (PM7250B_BASE + 0x400)
#define PM7250B_VADC_BASE  (PM7250B_BASE + 0x600)

static void test_pm7250b_identification(void)
{
    QTestState *qts = qtest_init("-machine qcs6490");

    /* Test PMIC identification registers */
    uint8_t type = qtest_readb(qts, PM7250B_BASE + 0x104);
    uint8_t subtype = qtest_readb(qts, PM7250B_BASE + 0x105);
    uint8_t rev2 = qtest_readb(qts, PM7250B_BASE + 0x101);
    uint8_t rev3 = qtest_readb(qts, PM7250B_BASE + 0x102);
    uint8_t fab_id = qtest_readb(qts, PM7250B_BASE + 0x1f2);

    g_assert_cmpuint(type, ==, 0x51);
    g_assert_cmpuint(subtype, ==, 0x32);

    g_test_message("PM7250B identification: type=0x%02x, subtype=0x%02x, "
                   "rev2=0x%02x, rev3=0x%02x, fab_id=0x%02x",
                   type, subtype, rev2, rev3, fab_id);

    qtest_quit(qts);
}

static void test_pm7250b_battery_charger(void)
{
    QTestState *qts = qtest_init("-machine qcs6490");

    /* Test battery charger status registers */
    uint8_t status1 = qtest_readb(qts, PM7250B_CHGR_BASE + 0x06);
    uint8_t status2 = qtest_readb(qts, PM7250B_CHGR_BASE + 0x07);
    uint8_t enable_cmd = qtest_readb(qts, PM7250B_CHGR_BASE + 0x42);

    g_assert_cmpuint(status1, ==, 0x01);  /* Battery present */
    g_assert_cmpuint(status2, ==, 0x08);  /* Valid input voltage */
    g_assert_cmpuint(enable_cmd, ==, 0x00);  /* Charger disabled initially */

    /* Test charger enable */
    qtest_writeb(qts, PM7250B_CHGR_BASE + 0x42, 0x01);
    enable_cmd = qtest_readb(qts, PM7250B_CHGR_BASE + 0x42);
    g_assert_cmpuint(enable_cmd, ==, 0x01);

    g_test_message("PM7250B charger: status1=0x%02x, status2=0x%02x, "
                   "enable=0x%02x", status1, status2, enable_cmd);

    qtest_quit(qts);
}

static void test_pm7250b_regulators(void)
{
    QTestState *qts = qtest_init("-machine qcs6490");

    /* Test L1 regulator (1.2V digital) */
    uint8_t type = qtest_readb(qts, PM7250B_L1_BASE + 0x04);
    uint8_t enable = qtest_readb(qts, PM7250B_L1_BASE + 0x46);
    uint8_t voltage_set = qtest_readb(qts, PM7250B_L1_BASE + 0x41);

    g_assert_cmpuint(type, ==, 0x04);  /* LDO type */
    g_assert_cmpuint(enable, ==, 0x80);  /* Enabled */
    g_assert_cmpuint(voltage_set, ==, 0x20);  /* 1.2V */

    /* Test regulator disable */
    qtest_writeb(qts, PM7250B_L1_BASE + 0x46, 0x00);
    enable = qtest_readb(qts, PM7250B_L1_BASE + 0x46);
    g_assert_cmpuint(enable, ==, 0x00);

    g_test_message("PM7250B L1 regulator: type=0x%02x, enable=0x%02x, "
                   "vset=0x%02x", type, enable, voltage_set);

    qtest_quit(qts);
}

static void test_pm7250b_adc_conversion(void)
{
    QTestState *qts = qtest_init("-machine qcs6490");

    /* Test ADC conversion sequence */
    /* Select channel 1 (VBAT) */
    qtest_writeb(qts, PM7250B_VADC_BASE + 0x48, 0x01);
    qtest_writeb(qts, PM7250B_VADC_BASE + 0x50, 0x01);  /* Enable interrupt */
    qtest_writeb(qts, PM7250B_VADC_BASE + 0x52, 0x80);  /* Start conversion */

    /* Wait for conversion to complete */
    qtest_clock_step(qts, 8000000);  /* 8ms */

    uint8_t status = qtest_readb(qts, PM7250B_VADC_BASE + 0x08);
    g_assert_cmpuint(status & 0x01, ==, 0x01);  /* EOC bit set */

    uint8_t data_low = qtest_readb(qts, PM7250B_VADC_BASE + 0x60);
    uint8_t data_high = qtest_readb(qts, PM7250B_VADC_BASE + 0x61);
    uint16_t data = (data_high << 8) | data_low;

    g_assert_cmpuint(data, >, 0x3000);  /* Should be battery voltage ~3.8V */

    /* Status should be cleared after data read */
    status = qtest_readb(qts, PM7250B_VADC_BASE + 0x08);
    g_assert_cmpuint(status & 0x01, ==, 0x00);

    g_test_message("PM7250B ADC conversion: status=0x%02x, data=0x%04x",
                   status, data);

    qtest_quit(qts);
}

static void test_pm7250b_gpio_control(void)
{
    QTestState *qts = qtest_init("-machine qcs6490");

    /* Test GPIO register access */
    qtest_writeb(qts, PM7250B_BASE + 0x800, 0x80);  /* Enable GPIO 0 */
    uint8_t gpio_val = qtest_readb(qts, PM7250B_BASE + 0x800);
    g_assert_cmpuint(gpio_val, ==, 0x80);

    /* Test multiple GPIOs (PM7250B has 12) */
    for (int i = 0; i < 12; i++) {
        qtest_writeb(qts, PM7250B_BASE + 0x800 + i, 0x40 | i);
        uint8_t val = qtest_readb(qts, PM7250B_BASE + 0x800 + i);
        g_assert_cmpuint(val, ==, 0x40 | i);
    }

    g_test_message("PM7250B GPIO test passed (12 GPIOs available)");

    qtest_quit(qts);
}

static void test_pm7250b_read_only_protection(void)
{
    QTestState *qts = qtest_init("-machine qcs6490");

    /* Test that ID registers are read-only */
    uint8_t original_type = qtest_readb(qts, PM7250B_BASE + 0x104);
    qtest_writeb(qts, PM7250B_BASE + 0x104, 0xFF);  /* Try to modify */
    uint8_t type_after = qtest_readb(qts, PM7250B_BASE + 0x104);
    g_assert_cmpuint(type_after, ==, original_type);  /* Should be unchanged */

    g_test_message("PM7250B read-only protection verified");

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/aarch64/pm7250b/identification",
                   test_pm7250b_identification);
    qtest_add_func("/aarch64/pm7250b/battery-charger",
                   test_pm7250b_battery_charger);
    qtest_add_func("/aarch64/pm7250b/regulators", test_pm7250b_regulators);
    qtest_add_func("/aarch64/pm7250b/adc-conversion",
                   test_pm7250b_adc_conversion);
    qtest_add_func("/aarch64/pm7250b/gpio-control", test_pm7250b_gpio_control);
    qtest_add_func("/aarch64/pm7250b/read-only-protection",
                   test_pm7250b_read_only_protection);

    return g_test_run();
}
