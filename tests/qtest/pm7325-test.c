/*
 * QTest for Qualcomm PM7325 PMIC device
 *
 * Copyright (c) 2025 Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This test exercises the PM7325 PMIC device the same way that the Linux
 * kernel does, validating PMIC identification, regulator functionality,
 * GPIO control, temperature alarm, and ADC conversion features.
 */

#include "qemu/osdep.h"
#include "libqtest.h"

#define SPMI_CONTROLLER_BASE 0x0c440000
#define SPMI_ARB_REG_CHN_OFFSET 0x8000
#define PM7325_SLAVE_ID 1
#define PM7325_BASE_ADDR (SPMI_CONTROLLER_BASE + SPMI_ARB_REG_CHN_OFFSET + \
                          (PM7325_SLAVE_ID * 0x1000))

/* PMIC identification registers */
#define PMIC_REV2         0x101
#define PMIC_REV3         0x102
#define PMIC_REV4         0x103
#define PMIC_TYPE         0x104
#define PMIC_SUBTYPE      0x105
#define PMIC_FAB_ID       0x1f2

/* Expected PMIC identification values */
#define PMIC_TYPE_VALUE   0x51
#define PM7325_SUBTYPE_VALUE 0x2C

/* Common SPMI regulator register offsets */
#define SPMI_COMMON_REG_TYPE         0x04
#define SPMI_COMMON_REG_SUBTYPE      0x05
#define SPMI_COMMON_REG_VOLTAGE_RANGE 0x40
#define SPMI_COMMON_REG_VOLTAGE_SET  0x41
#define SPMI_COMMON_REG_MODE         0x45
#define SPMI_COMMON_REG_ENABLE       0x46

/* Regulator enable values */
#define REGULATOR_ENABLE             0x80
#define REGULATOR_DISABLE            0x00

/* Regulator modes */
#define REGULATOR_MODE_NORMAL        0x07
#define REGULATOR_MODE_FAST          0x05
#define REGULATOR_MODE_IDLE          0x04

/* PM7325 regulators (base addresses) - slave 1 range is 0x000-0xFFF */
#define PM7325_S1_BASE    0x400
#define PM7325_S7_BASE    0x500
#define PM7325_L2_BASE    0x600
#define PM7325_L6_BASE    0x700
#define PM7325_L7_BASE    0x800
#define PM7325_L9_BASE    0x900
#define PM7325_L18_BASE   0xa00
#define PM7325_L19_BASE   0xb00

/* GPIO registers - slave 1 range is 0x000-0xFFF */
#define PM7325_GPIO_BASE  0xc00
#define PM7325_GPIO_COUNT 10

/* Temperature alarm registers */
#define PM7325_TEMP_ALARM_BASE 0xd00
#define TEMP_ALARM_STATUS      0x08
#define TEMP_ALARM_CONFIG      0x58

/* VADC registers */
#define PM7325_VADC_BASE  0xe00
#define VADC_STATUS1      0x08
#define VADC_MODE_CTL     0x40
#define VADC_EN_CTL1      0x46
#define VADC_CONV_REQ     0x52
#define VADC_DATA         0x60

static QTestState *qts;

static void test_pm7325_identification(void)
{
    uint8_t type, subtype, rev2, rev3, rev4, fab_id;

    /* Read PMIC identification registers */
    type = qtest_readb(qts, PM7325_BASE_ADDR + PMIC_TYPE);
    subtype = qtest_readb(qts, PM7325_BASE_ADDR + PMIC_SUBTYPE);
    rev2 = qtest_readb(qts, PM7325_BASE_ADDR + PMIC_REV2);
    rev3 = qtest_readb(qts, PM7325_BASE_ADDR + PMIC_REV3);
    rev4 = qtest_readb(qts, PM7325_BASE_ADDR + PMIC_REV4);
    fab_id = qtest_readb(qts, PM7325_BASE_ADDR + PMIC_FAB_ID);

    /* Validate PMIC identification values */
    g_assert_cmpuint(type, ==, PMIC_TYPE_VALUE);
    g_assert_cmpuint(subtype, ==, PM7325_SUBTYPE_VALUE);

    /* Log the identification for debugging */
    g_test_message("PM7325 identification: type=0x%02x, subtype=0x%02x, "
                   "rev2=0x%02x, rev3=0x%02x, rev4=0x%02x, fab_id=0x%02x",
                   type, subtype, rev2, rev3, rev4, fab_id);
}

static void test_pm7325_s1_regulator(void)
{
    uint32_t reg_base = PM7325_BASE_ADDR + PM7325_S1_BASE;
    uint8_t type, subtype, enable, mode;

    /* Read regulator type and subtype */
    type = qtest_readb(qts, reg_base + SPMI_COMMON_REG_TYPE);
    subtype = qtest_readb(qts, reg_base + SPMI_COMMON_REG_SUBTYPE);

    /* Read initial enable state (should be enabled for S1) */
    enable = qtest_readb(qts, reg_base + SPMI_COMMON_REG_ENABLE);
    g_assert_cmpuint(enable, ==, REGULATOR_ENABLE);

    /* Read mode */
    mode = qtest_readb(qts, reg_base + SPMI_COMMON_REG_MODE);
    g_assert_cmpuint(mode, ==, REGULATOR_MODE_NORMAL);

    g_test_message("PM7325 S1: type=0x%02x, subtype=0x%02x, "
                   "enable=0x%02x, mode=0x%02x", type, subtype, enable, mode);
}

