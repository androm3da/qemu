/*
 * Qualcomm PM8350C PMIC device model
 *
 * Copyright (c) 2025 Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This model implements the PM8350C PMIC, which is used primarily for
 * camera and display power management on QCS6490 SoCs.
 */

#ifndef HW_MISC_PM8350C_H
#define HW_MISC_PM8350C_H

#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "hw/irq.h"

/* Forward declaration */
typedef struct SPMIControllerState SPMIControllerState;

#define TYPE_PM8350C "pm8350c"
#define PM8350C(obj) OBJECT_CHECK(PM8350CState, (obj), TYPE_PM8350C)

/* PM8350C PMIC identification */
#define PM8350C_SUBTYPE_VALUE        0x2D  /* PM8350C specific subtype */

/* PM8350C has fewer GPIOs than PM7325 - focused on camera/display */
#define PM8350C_GPIO_COUNT           8

/* PM8350C register regions (SPMI mapping emulated via memory)
 * slave 2 range is 0x000-0xFFF
 */
#define PM8350C_REV_ID_BASE          0x100   /* PMIC identification */
#define PM8350C_GPIO_BASE            0x800   /* GPIO control */
#define PM8350C_TEMP_ALARM_BASE      0xa00   /* Temperature monitoring */
#define PM8350C_VADC_BASE            0xc00   /* Voltage ADC */

/* Camera-specific power rails (LDO regulators)
 * slave 2 range is 0x000-0xFFF
 */
#define PM8350C_L1_BASE              0x400  /* 1.8V camera digital */
#define PM8350C_L2_BASE              0x500  /* 1.2V camera core */
#define PM8350C_L3_BASE              0x600  /* 2.8V camera analog */
#define PM8350C_L4_BASE              0x800  /* 1.8V camera I/O */

/* Display-specific power rails */
#define PM8350C_L5_BASE              0x900  /* 1.8V display I/O */
#define PM8350C_L6_BASE              0x700  /* 3.0V display analog */

/* Flash LED driver for camera */
#define PM8350C_FLASH_BASE           0xd00   /* LED flash driver */

/* PWM/LPG for display backlight */
#define PM8350C_LPG_BASE             0xe00   /* LPG PWM generator */

/* Maximum number of regulators in PM8350C */
#define PM8350C_MAX_REGULATORS       6

/* PM8350C regulator configuration */
typedef struct {
    uint8_t type;
    uint8_t subtype;
    uint8_t voltage_range;
    uint8_t voltage_set;
    uint8_t mode;
    uint8_t enable;
    bool enabled;
    uint16_t voltage_mv;  /* Voltage in millivolts */
} PM8350CRegulator;

typedef struct PM8350CState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    /* PMIC identification registers */
    uint8_t pmic_type;
    uint8_t pmic_subtype;
    uint8_t pmic_rev[4];
    uint8_t fab_id;

    /* Camera/Display regulators */
    PM8350CRegulator regulators[PM8350C_MAX_REGULATORS];

    /* GPIO states (8 GPIOs) */
    uint8_t gpio_states[PM8350C_GPIO_COUNT];

    /* Temperature alarm */
    uint8_t temp_alarm_status;
    uint8_t temp_alarm_config;

    /* VADC (Voltage ADC) state */
    uint8_t vadc_status1;
    uint8_t vadc_mode_ctl;
    uint8_t vadc_en_ctl1;
    uint8_t vadc_channel;       /* Current channel being converted */
    uint16_t vadc_data;
    bool vadc_conversion_active;
    bool vadc_interrupt_enabled;

    /* Flash LED state */
    uint8_t flash_led_ctrl;
    uint8_t flash_led_current;

    /* PWM/LPG for backlight */
    uint8_t lpg_ctrl;
    uint16_t lpg_duty_cycle;

    /* IRQ line for various notifications */
    qemu_irq gpio_irq;
    qemu_irq temp_irq;
    qemu_irq flash_irq;
    qemu_irq adc_irq;

    /* Timer for ADC conversion simulation */
    QEMUTimer *adc_timer;

} PM8350CState;

/* SPMI registration function */
void pm8350c_register_with_spmi(PM8350CState *s,
                                SPMIControllerState *spmi_controller,
                                uint8_t slave_id);

#endif /* HW_MISC_PM8350C_H */
