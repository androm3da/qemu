/*
 * QTest for Qualcomm PMR735A Peripheral Power PMIC device
 *
 * Copyright (c) 2025 Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This test exercises the PMR735A PMIC device functionality,
 * including identification, peripheral power regulators, GPIO, and ADC.
 */

#include "qemu/osdep.h"
#include "libqtest.h"

#define SPMI_CONTROLLER_BASE 0x0c440000
#define SPMI_ARB_REG_CHN_OFFSET 0x8000
#define PMR735A_SLAVE_ID 4
#define PMR735A_BASE_ADDR (SPMI_CONTROLLER_BASE + SPMI_ARB_REG_CHN_OFFSET + \
                           (PMR735A_SLAVE_ID * 0x1000))

/* PMIC identification registers */
#define PMIC_REV2         0x101
#define PMIC_REV3         0x102
#define PMIC_REV4         0x103
#define PMIC_TYPE         0x104
#define PMIC_SUBTYPE      0x105
#define PMIC_FAB_ID       0x1f2

/* Expected PMIC identification values */
#define PMIC_TYPE_VALUE   0x51
#define PMR735A_SUBTYPE_VALUE 0x30

/* Common SPMI regulator register offsets */
#define SPMI_COMMON_REG_TYPE         0x04
#define SPMI_COMMON_REG_SUBTYPE      0x05
#define SPMI_COMMON_REG_VOLTAGE_RANGE 0x40
#define SPMI_COMMON_REG_VOLTAGE_SET  0x41
#define SPMI_COMMON_REG_MODE         0x45
#define SPMI_COMMON_REG_ENABLE       0x46

/* Peripheral power regulators - slave 4 range is 0x000-0xFFF */
#define PMR735A_L1_BASE  0x200   /* 1.2V WiFi core */
#define PMR735A_L2_BASE  0x250   /* 1.8V WiFi I/O */
#define PMR735A_L3_BASE  0x2a0   /* 3.3V WiFi RF */
#define PMR735A_L7_BASE  0x3e0   /* 2.8V peripheral */

/* GPIO registers - PMR735A has 4 GPIOs */
#define PMR735A_GPIO_BASE  0x800
#define PMR735A_GPIO_COUNT 4

/* Temperature alarm */
#define PMR735A_TEMP_ALARM_BASE 0xa00
#define TEMP_ALARM_STATUS    0x08
#define TEMP_ALARM_CONFIG    0x58

/* VADC registers */
#define PMR735A_VADC_BASE  0xb00
#define VADC_STATUS1       0x08
#define VADC_MODE_CTL      0x40
#define VADC_EN_CTL1       0x46
#define VADC_CHANNEL_SEL   0x48
#define VADC_INT_EN        0x50
#define VADC_CONV_REQ      0x52
#define VADC_DATA          0x60

static QTestState *qts;

static void test_pmr735a_identification(void)
{
    uint8_t type, subtype, rev2, rev3, rev4, fab_id;

    /* Read PMIC identification registers */
    type = qtest_readb(qts, PMR735A_BASE_ADDR + PMIC_TYPE);
    subtype = qtest_readb(qts, PMR735A_BASE_ADDR + PMIC_SUBTYPE);
    rev2 = qtest_readb(qts, PMR735A_BASE_ADDR + PMIC_REV2);
    rev3 = qtest_readb(qts, PMR735A_BASE_ADDR + PMIC_REV3);
    rev4 = qtest_readb(qts, PMR735A_BASE_ADDR + PMIC_REV4);
    fab_id = qtest_readb(qts, PMR735A_BASE_ADDR + PMIC_FAB_ID);

    /* Validate PMIC identification values */
    g_assert_cmpuint(type, ==, PMIC_TYPE_VALUE);
    g_assert_cmpuint(subtype, ==, PMR735A_SUBTYPE_VALUE);

    /* Log the identification for debugging */
    g_test_message("PMR735A identification: type=0x%02x, subtype=0x%02x, "
                   "rev2=0x%02x, rev3=0x%02x, rev4=0x%02x, fab_id=0x%02x",
                   type, subtype, rev2, rev3, rev4, fab_id);
}

