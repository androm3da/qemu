/*
 * Qualcomm PMR735A PMIC device model
 *
 * Copyright (c) 2025 Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This model implements the PMR735A PMIC, which provides additional
 * power rails and LDO regulators for peripheral devices on QCS6490 SoCs.
 * PMR735A is typically used for WiFi, connectivity, and sensor power.
 */

#ifndef HW_MISC_PMR735A_H
#define HW_MISC_PMR735A_H

#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "hw/irq.h"

/* Forward declarations */
typedef struct SPMIControllerState SPMIControllerState;

#define TYPE_PMR735A "pmr735a"
#define PMR735A(obj) OBJECT_CHECK(PMR735AState, (obj), TYPE_PMR735A)

/* PMR735A PMIC identification */
#define PMR735A_SUBTYPE_VALUE        0x30  /* PMR735A specific subtype */

/* PMR735A has fewer GPIOs than primary PMICs - focused on power rails */
#define PMR735A_GPIO_COUNT           4

/* PMR735A register regions - slave 4 range is 0x000-0xFFF */
#define PMR735A_REV_ID_BASE          0x100   /* PMIC identification */
#define PMR735A_GPIO_BASE            0x800   /* GPIO control */
#define PMR735A_TEMP_ALARM_BASE      0xa00   /* Temperature monitoring */
#define PMR735A_VADC_BASE            0xb00   /* Voltage ADC */

/* Peripheral power rails (LDO regulators) - slave 4 range is 0x000-0xFFF */
#define PMR735A_L1_BASE              0x200   /* 1.2V WiFi core */
#define PMR735A_L2_BASE              0x250   /* 1.8V WiFi I/O */
#define PMR735A_L3_BASE              0x2a0   /* 3.3V WiFi RF */
#define PMR735A_L4_BASE              0x2f0   /* 1.8V sensors */
#define PMR735A_L5_BASE              0x340   /* 3.0V sensor analog */
#define PMR735A_L6_BASE              0x390   /* 1.2V connectivity */
#define PMR735A_L7_BASE              0x3e0   /* 2.8V peripheral */

/* Maximum number of regulators in PMR735A */
#define PMR735A_MAX_REGULATORS       7

/* PMR735A regulator configuration */
typedef struct {
    uint8_t type;
    uint8_t subtype;
    uint8_t voltage_range;
    uint8_t voltage_set;
    uint8_t mode;
    uint8_t enable;
    bool enabled;
    uint16_t voltage_mv;  /* Voltage in millivolts */
} PMR735ARegulator;

typedef struct PMR735AState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    /* PMIC identification registers */
    uint8_t pmic_type;
    uint8_t pmic_subtype;
    uint8_t pmic_rev[4];
    uint8_t fab_id;

    /* Peripheral power regulators */
    PMR735ARegulator regulators[PMR735A_MAX_REGULATORS];

    /* GPIO states (4 GPIOs) */
    uint8_t gpio_states[PMR735A_GPIO_COUNT];

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

    /* IRQ line for various notifications */
    qemu_irq gpio_irq;
    qemu_irq temp_irq;
    qemu_irq adc_irq;

    /* Timer for ADC conversion simulation */
    QEMUTimer *adc_timer;

} PMR735AState;

/* SPMI registration function */
void pmr735a_register_with_spmi(PMR735AState *s, SPMIControllerState *spmi_controller, uint8_t slave_id);

#endif /* HW_MISC_PMR735A_H */
