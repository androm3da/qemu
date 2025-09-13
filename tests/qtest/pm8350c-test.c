/*
 * QTest for Qualcomm PM8350C Camera/Display PMIC device
 *
 * Copyright (c) 2025 Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This test exercises the PM8350C PMIC device functionality,
 * including identification, camera/display regulators, GPIO, flash LED,
 * and ADC.
 */

#include "qemu/osdep.h"
#include "libqtest.h"

#define SPMI_CONTROLLER_BASE 0x0c440000
#define SPMI_ARB_REG_CHN_OFFSET 0x8000
#define PM8350C_SLAVE_ID 2
#define PM8350C_BASE_ADDR (SPMI_CONTROLLER_BASE + SPMI_ARB_REG_CHN_OFFSET + \
                           (PM8350C_SLAVE_ID * 0x1000))

/* PMIC identification registers */
#define PMIC_REV2         0x101
#define PMIC_REV3         0x102
#define PMIC_REV4         0x103
#define PMIC_TYPE         0x104
#define PMIC_SUBTYPE      0x105
#define PMIC_FAB_ID       0x1f2

/* Expected PMIC identification values */
#define PMIC_TYPE_VALUE   0x51
#define PM8350C_SUBTYPE_VALUE 0x2D

/* Common SPMI regulator register offsets */
#define SPMI_COMMON_REG_TYPE         0x04
#define SPMI_COMMON_REG_SUBTYPE      0x05
#define SPMI_COMMON_REG_VOLTAGE_RANGE 0x40
#define SPMI_COMMON_REG_VOLTAGE_SET  0x41
#define SPMI_COMMON_REG_MODE         0x45
#define SPMI_COMMON_REG_ENABLE       0x46

/* Camera/Display regulators - slave 2 range is 0x000-0xFFF */
#define PM8350C_L1_BASE  0x400  /* 1.8V camera digital */
#define PM8350C_L2_BASE  0x500  /* 1.2V camera core */
#define PM8350C_L3_BASE  0x600  /* 2.8V camera analog */
#define PM8350C_L6_BASE  0x700  /* 3.0V display analog */

/* GPIO registers - PM8350C has 8 GPIOs - slave 2 range is 0x000-0xFFF */
#define PM8350C_GPIO_BASE  0x800
#define PM8350C_GPIO_COUNT 8

/* Temperature alarm */
#define PM8350C_TEMP_ALARM_BASE 0xa00
#define TEMP_ALARM_STATUS    0x08
#define TEMP_ALARM_CONFIG    0x58

/* VADC registers */
#define PM8350C_VADC_BASE  0xc00
#define VADC_STATUS1       0x08
#define VADC_MODE_CTL      0x40
#define VADC_EN_CTL1       0x46
#define VADC_CONV_REQ      0x52
#define VADC_DATA          0x60

/* Flash LED registers */
#define PM8350C_FLASH_BASE   0xd00
#define FLASH_LED_CTRL       0x40
#define FLASH_LED_CURRENT    0x41

/* LPG PWM registers for backlight */
#define PM8350C_LPG_BASE     0xe00
#define LPG_CTRL             0x40
#define LPG_DUTY_CYCLE_LSB   0x42
#define LPG_DUTY_CYCLE_MSB   0x43

static QTestState *qts;

static void test_pm8350c_identification(void)
{
    uint8_t type, subtype, rev2, rev3, rev4, fab_id;

    /* Read PMIC identification registers */
    type = qtest_readb(qts, PM8350C_BASE_ADDR + PMIC_TYPE);
    subtype = qtest_readb(qts, PM8350C_BASE_ADDR + PMIC_SUBTYPE);
    rev2 = qtest_readb(qts, PM8350C_BASE_ADDR + PMIC_REV2);
    rev3 = qtest_readb(qts, PM8350C_BASE_ADDR + PMIC_REV3);
    rev4 = qtest_readb(qts, PM8350C_BASE_ADDR + PMIC_REV4);
    fab_id = qtest_readb(qts, PM8350C_BASE_ADDR + PMIC_FAB_ID);

    /* Validate PMIC identification values */
    g_assert_cmpuint(type, ==, PMIC_TYPE_VALUE);
    g_assert_cmpuint(subtype, ==, PM8350C_SUBTYPE_VALUE);

    /* Log the identification for debugging */
    g_test_message("PM8350C identification: type=0x%02x, subtype=0x%02x, "
                   "rev2=0x%02x, rev3=0x%02x, rev4=0x%02x, fab_id=0x%02x",
                   type, subtype, rev2, rev3, rev4, fab_id);
}

