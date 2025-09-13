/*
 * Qualcomm PM7325 PMIC
 *
 * Copyright (c) 2025 Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This implements the PM7325 PMIC device found on QCM6490 hardware.
 * Based on real hardware analysis and Linux drivers.
 */

#ifndef HW_MISC_PM7325_H
#define HW_MISC_PM7325_H

#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "hw/irq.h"
#include "qom/object.h"

/* Forward declaration */
typedef struct SPMIControllerState SPMIControllerState;

#define TYPE_PM7325 "pm7325"
OBJECT_DECLARE_SIMPLE_TYPE(PM7325State, PM7325)

#define PM7325_REGISTER_SPACE_SIZE 0x10000  /* 64KB SPMI address space */

/* PMIC identification registers */
#define PMIC_REV2                    0x101
#define PMIC_REV3                    0x102
#define PMIC_REV4                    0x103
#define PMIC_TYPE                    0x104
#define PMIC_SUBTYPE                 0x105
#define PMIC_FAB_ID                  0x1f2

/* PMIC type values for PM7325 */
#define PMIC_TYPE_VALUE              0x51
#define PM7325_SUBTYPE_VALUE         0x2C  /* Based on Linux kernel patterns */

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

/* GPIO controller registers - PM7325 has 10 GPIOs
 * slave 1 range is 0x000-0xFFF
 */
#define PM7325_GPIO_BASE             0xc00
#define PM7325_GPIO_COUNT            10

/* Temperature alarm registers */
#define PM7325_TEMP_ALARM_BASE       0xd00

/* VADC registers for ADC functionality */
#define PM7325_VADC_BASE             0xe00
#define VADC_STATUS1                 0x08
#define VADC_MODE_CTL                0x40
#define VADC_EN_CTL1                 0x46
#define VADC_CHANNEL_SEL             0x48
#define VADC_INT_EN                  0x50
#define VADC_CONV_REQ                0x52
#define VADC_DATA                    0x60

/* Maximum number of regulators */
#define PM7325_MAX_REGULATORS        32

/* Enhanced regulator definition with voltage tracking */
typedef struct {
    uint16_t base_addr;
    uint8_t type;
    uint8_t subtype;
    uint8_t voltage_range;
    uint8_t voltage_set;
    uint8_t mode;
    uint8_t enable;
    bool enabled;
    uint16_t voltage_mv;  /* Actual voltage in millivolts */
    const char *name;
} PM7325Regulator;

struct PM7325State {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    /* PMIC identification */
    uint8_t pmic_rev2;
    uint8_t pmic_rev3;
    uint8_t pmic_rev4;
    uint8_t pmic_type;
    uint8_t pmic_subtype;
    uint8_t pmic_fab_id;

    /* Regulator states */
    PM7325Regulator regulators[PM7325_MAX_REGULATORS];
    uint8_t num_regulators;

    /* GPIO states */
    uint8_t gpio_states[PM7325_GPIO_COUNT];

    /* Temperature alarm */
    uint8_t temp_alarm_status;
    uint8_t temp_alarm_config;

    /* Enhanced VADC state with channel selection and interrupts */
    uint8_t vadc_status1;
    uint8_t vadc_mode_ctl;
    uint8_t vadc_en_ctl1;
    uint8_t vadc_channel;       /* Current channel being converted */
    uint8_t vadc_conv_req;
    uint16_t vadc_data;
    bool vadc_conversion_active;
    bool vadc_interrupt_enabled;

    /* IRQ lines for various notifications */
    qemu_irq gpio_irq;
    qemu_irq temp_irq;
    qemu_irq adc_irq;

    /* Timer for ADC conversion simulation */
    QEMUTimer *adc_timer;
};

/* SPMI registration function */
void pm7325_register_with_spmi(PM7325State *s,
                               SPMIControllerState *spmi_controller,
                               uint8_t slave_id);

#endif /* HW_MISC_PM7325_H */
