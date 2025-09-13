/*
 * QTest for Qualcomm PMK8350 Master PMIC device
 *
 * Copyright (c) 2025 Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This test exercises the PMK8350 Master PMIC device functionality,
 * including identification, GPIO control, power-on/off status, and ADC.
 */

#include "qemu/osdep.h"
#include "libqtest.h"

#define SPMI_CONTROLLER_BASE 0x0c440000
#define SPMI_ARB_REG_CHN_OFFSET 0x8000
#define PMK8350_SLAVE_ID 0
#define PMK8350_BASE_ADDR (SPMI_CONTROLLER_BASE + SPMI_ARB_REG_CHN_OFFSET + \
                           (PMK8350_SLAVE_ID * 0x1000))

/* PMIC identification registers */
#define PMIC_REV2         0x101
#define PMIC_REV3         0x102
#define PMIC_REV4         0x103
#define PMIC_TYPE         0x104
#define PMIC_SUBTYPE      0x105
#define PMIC_FAB_ID       0x1f2

/* Expected PMIC identification values */
#define PMIC_TYPE_VALUE   0x51
#define PMK8350_SUBTYPE_VALUE 0x2F

/* Common SPMI regulator register offsets */
#define SPMI_COMMON_REG_TYPE         0x04
#define SPMI_COMMON_REG_SUBTYPE      0x05
#define SPMI_COMMON_REG_VOLTAGE_RANGE 0x40
#define SPMI_COMMON_REG_VOLTAGE_SET  0x41
#define SPMI_COMMON_REG_MODE         0x45
#define SPMI_COMMON_REG_ENABLE       0x46

/* PMK8350 regulators (minimal set) - slave 0 range is 0x000-0xFFF */
#define PMK8350_LDO1_BASE 0x400
#define PMK8350_BOB_BASE  0x700

/* Power-on/off registers */
#define PMK8350_PON_BASE   0x800

/* GPIO registers - PMK8350 has only 4 GPIOs */
#define PMK8350_GPIO_BASE  0x900
#define PMK8350_GPIO_COUNT 4
#define PON_PON_REASON1    0x08
#define PON_PON_REASON2    0x09
#define PON_POFF_REASON1   0x0C
#define PON_POFF_REASON2   0x0D

/* VADC registers - slave 0 range is 0x000-0xFFF */
#define PMK8350_VADC_BASE  0xc00
#define VADC_STATUS1       0x08
#define VADC_MODE_CTL      0x40
#define VADC_EN_CTL1       0x46
#define VADC_CONV_REQ      0x52
#define VADC_DATA          0x60

static QTestState *qts;

static void test_pmk8350_identification(void)
{
    uint8_t type, subtype, rev2, rev3, rev4, fab_id;

    /* Read PMIC identification registers */
    type = qtest_readb(qts, PMK8350_BASE_ADDR + PMIC_TYPE);
    subtype = qtest_readb(qts, PMK8350_BASE_ADDR + PMIC_SUBTYPE);
    rev2 = qtest_readb(qts, PMK8350_BASE_ADDR + PMIC_REV2);
    rev3 = qtest_readb(qts, PMK8350_BASE_ADDR + PMIC_REV3);
    rev4 = qtest_readb(qts, PMK8350_BASE_ADDR + PMIC_REV4);
    fab_id = qtest_readb(qts, PMK8350_BASE_ADDR + PMIC_FAB_ID);

    /* Validate PMIC identification values */
    g_assert_cmpuint(type, ==, PMIC_TYPE_VALUE);
    g_assert_cmpuint(subtype, ==, PMK8350_SUBTYPE_VALUE);

    /* Log the identification for debugging */
    g_test_message("PMK8350 identification: type=0x%02x, subtype=0x%02x, "
                   "rev2=0x%02x, rev3=0x%02x, rev4=0x%02x, fab_id=0x%02x",
                   type, subtype, rev2, rev3, rev4, fab_id);
}

static void test_pmk8350_ldo1_regulator(void)
{
    uint32_t reg_base = PMK8350_BASE_ADDR + PMK8350_LDO1_BASE;
    uint8_t type, subtype, enable, mode;

    /* Read regulator type and subtype */
    type = qtest_readb(qts, reg_base + SPMI_COMMON_REG_TYPE);
    subtype = qtest_readb(qts, reg_base + SPMI_COMMON_REG_SUBTYPE);

    /* Read initial enable state (should be enabled for LDO1) */
    enable = qtest_readb(qts, reg_base + SPMI_COMMON_REG_ENABLE);
    g_assert_cmpuint(enable, ==, 0x80);

    /* Read mode */
    mode = qtest_readb(qts, reg_base + SPMI_COMMON_REG_MODE);
    g_assert_cmpuint(mode, ==, 0x07);

    g_test_message("PMK8350 LDO1: type=0x%02x, subtype=0x%02x, "
                   "enable=0x%02x, mode=0x%02x", type, subtype, enable, mode);
}

static void test_pmk8350_gpio_control(void)
{
    uint32_t gpio_base = PMK8350_BASE_ADDR + PMK8350_GPIO_BASE;
    uint8_t gpio_val;

    /* Test GPIO 0 (PMK8350 has only 4 GPIOs) */
    gpio_val = qtest_readb(qts, gpio_base + 0);

    /* Write a value to GPIO 0 */
    qtest_writeb(qts, gpio_base + 0, 0x55);
    /* Read back and verify */
    gpio_val = qtest_readb(qts, gpio_base + 0);
    g_assert_cmpuint(gpio_val, ==, 0x55);

    /* Test GPIO 3 (last GPIO) */
    qtest_writeb(qts, gpio_base + 3, 0xAA);
    gpio_val = qtest_readb(qts, gpio_base + 3);
    g_assert_cmpuint(gpio_val, ==, 0xAA);

    g_test_message("PMK8350 GPIO test passed (4 GPIOs available)");
}

