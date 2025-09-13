/*
 * Qualcomm PMK8350 PMIC (Master PMIC)
 *
 * Copyright (c) 2025 Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This implements the PMK8350 Master PMIC device found on QCM6490 hardware.
 * Based on real hardware analysis and Linux drivers.
 */

#ifndef HW_MISC_PMK8350_H
#define HW_MISC_PMK8350_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_PMK8350 "pmk8350"
OBJECT_DECLARE_SIMPLE_TYPE(PMK8350State, PMK8350)

#define PMK8350_REGISTER_SPACE_SIZE 0x10000  /* 64KB SPMI address space */

/* PMIC identification registers */
#define PMIC_REV2                    0x101
#define PMIC_REV3                    0x102
#define PMIC_REV4                    0x103
#define PMIC_TYPE                    0x104
#define PMIC_SUBTYPE                 0x105
#define PMIC_FAB_ID                  0x1f2

/* PMIC type values for PMK8350 */
#define PMIC_TYPE_VALUE              0x51
#define PMK8350_SUBTYPE_VALUE        0x2F  /* Based on Linux kernel patterns */

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

/* GPIO controller registers - PMK8350 has only 4 GPIOs */
#define PMK8350_GPIO_BASE            0x900
#define PMK8350_GPIO_COUNT           4

/* Power key registers */
#define PMK8350_PON_BASE             0x800
#define PON_PON_REASON1              0x08
#define PON_PON_REASON2              0x09
#define PON_POFF_REASON1             0x0C
#define PON_POFF_REASON2             0x0D

/* RTC registers */
#define PMK8350_RTC_BASE             0x6000

/* VADC registers for ADC functionality - slave 0 range is 0x000-0xFFF */
#define PMK8350_VADC_BASE            0xc00
#define VADC_STATUS1                 0x08
#define VADC_MODE_CTL                0x40
#define VADC_EN_CTL1                 0x46
#define VADC_CHANNEL_SEL             0x48
#define VADC_INT_EN                  0x50
#define VADC_CONV_REQ                0x52
#define VADC_DATA                    0x60

/* Maximum number of regulators (PMK8350 has fewer than other PMICs) */
#define PMK8350_MAX_REGULATORS       8

/* Regulator definition */
typedef struct {
    uint16_t base_addr;
    uint8_t type;
    uint8_t subtype;
    uint8_t voltage_range;
    uint8_t voltage_set;
    uint8_t mode;
    uint8_t enable;
    const char *name;
} PMK8350Regulator;

struct PMK8350State {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    /* PMIC identification */
    uint8_t pmic_rev2;
    uint8_t pmic_rev3;
    uint8_t pmic_rev4;
    uint8_t pmic_type;
    uint8_t pmic_subtype;
    uint8_t pmic_fab_id;

    /* Regulator states (minimal for master PMIC) */
    PMK8350Regulator regulators[PMK8350_MAX_REGULATORS];
    uint8_t num_regulators;

    /* GPIO states - only 4 GPIOs */
    uint8_t gpio_states[PMK8350_GPIO_COUNT];

    /* Power-on/off reasons */
    uint8_t pon_reason1;
    uint8_t pon_reason2;
    uint8_t poff_reason1;
    uint8_t poff_reason2;

    /* Enhanced VADC state with channel selection and interrupts */
    uint8_t vadc_status1;
    uint8_t vadc_mode_ctl;
    uint8_t vadc_en_ctl1;
    uint8_t vadc_channel;
    uint8_t vadc_conv_req;
    uint16_t vadc_data;
    bool vadc_conversion_active;
    bool vadc_interrupt_enabled;

    /* IRQ lines for various notifications */
    qemu_irq gpio_irq;      /* GPIO state change interrupt */
    qemu_irq temp_irq;      /* Temperature alarm interrupt */
    qemu_irq adc_irq;       /* ADC End-of-Conversion interrupt */
    qemu_irq pon_irq;       /* Power-on interrupt */

    /* Timer for ADC conversion simulation */
    QEMUTimer *adc_timer;
};

/* Forward declarations */
typedef struct SPMIControllerState SPMIControllerState;

/* SPMI registration function */
void pmk8350_register_with_spmi(PMK8350State *s,
                                SPMIControllerState *spmi_controller,
                                uint8_t slave_id);

#endif /* HW_MISC_PMK8350_H */
