/*
 * Qualcomm PMR735A PMIC device model
 *
 * Copyright (c) 2025 Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This model implements the PMR735A PMIC, which provides additional
 * power rails for WiFi, connectivity, and sensor subsystems on QCS6490.
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "trace.h"
#include "hw/misc/pmr735a.h"
#include "hw/misc/spmi-controller.h"

/* PMIC registers */
#define PMR735A_REV2                 0x101
#define PMR735A_REV3                 0x102
#define PMR735A_REV4                 0x103
#define PMR735A_TYPE                 0x104
#define PMR735A_SUBTYPE              0x105
#define PMR735A_FAB_ID               0x1f2

/* Common SPMI regulator register offsets */
#define SPMI_COMMON_REG_TYPE         0x04
#define SPMI_COMMON_REG_SUBTYPE      0x05
#define SPMI_COMMON_REG_VOLTAGE_RANGE 0x40
#define SPMI_COMMON_REG_VOLTAGE_SET  0x41
#define SPMI_COMMON_REG_MODE         0x45
#define SPMI_COMMON_REG_ENABLE       0x46

/* GPIO register offsets */
#define GPIO_STATUS                  0x08
#define GPIO_CTL                     0x40

/* Temperature alarm registers */
#define TEMP_ALARM_STATUS            0x08
#define TEMP_ALARM_CONFIG            0x58

/* VADC registers */
#define VADC_STATUS1                 0x08
#define VADC_MODE_CTL                0x40
#define VADC_EN_CTL1                 0x46
#define VADC_CHANNEL_SEL             0x48
#define VADC_INT_EN                  0x50
#define VADC_CONV_REQ                0x52
#define VADC_DATA                    0x60

static void pmr735a_adc_conversion_complete(void *opaque)
{
    PMR735AState *s = PMR735A(opaque);

    /* Mark conversion complete */
    s->vadc_status1 |= 0x01;  /* Set EOC (End of Conversion) bit */
    s->vadc_conversion_active = false;

    /* Generate realistic ADC data based on peripheral power rails */
    switch (s->vadc_channel) {
    case 0:  /* WiFi core rail (L1 - 1.2V) */
        s->vadc_data = 0x1200 + (rand() % 30);
        break;
    case 1:  /* WiFi I/O rail (L2 - 1.8V) */
        s->vadc_data = 0x1800 + (rand() % 30);
        break;
    case 2:  /* WiFi RF rail (L3 - 3.3V) */
        s->vadc_data = 0x3300 + (rand() % 50);
        break;
    case 3:  /* Sensor rail (L4 - 1.8V) */
        s->vadc_data = 0x1800 + (rand() % 25);
        break;
    default: /* Default to VDD measurement */
        s->vadc_data = 0x3300 + (rand() % 100);
        break;
    }

    /* Generate ADC completion interrupt */
    if (s->vadc_interrupt_enabled && s->adc_irq) {
        qemu_irq_pulse(s->adc_irq);
    }
}