static void test_pmk8350_power_on_reason(void)
{
    uint32_t pon_base = PMK8350_BASE_ADDR + PMK8350_PON_BASE;
    uint8_t pon_reason1, pon_reason2, poff_reason1, poff_reason2;

    /* Read power-on reasons */
    pon_reason1 = qtest_readb(qts, pon_base + PON_PON_REASON1);
    pon_reason2 = qtest_readb(qts, pon_base + PON_PON_REASON2);

    /* Read power-off reasons */
    poff_reason1 = qtest_readb(qts, pon_base + PON_POFF_REASON1);
    poff_reason2 = qtest_readb(qts, pon_base + PON_POFF_REASON2);

    /* Expected: normal power-on (power key pressed) */
    g_assert_cmpuint(pon_reason1, ==, 0x20);

    g_test_message("PMK8350 power reasons: pon1=0x%02x, pon2=0x%02x, "
                   "poff1=0x%02x, poff2=0x%02x",
                   pon_reason1, pon_reason2, poff_reason1, poff_reason2);
}

static void test_pmk8350_adc_conversion(void)
{
    uint32_t vadc_base = PMK8350_BASE_ADDR + PMK8350_VADC_BASE;
    uint8_t status, mode, enable;
    uint16_t data;
    int timeout;

    /* Configure ADC mode */
    qtest_writeb(qts, vadc_base + VADC_MODE_CTL, 0x01);
    mode = qtest_readb(qts, vadc_base + VADC_MODE_CTL);
    g_assert_cmpuint(mode, ==, 0x01);

    /* Enable ADC */
    qtest_writeb(qts, vadc_base + VADC_EN_CTL1, 0x80);
    enable = qtest_readb(qts, vadc_base + VADC_EN_CTL1);
    g_assert_cmpuint(enable, ==, 0x80);

    /* Trigger conversion */
    qtest_writeb(qts, vadc_base + VADC_CONV_REQ, 0x80);

    /* Wait for conversion completion with timeout */
    timeout = 1000; /* 1000 iterations = ~10ms timeout */
    do {
        g_usleep(10); /* 10 microseconds */
        status = qtest_readb(qts, vadc_base + VADC_STATUS1);
        timeout--;
    } while (!(status & 0x01) && timeout > 0);

    /* Verify conversion completed */
    g_assert_cmpuint(status & 0x01, ==, 0x01);

    /* Read conversion data */
    data = qtest_readb(qts, vadc_base + VADC_DATA);
    data |= (qtest_readb(qts, vadc_base + VADC_DATA + 1) << 8);

    /* Verify data is reasonable for PMK8350 (should be ~1.25V reference) */
    g_assert_cmpuint(data, >, 0x1000);  /* Should be around 0x1250 */
    g_assert_cmpuint(data, <, 0x1500);

    g_test_message("PMK8350 ADC conversion: status=0x%02x, data=0x%04x",
                   status, data);
}

static void test_pmk8350_master_pmic_features(void)
{
    /* Test that this is functioning as master PMIC */
    uint32_t reg_base = PMK8350_BASE_ADDR + PMK8350_LDO1_BASE;
    uint32_t bob_base = PMK8350_BASE_ADDR + PMK8350_BOB_BASE;
    uint8_t ldo1_enable, bob_enable;

    /* Check that reference regulator (LDO1) is enabled */
    ldo1_enable = qtest_readb(qts, reg_base + SPMI_COMMON_REG_ENABLE);
    g_assert_cmpuint(ldo1_enable, ==, 0x80);

    /* Check BOB regulator status */
    bob_enable = qtest_readb(qts, bob_base + SPMI_COMMON_REG_ENABLE);
    g_assert_cmpuint(bob_enable, ==, 0x80);

    g_test_message("PMK8350 master PMIC features verified");
}

static void test_pmk8350_read_only_protection(void)
{
    uint8_t original_type, modified_type;

    /* Try to write to read-only PMIC_TYPE register */
    original_type = qtest_readb(qts, PMK8350_BASE_ADDR + PMIC_TYPE);

    /* Attempt to modify (should be ignored) */
    qtest_writeb(qts, PMK8350_BASE_ADDR + PMIC_TYPE, 0xFF);

    /* Verify it wasn't changed */
    modified_type = qtest_readb(qts, PMK8350_BASE_ADDR + PMIC_TYPE);
    g_assert_cmpuint(modified_type, ==, original_type);

    g_test_message("PMK8350 read-only protection verified");
}

static void setup_qtest(void)
{
    qts = qtest_init("-machine qcs6490 -accel tcg");
}

static void teardown_qtest(void)
{
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/pmk8350/identification", test_pmk8350_identification);
    qtest_add_func("/pmk8350/ldo1-regulator", test_pmk8350_ldo1_regulator);
    qtest_add_func("/pmk8350/gpio-control", test_pmk8350_gpio_control);
    qtest_add_func("/pmk8350/power-on-reason", test_pmk8350_power_on_reason);
    qtest_add_func("/pmk8350/adc-conversion", test_pmk8350_adc_conversion);
    qtest_add_func("/pmk8350/master-pmic-features",
                   test_pmk8350_master_pmic_features);
    qtest_add_func("/pmk8350/read-only-protection",
                   test_pmk8350_read_only_protection);

    /* Setup and teardown for all tests */
    g_test_set_nonfatal_assertions();

    setup_qtest();
    int result = g_test_run();
    teardown_qtest();

    return result;
}