static void test_pmr735a_wifi_regulators(void)
{
    uint32_t l1_base = PMR735A_BASE_ADDR + PMR735A_L1_BASE;
    uint32_t l2_base = PMR735A_BASE_ADDR + PMR735A_L2_BASE;
    uint32_t l3_base = PMR735A_BASE_ADDR + PMR735A_L3_BASE;
    uint8_t enable, mode, voltage_range, voltage_set;

    /* Test L1: 1.2V WiFi core */
    enable = qtest_readb(qts, l1_base + SPMI_COMMON_REG_ENABLE);
    mode = qtest_readb(qts, l1_base + SPMI_COMMON_REG_MODE);
    voltage_range = qtest_readb(qts, l1_base + SPMI_COMMON_REG_VOLTAGE_RANGE);
    voltage_set = qtest_readb(qts, l1_base + SPMI_COMMON_REG_VOLTAGE_SET);

    g_assert_cmpuint(enable, ==, 0x80);      /* Should be enabled */
    g_assert_cmpuint(mode, ==, 0x07);        /* Auto mode */
    g_assert_cmpuint(voltage_range, ==, 0x01); /* Low voltage range */
    g_assert_cmpuint(voltage_set, ==, 0x30);   /* 1.2V setting */

    /* Test L2: 1.8V WiFi I/O */
    enable = qtest_readb(qts, l2_base + SPMI_COMMON_REG_ENABLE);
    g_assert_cmpuint(enable, ==, 0x80);  /* Should be enabled */

    /* Test L3: 3.3V WiFi RF */
    enable = qtest_readb(qts, l3_base + SPMI_COMMON_REG_ENABLE);
    voltage_range = qtest_readb(qts, l3_base + SPMI_COMMON_REG_VOLTAGE_RANGE);
    g_assert_cmpuint(enable, ==, 0x80);      /* Should be enabled */
    g_assert_cmpuint(voltage_range, ==, 0x03); /* High voltage range */

    g_test_message("PMR735A WiFi regulators: L1=%dmV, L2=%dmV, L3=%dmV",
                   1200, 1800, 3300);
}

static void test_pmr735a_peripheral_regulator(void)
{
    uint32_t l7_base = PMR735A_BASE_ADDR + PMR735A_L7_BASE;
    uint8_t enable, voltage_set, voltage_range;

    /* Test L7: 2.8V peripheral */
    enable = qtest_readb(qts, l7_base + SPMI_COMMON_REG_ENABLE);
    voltage_range = qtest_readb(qts, l7_base + SPMI_COMMON_REG_VOLTAGE_RANGE);
    voltage_set = qtest_readb(qts, l7_base + SPMI_COMMON_REG_VOLTAGE_SET);

    g_assert_cmpuint(enable, ==, 0x80);      /* Should be enabled */
    g_assert_cmpuint(voltage_range, ==, 0x03); /* High voltage range */
    g_assert_cmpuint(voltage_set, ==, 0x38);   /* 2.8V setting */

    g_test_message("PMR735A peripheral regulator L7: enable=0x%02x, "
                   "vrange=0x%02x, vset=0x%02x",
                   enable, voltage_range, voltage_set);
}

static void test_pmr735a_gpio_control(void)
{
    uint32_t gpio_base = PMR735A_BASE_ADDR + PMR735A_GPIO_BASE;
    uint8_t gpio_val;

    /* Test GPIO 0 (PMR735A has 4 GPIOs) */
    gpio_val = qtest_readb(qts, gpio_base + 0);

    /* Write a value to GPIO 0 */
    qtest_writeb(qts, gpio_base + 0, 0x44);
    /* Read back and verify */
    gpio_val = qtest_readb(qts, gpio_base + 0);
    g_assert_cmpuint(gpio_val, ==, 0x44);

    /* Test GPIO 3 (last GPIO) */
    qtest_writeb(qts, gpio_base + 3, 0x88);
    gpio_val = qtest_readb(qts, gpio_base + 3);
    g_assert_cmpuint(gpio_val, ==, 0x88);

    g_test_message("PMR735A GPIO test passed (4 GPIOs available)");
}