static uint64_t pmr735a_read(void *opaque, hwaddr offset, unsigned size)
{
    PMR735AState *s = PMR735A(opaque);
    uint32_t regulator_base = 0;
    int regulator_idx = -1;
    uint64_t value = 0;

    /* PMIC identification registers */
    switch (offset) {
    case PMR735A_REV2:
        value = s->pmic_rev[0];
        break;
    case PMR735A_REV3:
        value = s->pmic_rev[1];
        break;
    case PMR735A_REV4:
        value = s->pmic_rev[2];
        break;
    case PMR735A_TYPE:
        value = s->pmic_type;
        break;
    case PMR735A_SUBTYPE:
        value = s->pmic_subtype;
        break;
    case PMR735A_FAB_ID:
        value = s->fab_id;
        break;
    }

    /* GPIO registers - simple addressing */
    if (offset >= PMR735A_GPIO_BASE && offset < PMR735A_GPIO_BASE + 16) {
        int gpio_num = offset - PMR735A_GPIO_BASE;
        value = s->gpio_states[gpio_num];
        goto done;
    }

    /* Temperature alarm */
    if (offset == PMR735A_TEMP_ALARM_BASE + TEMP_ALARM_STATUS) {
        value = s->temp_alarm_status;
        goto done;
    }
    if (offset == PMR735A_TEMP_ALARM_BASE + TEMP_ALARM_CONFIG) {
        value = s->temp_alarm_config;
        goto done;
    }

    /* VADC registers */
    if (offset == PMR735A_VADC_BASE + VADC_STATUS1) {
        value = s->vadc_status1;
        goto done;
    }
    if (offset == PMR735A_VADC_BASE + VADC_MODE_CTL) {
        value = s->vadc_mode_ctl;
        goto done;
    }
    if (offset == PMR735A_VADC_BASE + VADC_EN_CTL1) {
        value = s->vadc_en_ctl1;
        goto done;
    }
    if (offset == PMR735A_VADC_BASE + VADC_CHANNEL_SEL) {
        value = s->vadc_channel;
        goto done;
    }
    if (offset == PMR735A_VADC_BASE + VADC_INT_EN) {
        value = s->vadc_interrupt_enabled ? 0x01 : 0x00;
        goto done;
    }
    if (offset == PMR735A_VADC_BASE + VADC_DATA) {
        /* STATUS1 automatically cleared on DATA read */
        s->vadc_status1 &= ~0x01;
        value = s->vadc_data & 0xFF;
        goto done;
    }
    if (offset == PMR735A_VADC_BASE + VADC_DATA + 1) {
        value = (s->vadc_data >> 8) & 0xFF;
        goto done;
    }

    /* Regulator registers */
    if (offset >= PMR735A_L1_BASE && offset < PMR735A_L1_BASE + 0x50) {
        regulator_base = PMR735A_L1_BASE;
        regulator_idx = 0;
    } else if (offset >= PMR735A_L2_BASE && offset < PMR735A_L2_BASE + 0x50) {
        regulator_base = PMR735A_L2_BASE;
        regulator_idx = 1;
    } else if (offset >= PMR735A_L3_BASE && offset < PMR735A_L3_BASE + 0x50) {
        regulator_base = PMR735A_L3_BASE;
        regulator_idx = 2;
    } else if (offset >= PMR735A_L4_BASE && offset < PMR735A_L4_BASE + 0x50) {
        regulator_base = PMR735A_L4_BASE;
        regulator_idx = 3;
    } else if (offset >= PMR735A_L5_BASE && offset < PMR735A_L5_BASE + 0x50) {
        regulator_base = PMR735A_L5_BASE;
        regulator_idx = 4;
    } else if (offset >= PMR735A_L6_BASE && offset < PMR735A_L6_BASE + 0x50) {
        regulator_base = PMR735A_L6_BASE;
        regulator_idx = 5;
    } else if (offset >= PMR735A_L7_BASE && offset < PMR735A_L7_BASE + 0x50) {
        regulator_base = PMR735A_L7_BASE;
        regulator_idx = 6;
    }

    if (regulator_idx >= 0) {
        PMR735ARegulator *reg = &s->regulators[regulator_idx];
        uint32_t reg_offset = offset - regulator_base;

        switch (reg_offset) {
        case SPMI_COMMON_REG_TYPE:
            value = reg->type;
            break;
        case SPMI_COMMON_REG_SUBTYPE:
            value = reg->subtype;
            break;
        case SPMI_COMMON_REG_VOLTAGE_RANGE:
            value = reg->voltage_range;
            break;
        case SPMI_COMMON_REG_VOLTAGE_SET:
            value = reg->voltage_set;
            break;
        case SPMI_COMMON_REG_MODE:
            value = reg->mode;
            break;
        case SPMI_COMMON_REG_ENABLE:
            value = reg->enable;
            break;
        }
    }

done:
    return value;
}