static void test_pm8350c_camera_regulators(void)
{
    uint32_t l1_base = PM8350C_BASE_ADDR + PM8350C_L1_BASE;
    uint32_t l2_base = PM8350C_BASE_ADDR + PM8350C_L2_BASE;
    uint32_t l3_base = PM8350C_BASE_ADDR + PM8350C_L3_BASE;
    uint8_t enable, mode;

    /* Test L1: 1.8V camera digital */
    enable = qtest_readb(qts, l1_base + SPMI_COMMON_REG_ENABLE);
    mode = qtest_readb(qts, l1_base + SPMI_COMMON_REG_MODE);

    g_assert_cmpuint(enable, ==, 0x80);  /* Should be enabled */
    g_assert_cmpuint(mode, ==, 0x07);    /* Auto mode */

    /* Test L2: 1.2V camera core */
    enable = qtest_readb(qts, l2_base + SPMI_COMMON_REG_ENABLE);
    g_assert_cmpuint(enable, ==, 0x80);  /* Should be enabled */

    /* Test L3: 2.8V camera analog */
    enable = qtest_readb(qts, l3_base + SPMI_COMMON_REG_ENABLE);
    g_assert_cmpuint(enable, ==, 0x80);  /* Should be enabled */

    g_test_message("PM8350C camera regulators: L1=0x%02x, L2=0x%02x, L3=0x%02x",
                   qtest_readb(qts, l1_base + SPMI_COMMON_REG_VOLTAGE_SET),
                   qtest_readb(qts, l2_base + SPMI_COMMON_REG_VOLTAGE_SET),
                   qtest_readb(qts, l3_base + SPMI_COMMON_REG_VOLTAGE_SET));
}

static void test_pm8350c_display_regulator(void)
{
    uint32_t l6_base = PM8350C_BASE_ADDR + PM8350C_L6_BASE;
    uint8_t enable, voltage_set, voltage_range;

    /* Test L6: 3.0V display analog */
    enable = qtest_readb(qts, l6_base + SPMI_COMMON_REG_ENABLE);
    voltage_range = qtest_readb(qts, l6_base + SPMI_COMMON_REG_VOLTAGE_RANGE);
    voltage_set = qtest_readb(qts, l6_base + SPMI_COMMON_REG_VOLTAGE_SET);

    g_assert_cmpuint(enable, ==, 0x80);      /* Should be enabled */
    g_assert_cmpuint(voltage_range, ==, 0x03); /* High voltage range */
    g_assert_cmpuint(voltage_set, ==, 0x3C);   /* 3.0V setting */

    g_test_message("PM8350C display regulator L6: enable=0x%02x, "
                   "vrange=0x%02x, vset=0x%02x",
                   enable, voltage_range, voltage_set);
}

static void test_pm8350c_gpio_control(void)
{
    uint32_t gpio_base = PM8350C_BASE_ADDR + PM8350C_GPIO_BASE;
    uint8_t gpio_val;

    /* Test GPIO 0 (PM8350C has 8 GPIOs) */
    gpio_val = qtest_readb(qts, gpio_base + 0);

    /* Write a value to GPIO 0 */
    qtest_writeb(qts, gpio_base + 0, 0x33);
    /* Read back and verify */
    gpio_val = qtest_readb(qts, gpio_base + 0);
    g_assert_cmpuint(gpio_val, ==, 0x33);

    /* Test GPIO 7 (last GPIO) */
    qtest_writeb(qts, gpio_base + 7, 0x77);
    gpio_val = qtest_readb(qts, gpio_base + 7);
    g_assert_cmpuint(gpio_val, ==, 0x77);

    g_test_message("PM8350C GPIO test passed (8 GPIOs available)");
}

static void test_pm8350c_temperature_alarm(void)
{
    uint32_t temp_base = PM8350C_BASE_ADDR + PM8350C_TEMP_ALARM_BASE;
    uint8_t status, config;

    /* Read initial temperature alarm status */
    status = qtest_readb(qts, temp_base + TEMP_ALARM_STATUS);
    config = qtest_readb(qts, temp_base + TEMP_ALARM_CONFIG);

    /* Configure temperature alarm */
    qtest_writeb(qts, temp_base + TEMP_ALARM_CONFIG, 0x22);
    config = qtest_readb(qts, temp_base + TEMP_ALARM_CONFIG);
    g_assert_cmpuint(config, ==, 0x22);

    g_test_message("PM8350C temperature alarm: status=0x%02x, config=0x%02x",
                   status, config);
}

