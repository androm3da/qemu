/*
 * Qualcomm PM7250B PMIC device model
 *
 * Copyright (c) 2025 Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This model implements the PM7250B PMIC, which provides additional
 * power rails and battery management for QCS6490 SoCs.
 */

#ifndef HW_MISC_PM7250B_H
#define HW_MISC_PM7250B_H

#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "hw/irq.h"

#define TYPE_PM7250B "pm7250b"
#define PM7250B(obj) OBJECT_CHECK(PM7250BState, (obj), TYPE_PM7250B)

/* PM7250B PMIC identification */
#define PM7250B_SUBTYPE_VALUE        0x32  /* PM7250B specific subtype */

/* PM7250B register regions - slave 3 range is 0x000-0xFFF */
#define PM7250B_REV_ID_BASE          0x100   /* PMIC identification */
#define PM7250B_CHARGER_BASE         0x200   /* Battery charger */
#define PM7250B_VADC_BASE            0x600   /* Voltage ADC */
#define PM7250B_GPIO_BASE            0x800   /* GPIO control */
#define PM7250B_TEMP_ALARM_BASE      0xa00   /* Temperature monitoring */

/* PM7250B has 12 GPIOs for I/O and control */
#define PM7250B_GPIO_COUNT           12

/* Battery charger registers */
#define CHGR_BATTERY_CHARGER_STATUS_1  0x06
#define CHGR_BATTERY_CHARGER_STATUS_2  0x07
#define CHGR_BATTERY_CHARGER_STATUS_3  0x08
#define CHGR_CHARGING_ENABLE_CMD       0x42
#define CHGR_CHGR_CFG2                 0x51
#define CHGR_FLOAT_VOLTAGE_CFG         0x70

/* LDO regulator base addresses - slave 3 range is 0x000-0xFFF */
#define PM7250B_L1_BASE              0x400   /* 1.2V digital core */
#define PM7250B_L2_BASE              0x450   /* 1.8V I/O rails */
#define PM7250B_L3_BASE              0x4a0   /* 2.9V analog rails */
#define PM7250B_L4_BASE              0x4f0   /* 1.8V memory */
#define PM7250B_L5_BASE              0x540   /* 0.88V low power */
#define PM7250B_L6_BASE              0x590   /* 1.8V sensor I/O */

/* Maximum number of regulators in PM7250B */
#define PM7250B_MAX_REGULATORS       6

/* PM7250B regulator configuration */
typedef struct {
    uint8_t type;
    uint8_t subtype;
    uint8_t voltage_range;
    uint8_t voltage_set;
    uint8_t mode;
    uint8_t enable;
    bool enabled;
    uint16_t voltage_mv;  /* Voltage in millivolts */
    const char *name;
} PM7250BRegulator;

typedef struct PM7250BState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    /* PMIC identification registers */
    uint8_t pmic_type;
    uint8_t pmic_subtype;
    uint8_t pmic_rev[4];
    uint8_t fab_id;

    /* Battery charger state */
    uint8_t chgr_status1;
    uint8_t chgr_status2;
    uint8_t chgr_status3;
    uint8_t chgr_enable_cmd;
    uint8_t chgr_cfg2;
    uint8_t chgr_float_voltage;

    /* Power regulators */
    PM7250BRegulator regulators[PM7250B_MAX_REGULATORS];

    /* GPIO states (12 GPIOs) */
    uint8_t gpio_states[PM7250B_GPIO_COUNT];

    /* Temperature alarm */
    uint8_t temp_alarm_status;
    uint8_t temp_alarm_config;

    /* VADC (Voltage ADC) state */
    uint8_t vadc_status1;
    uint8_t vadc_mode_ctl;
    uint8_t vadc_en_ctl1;
    uint8_t vadc_channel;
    uint16_t vadc_data;
    bool vadc_conversion_active;
    bool vadc_interrupt_enabled;

    /* IRQ lines for various notifications */
    qemu_irq gpio_irq;
    qemu_irq temp_irq;
    qemu_irq adc_irq;
    qemu_irq chgr_irq;      /* Charger status interrupt */

    /* Timer for ADC conversion simulation */
    QEMUTimer *adc_timer;

} PM7250BState;

/* Forward declarations */
typedef struct SPMIControllerState SPMIControllerState;

/* SPMI registration function */
void pm7250b_register_with_spmi(PM7250BState *s, SPMIControllerState *spmi_controller, uint8_t slave_id);

#endif /* HW_MISC_PM7250B_H */