static void pmr735a_write(void *opaque, hwaddr offset, uint64_t value,
                          unsigned size)
{
    PMR735AState *s = PMR735A(opaque);
    uint32_t regulator_base = 0;
    int regulator_idx = -1;

    /* Read-only registers */
    if (offset >= PMR735A_REV2 && offset <= PMR735A_FAB_ID) {
        return;  /* Ignore writes to read-only identification registers */
    }

    /* GPIO registers - simple addressing */
    if (offset >= PMR735A_GPIO_BASE && offset < PMR735A_GPIO_BASE + 16) {
        int gpio_num = offset - PMR735A_GPIO_BASE;
        s->gpio_states[gpio_num] = value & 0xFF;

        /* Check for GPIO enable bit to trigger side effects */
        if (value & 0x80) {
            /* Generate GPIO change interrupt */
            qemu_irq_pulse(s->gpio_irq);
        }
        return;
    }

    /* Temperature alarm */
    if (offset == PMR735A_TEMP_ALARM_BASE + TEMP_ALARM_CONFIG) {
        s->temp_alarm_config = value & 0xFF;
        return;
    }

    /* VADC control */
    if (offset == PMR735A_VADC_BASE + VADC_MODE_CTL) {
        s->vadc_mode_ctl = value & 0xFF;
        return;
    }
    if (offset == PMR735A_VADC_BASE + VADC_EN_CTL1) {
        s->vadc_en_ctl1 = value & 0xFF;
        return;
    }
    if (offset == PMR735A_VADC_BASE + VADC_CHANNEL_SEL) {
        s->vadc_channel = value & 0xFF;
        return;
    }
    if (offset == PMR735A_VADC_BASE + VADC_INT_EN) {
        s->vadc_interrupt_enabled = (value & 0x01) != 0;
        return;
    }
    if (offset == PMR735A_VADC_BASE + VADC_CONV_REQ) {
        if (value & 0x80) {
            /* Start ADC conversion */
            s->vadc_status1 &= ~0x01;  /* Clear EOC bit */
            s->vadc_conversion_active = true;
            /* Simulate 5ms conversion time */
            timer_mod(s->adc_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 5000000);
        }
        return;
    }

    /* Regulator registers */
    if (offset >= PMR735A_L1_BASE && offset < PMR735A_L1_BASE + 0x50) {
        regulator_base = PMR735A_L1_BASE;
        regulator_idx = 0;
    } else if (offset >= PMR735A_L2_BASE && offset < PMR735A_L2_BASE + 0x50) {
        regulator_base = PMR735A_L2_BASE;
        regulator_idx = 1;
    } else if (offset >= PMR735A_L3_BASE && offset < PMR735A_L3_BASE + 0x50) {
        regulator_base = PMR735A_L3_BASE;
        regulator_idx = 2;
    } else if (offset >= PMR735A_L4_BASE && offset < PMR735A_L4_BASE + 0x50) {
        regulator_base = PMR735A_L4_BASE;
        regulator_idx = 3;
    } else if (offset >= PMR735A_L5_BASE && offset < PMR735A_L5_BASE + 0x50) {
        regulator_base = PMR735A_L5_BASE;
        regulator_idx = 4;
    } else if (offset >= PMR735A_L6_BASE && offset < PMR735A_L6_BASE + 0x50) {
        regulator_base = PMR735A_L6_BASE;
        regulator_idx = 5;
    } else if (offset >= PMR735A_L7_BASE && offset < PMR735A_L7_BASE + 0x50) {
        regulator_base = PMR735A_L7_BASE;
        regulator_idx = 6;
    }

    if (regulator_idx >= 0) {
        PMR735ARegulator *reg = &s->regulators[regulator_idx];
        uint32_t reg_offset = offset - regulator_base;

        switch (reg_offset) {
        case SPMI_COMMON_REG_VOLTAGE_RANGE:
            reg->voltage_range = value & 0xFF;
            break;
        case SPMI_COMMON_REG_VOLTAGE_SET:
            /* Voltage can only be set if range is configured */
            if (reg->voltage_range == 0) {
                return;
            }
            reg->voltage_set = value & 0xFF;
            /* Update actual voltage based on range and setting */
            switch (reg->voltage_range) {
            case 0x01:  /* 0.6-1.8V */
                reg->voltage_mv = 600 + (value * 12);
                break;
            case 0x02:  /* 1.2-2.4V */
                reg->voltage_mv = 1200 + (value * 12);
                break;
            case 0x03:  /* 1.8-3.3V */
                reg->voltage_mv = 1800 + (value * 25);
                break;
            default:  /* Default fallback */
                reg->voltage_mv = 1800;
                break;
            }
            break;
        case SPMI_COMMON_REG_MODE:
            reg->mode = value & 0xFF;
            /* Mode changes affect power consumption */
            if ((value & 0x07) == 0x05) {  /* LPM mode */
                /* Low power mode */
            } else if ((value & 0x07) == 0x07) {  /* Auto/HPM mode */
                /* High performance mode */
            }
            break;
        case SPMI_COMMON_REG_ENABLE:
            reg->enable = value & 0xFF;
            reg->enabled = (value & 0x80) != 0;
            if (reg->enabled) {
                /* Verify voltage range and setting are configured */
                if (reg->voltage_range == 0 || reg->voltage_set == 0) {
                    /* Configuration incomplete */
                }
            }
            break;
        }
    }
}