static void test_pm8350c_adc_conversion(void)
{
    uint32_t vadc_base = PM8350C_BASE_ADDR + PM8350C_VADC_BASE;
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

    /* Verify data is reasonable for PM8350C (should be ~2.8V camera rail) */
    g_assert_cmpuint(data, >, 0x2700);  /* Should be around 0x2800 */
    g_assert_cmpuint(data, <, 0x2900);

    g_test_message("PM8350C ADC conversion: status=0x%02x, data=0x%04x",
                   status, data);
}

static void test_pm8350c_flash_led(void)
{
    uint32_t flash_base = PM8350C_BASE_ADDR + PM8350C_FLASH_BASE;
    uint8_t ctrl, current;

    /* Configure flash LED */
    qtest_writeb(qts, flash_base + FLASH_LED_CTRL, 0x80);
    qtest_writeb(qts, flash_base + FLASH_LED_CURRENT, 0x3F);

    /* Read back values */
    ctrl = qtest_readb(qts, flash_base + FLASH_LED_CTRL);
    current = qtest_readb(qts, flash_base + FLASH_LED_CURRENT);

    g_assert_cmpuint(ctrl, ==, 0x80);
    g_assert_cmpuint(current, ==, 0x3F);

    g_test_message("PM8350C flash LED: ctrl=0x%02x, current=0x%02x",
                   ctrl, current);
}

static void test_pm8350c_pwm_backlight(void)
{
    uint32_t lpg_base = PM8350C_BASE_ADDR + PM8350C_LPG_BASE;
    uint8_t ctrl, duty_lsb, duty_msb;

    /* Configure PWM for backlight */
    qtest_writeb(qts, lpg_base + LPG_CTRL, 0xC0);
    qtest_writeb(qts, lpg_base + LPG_DUTY_CYCLE_LSB, 0x80);
    qtest_writeb(qts, lpg_base + LPG_DUTY_CYCLE_MSB, 0x01);

    /* Read back values */
    ctrl = qtest_readb(qts, lpg_base + LPG_CTRL);
    duty_lsb = qtest_readb(qts, lpg_base + LPG_DUTY_CYCLE_LSB);
    duty_msb = qtest_readb(qts, lpg_base + LPG_DUTY_CYCLE_MSB);

    g_assert_cmpuint(ctrl, ==, 0xC0);
    g_assert_cmpuint(duty_lsb, ==, 0x80);
    g_assert_cmpuint(duty_msb, ==, 0x01);

    g_test_message("PM8350C PWM backlight: ctrl=0x%02x, duty=0x%02x%02x",
                   ctrl, duty_msb, duty_lsb);
}

static void test_pm8350c_read_only_protection(void)
{
    uint8_t original_type, modified_type;

    /* Try to write to read-only PMIC_TYPE register */
    original_type = qtest_readb(qts, PM8350C_BASE_ADDR + PMIC_TYPE);

    /* Attempt to modify (should be ignored) */
    qtest_writeb(qts, PM8350C_BASE_ADDR + PMIC_TYPE, 0xFF);

    /* Verify it wasn't changed */
    modified_type = qtest_readb(qts, PM8350C_BASE_ADDR + PMIC_TYPE);
    g_assert_cmpuint(modified_type, ==, original_type);

    g_test_message("PM8350C read-only protection verified");
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

    qtest_add_func("/pm8350c/identification", test_pm8350c_identification);
    qtest_add_func("/pm8350c/camera-regulators",
                   test_pm8350c_camera_regulators);
    qtest_add_func("/pm8350c/display-regulator",
                   test_pm8350c_display_regulator);
    qtest_add_func("/pm8350c/gpio-control", test_pm8350c_gpio_control);
    qtest_add_func("/pm8350c/temperature-alarm",
                   test_pm8350c_temperature_alarm);
    qtest_add_func("/pm8350c/adc-conversion", test_pm8350c_adc_conversion);
    qtest_add_func("/pm8350c/flash-led", test_pm8350c_flash_led);
    qtest_add_func("/pm8350c/pwm-backlight", test_pm8350c_pwm_backlight);
    qtest_add_func("/pm8350c/read-only-protection",
                   test_pm8350c_read_only_protection);

    /* Setup and teardown for all tests */
    g_test_set_nonfatal_assertions();

    setup_qtest();
    int result = g_test_run();
    teardown_qtest();

    return result;
}