static void test_pm7325_l7_regulator(void)
{
    uint32_t reg_base = PM7325_BASE_ADDR + PM7325_L7_BASE;
    uint8_t enable, mode, voltage_range, voltage_set;

    /* Read initial state (should be enabled for L7) */
    enable = qtest_readb(qts, reg_base + SPMI_COMMON_REG_ENABLE);
    g_assert_cmpuint(enable, ==, REGULATOR_ENABLE);

    /* Test voltage setting */
    voltage_range = qtest_readb(qts, reg_base + SPMI_COMMON_REG_VOLTAGE_RANGE);
    voltage_set = qtest_readb(qts, reg_base + SPMI_COMMON_REG_VOLTAGE_SET);

    /* Test mode control */
    mode = qtest_readb(qts, reg_base + SPMI_COMMON_REG_MODE);
    g_assert_cmpuint(mode, ==, REGULATOR_MODE_NORMAL);

    g_test_message("PM7325 L7: enable=0x%02x, mode=0x%02x, "
                   "vrange=0x%02x, vset=0x%02x",
                   enable, mode, voltage_range, voltage_set);
}

static void test_pm7325_gpio_control(void)
{
    uint32_t gpio_base = PM7325_BASE_ADDR + PM7325_GPIO_BASE;
    uint8_t gpio_val;

    /* Test GPIO 0 */
    gpio_val = qtest_readb(qts, gpio_base + 0);

    /* Write a value to GPIO 0 */
    qtest_writeb(qts, gpio_base + 0, 0x42);
    /* Read back and verify */
    gpio_val = qtest_readb(qts, gpio_base + 0);
    g_assert_cmpuint(gpio_val, ==, 0x42);

    /* Test GPIO enable bit manipulation */
    qtest_writeb(qts, gpio_base + 0, 0x80);  /* Enable bit */
    gpio_val = qtest_readb(qts, gpio_base + 0);
    g_assert_cmpuint(gpio_val & 0x80, ==, 0x80);

    g_test_message("PM7325 GPIO test passed");
}

static void test_pm7325_temperature_alarm(void)
{
    uint32_t temp_base = PM7325_BASE_ADDR + PM7325_TEMP_ALARM_BASE;
    uint8_t status, config;

    /* Read temperature alarm status */
    status = qtest_readb(qts, temp_base + TEMP_ALARM_STATUS);

    /* Read configuration */
    config = qtest_readb(qts, temp_base + TEMP_ALARM_CONFIG);
    /* Write configuration */
    qtest_writeb(qts, temp_base + TEMP_ALARM_CONFIG, 0x55);

    /* Read back and verify */
    config = qtest_readb(qts, temp_base + TEMP_ALARM_CONFIG);
    g_assert_cmpuint(config, ==, 0x55);

    g_test_message("PM7325 temperature alarm: status=0x%02x, config=0x%02x",
                   status, config);
}

static void test_pm7325_adc_conversion(void)
{
    uint32_t vadc_base = PM7325_BASE_ADDR + PM7325_VADC_BASE;
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

    /* Verify data is reasonable (non-zero) */
    g_assert_cmpuint(data, >, 0);

    g_test_message("PM7325 ADC conversion: status=0x%02x, data=0x%04x",
                   status, data);
}

static void test_pm7325_multiple_regulators(void)
{
    /* Test multiple regulators that should be enabled */
    uint32_t reg_bases[] = {
        PM7325_BASE_ADDR + PM7325_S1_BASE,   /* S1 - enabled */
        PM7325_BASE_ADDR + PM7325_L2_BASE,   /* L2 - enabled */
        PM7325_BASE_ADDR + PM7325_L6_BASE,   /* L6 - enabled */
        PM7325_BASE_ADDR + PM7325_L7_BASE,   /* L7 - enabled */
        PM7325_BASE_ADDR + PM7325_L9_BASE,   /* L9 - enabled */
        PM7325_BASE_ADDR + PM7325_L18_BASE,  /* L18 - enabled */
        PM7325_BASE_ADDR + PM7325_L19_BASE,  /* L19 - enabled */
    };

    for (int i = 0; i < ARRAY_SIZE(reg_bases); i++) {
        uint8_t enable = qtest_readb(qts, reg_bases[i] +
                                     SPMI_COMMON_REG_ENABLE);
        g_assert_cmpuint(enable, ==, REGULATOR_ENABLE);
    }

    g_test_message("PM7325 multiple regulator test passed");
}

static void test_pm7325_read_only_protection(void)
{
    uint8_t original_type, modified_type;

    /* Try to write to read-only PMIC_TYPE register */
    original_type = qtest_readb(qts, PM7325_BASE_ADDR + PMIC_TYPE);

    /* Attempt to modify (should be ignored) */
    qtest_writeb(qts, PM7325_BASE_ADDR + PMIC_TYPE, 0xFF);

    /* Verify it wasn't changed */
    modified_type = qtest_readb(qts, PM7325_BASE_ADDR + PMIC_TYPE);
    g_assert_cmpuint(modified_type, ==, original_type);

    g_test_message("PM7325 read-only protection verified");
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

    qtest_add_func("/pm7325/identification", test_pm7325_identification);
    qtest_add_func("/pm7325/s1-regulator", test_pm7325_s1_regulator);
    qtest_add_func("/pm7325/l7-regulator", test_pm7325_l7_regulator);
    qtest_add_func("/pm7325/gpio-control", test_pm7325_gpio_control);
    qtest_add_func("/pm7325/temperature-alarm", test_pm7325_temperature_alarm);
    qtest_add_func("/pm7325/adc-conversion", test_pm7325_adc_conversion);
    qtest_add_func("/pm7325/multiple-regulators",
                   test_pm7325_multiple_regulators);
    qtest_add_func("/pm7325/read-only-protection",
                   test_pm7325_read_only_protection);

    /* Setup and teardown for all tests */
    g_test_set_nonfatal_assertions();

    setup_qtest();
    int result = g_test_run();
    teardown_qtest();

    return result;
}