static const MemoryRegionOps pmr735a_ops = {
    .read = pmr735a_read,
    .write = pmr735a_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void pmr735a_reset_enter(Object *obj, ResetType type)
{
    PMR735AState *s = PMR735A(obj);

    /* Initialize PMIC identification */
    s->pmic_type = 0x51;  /* Standard PMIC type */
    s->pmic_subtype = PMR735A_SUBTYPE_VALUE;
    s->pmic_rev[0] = 0x00;  /* Rev2 */
    s->pmic_rev[1] = 0x01;  /* Rev3 */
    s->pmic_rev[2] = 0x03;  /* Rev4 */
    s->fab_id = 0x00;

    /* Initialize peripheral power regulators with typical configurations */
    /* L1: 1.2V WiFi core */
    s->regulators[0] = (PMR735ARegulator){
        .type = 0x03, .subtype = 0x0a, .voltage_range = 0x01,
        .voltage_set = 0x30, .mode = 0x07, .enable = 0x80,
        .enabled = true, .voltage_mv = 1200
    };

    /* L2: 1.8V WiFi I/O */
    s->regulators[1] = (PMR735ARegulator){
        .type = 0x03, .subtype = 0x0a, .voltage_range = 0x02,
        .voltage_set = 0x38, .mode = 0x07, .enable = 0x80,
        .enabled = true, .voltage_mv = 1800
    };

    /* L3: 3.3V WiFi RF */
    s->regulators[2] = (PMR735ARegulator){
        .type = 0x03, .subtype = 0x0a, .voltage_range = 0x03,
        .voltage_set = 0x3E, .mode = 0x07, .enable = 0x80,
        .enabled = true, .voltage_mv = 3300
    };

    /* L4: 1.8V sensors */
    s->regulators[3] = (PMR735ARegulator){
        .type = 0x03, .subtype = 0x0a, .voltage_range = 0x02,
        .voltage_set = 0x38, .mode = 0x07, .enable = 0x80,
        .enabled = true, .voltage_mv = 1800
    };

    /* L5: 3.0V sensor analog */
    s->regulators[4] = (PMR735ARegulator){
        .type = 0x03, .subtype = 0x0a, .voltage_range = 0x03,
        .voltage_set = 0x3C, .mode = 0x07, .enable = 0x80,
        .enabled = true, .voltage_mv = 3000
    };

    /* L6: 1.2V connectivity */
    s->regulators[5] = (PMR735ARegulator){
        .type = 0x03, .subtype = 0x0a, .voltage_range = 0x01,
        .voltage_set = 0x30, .mode = 0x07, .enable = 0x80,
        .enabled = true, .voltage_mv = 1200
    };

    /* L7: 2.8V peripheral */
    s->regulators[6] = (PMR735ARegulator){
        .type = 0x03, .subtype = 0x0a, .voltage_range = 0x03,
        .voltage_set = 0x38, .mode = 0x07, .enable = 0x80,
        .enabled = true, .voltage_mv = 2800
    };

    /* Initialize GPIO states */
    memset(s->gpio_states, 0, sizeof(s->gpio_states));

    /* Initialize other states */
    s->temp_alarm_status = 0x00;
    s->temp_alarm_config = 0x00;
    s->vadc_status1 = 0x00;
    s->vadc_mode_ctl = 0x00;
    s->vadc_en_ctl1 = 0x00;
    s->vadc_data = 0x0000;
    s->vadc_conversion_active = false;
    s->vadc_interrupt_enabled = false;
}

static void pmr735a_realize(DeviceState *dev, Error **errp)
{
    PMR735AState *s = PMR735A(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &pmr735a_ops, s,
                          TYPE_PMR735A, 0x10000);
    sysbus_init_mmio(sbd, &s->iomem);

    /* Initialize timer for ADC conversions */
    s->adc_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                pmr735a_adc_conversion_complete, s);

    /* Initialize IRQ outputs */
    sysbus_init_irq(sbd, &s->gpio_irq);
    sysbus_init_irq(sbd, &s->temp_irq);
    sysbus_init_irq(sbd, &s->adc_irq);
}

static void pmr735a_unrealize(DeviceState *dev)
{
    PMR735AState *s = PMR735A(dev);

    if (s->adc_timer) {
        timer_free(s->adc_timer);
    }
}

static void pmr735a_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = pmr735a_realize;
    dc->unrealize = pmr735a_unrealize;
    rc->phases.enter = pmr735a_reset_enter;
}

/* SPMI callback wrapper functions */
static uint64_t pmr735a_spmi_read(void *opaque, uint16_t addr, unsigned size)
{
    return pmr735a_read(opaque, addr, size);
}

static void pmr735a_spmi_write(void *opaque, uint16_t addr, uint64_t value,
                              unsigned size)
{
    pmr735a_write(opaque, addr, value, size);
}

/* Function to register with SPMI controller */
void pmr735a_register_with_spmi(PMR735AState *s,
                               SPMIControllerState *spmi_controller,
                               uint8_t slave_id)
{
    spmi_register_slave(spmi_controller, slave_id, DEVICE(s), "PMR735A",
                       pmr735a_spmi_read, pmr735a_spmi_write, s);
}

static const TypeInfo pmr735a_info = {
    .name = TYPE_PMR735A,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PMR735AState),
    .class_init = pmr735a_class_init,
};

static void pmr735a_register_types(void)
{
    type_register_static(&pmr735a_info);
}

type_init(pmr735a_register_types)