static void test_pmr735a_temperature_alarm(void)
{
    uint32_t temp_base = PMR735A_BASE_ADDR + PMR735A_TEMP_ALARM_BASE;
    uint8_t status, config;

    /* Read initial temperature alarm status */
    status = qtest_readb(qts, temp_base + TEMP_ALARM_STATUS);
    config = qtest_readb(qts, temp_base + TEMP_ALARM_CONFIG);

    /* Configure temperature alarm */
    qtest_writeb(qts, temp_base + TEMP_ALARM_CONFIG, 0x33);
    config = qtest_readb(qts, temp_base + TEMP_ALARM_CONFIG);
    g_assert_cmpuint(config, ==, 0x33);

    g_test_message("PMR735A temperature alarm: status=0x%02x, config=0x%02x",
                   status, config);
}

static void test_pmr735a_adc_conversion(void)
{
    uint32_t vadc_base = PMR735A_BASE_ADDR + PMR735A_VADC_BASE;
    uint8_t status, mode, enable, channel;
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

    /* Set channel to WiFi core rail (channel 0) */
    qtest_writeb(qts, vadc_base + VADC_CHANNEL_SEL, 0x00);
    channel = qtest_readb(qts, vadc_base + VADC_CHANNEL_SEL);
    g_assert_cmpuint(channel, ==, 0x00);

    /* Enable interrupts */
    qtest_writeb(qts, vadc_base + VADC_INT_EN, 0x01);

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

    /* Verify data is reasonable for WiFi core rail (should be ~1.2V) */
    g_assert_cmpuint(data, >, 0x1150);  /* Should be around 0x1200 */
    g_assert_cmpuint(data, <, 0x1250);

    g_test_message("PMR735A ADC conversion: status=0x%02x, data=0x%04x",
                   status, data);
}

static void test_pmr735a_regulator_state_machine(void)
{
    uint32_t l1_base = PMR735A_BASE_ADDR + PMR735A_L1_BASE;
    uint8_t enable;

    /* Test regulator enable/disable sequence */

    /* Disable regulator */
    qtest_writeb(qts, l1_base + SPMI_COMMON_REG_ENABLE, 0x00);
    enable = qtest_readb(qts, l1_base + SPMI_COMMON_REG_ENABLE);
    g_assert_cmpuint(enable, ==, 0x00);

    /* Re-enable regulator */
    qtest_writeb(qts, l1_base + SPMI_COMMON_REG_ENABLE, 0x80);
    enable = qtest_readb(qts, l1_base + SPMI_COMMON_REG_ENABLE);
    g_assert_cmpuint(enable, ==, 0x80);

    /* Test voltage setting validation */
    qtest_writeb(qts, l1_base + SPMI_COMMON_REG_VOLTAGE_SET, 0x20);
    uint8_t voltage = qtest_readb(qts, l1_base + SPMI_COMMON_REG_VOLTAGE_SET);
    g_assert_cmpuint(voltage, ==, 0x20);

    g_test_message("PMR735A regulator state machine test passed");
}

static void test_pmr735a_read_only_protection(void)
{
    uint8_t original_type, modified_type;

    /* Try to write to read-only PMIC_TYPE register */
    original_type = qtest_readb(qts, PMR735A_BASE_ADDR + PMIC_TYPE);

    /* Attempt to modify (should be ignored) */
    qtest_writeb(qts, PMR735A_BASE_ADDR + PMIC_TYPE, 0xFF);

    /* Verify it wasn't changed */
    modified_type = qtest_readb(qts, PMR735A_BASE_ADDR + PMIC_TYPE);
    g_assert_cmpuint(modified_type, ==, original_type);

    g_test_message("PMR735A read-only protection verified");
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

    qtest_add_func("/pmr735a/identification", test_pmr735a_identification);
    qtest_add_func("/pmr735a/wifi-regulators", test_pmr735a_wifi_regulators);
    qtest_add_func("/pmr735a/peripheral-regulator",
                   test_pmr735a_peripheral_regulator);
    qtest_add_func("/pmr735a/gpio-control", test_pmr735a_gpio_control);
    qtest_add_func("/pmr735a/temperature-alarm",
                   test_pmr735a_temperature_alarm);
    qtest_add_func("/pmr735a/adc-conversion", test_pmr735a_adc_conversion);
    qtest_add_func("/pmr735a/regulator-state-machine",
                   test_pmr735a_regulator_state_machine);
    qtest_add_func("/pmr735a/read-only-protection",
                   test_pmr735a_read_only_protection);

    /* Setup and teardown for all tests */
    g_test_set_nonfatal_assertions();

    setup_qtest();
    int result = g_test_run();
    teardown_qtest();

    return result;
}
