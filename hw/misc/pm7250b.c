/*
 * Qualcomm PM7250B PMIC device model
 *
 * Copyright (c) 2025 Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This model implements the PM7250B PMIC, which provides additional
 * power rails and battery management for QCS6490 SoCs.
 */

#include "qemu/osdep.h"
#include "hw/misc/pm7250b.h"
#include "hw/misc/spmi-controller.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qapi/error.h"

/* Default regulator configurations for PM7250B */
static const PM7250BRegulator pm7250b_regulators[] = {
    { 0x04, 0x10, 0x02, 0x20, 0x07, 0x80, true, 1200, "l1" }, /* 1.2V digital */
    { 0x04, 0x10, 0x02, 0x30, 0x07, 0x80, true, 1800, "l2" },  /* 1.8V I/O */
    { 0x04, 0x10, 0x03, 0x2C, 0x07, 0x00, false, 0, "l3" },    /* 2.9V analog */
    { 0x04, 0x10, 0x02, 0x30, 0x07, 0x80, true, 1800, "l4" },  /* 1.8V memory */
    { 0x04, 0x10, 0x01, 0x10, 0x07, 0x00, false, 0, "l5" }, /* 0.88V low pwr */
    { 0x04, 0x10, 0x02, 0x30, 0x07, 0x80, true, 1800, "l6" }, /* 1.8V sensors */
};

static PM7250BRegulator *pm7250b_find_regulator(PM7250BState *s, uint16_t addr)
{
    /* Map to specific regulator base addresses */
    static const uint16_t regulator_bases[] = {
        PM7250B_L1_BASE, PM7250B_L2_BASE, PM7250B_L3_BASE,
        PM7250B_L4_BASE, PM7250B_L5_BASE, PM7250B_L6_BASE
    };

    for (int i = 0; i < PM7250B_MAX_REGULATORS; i++) {
        if (addr >= regulator_bases[i] && addr < regulator_bases[i] + 0x50) {
            return &s->regulators[i];
        }
    }
    return NULL;
}

/* Enhanced timer callback for ADC conversion completion */
static void pm7250b_adc_conversion_complete(void *opaque)
{
    PM7250BState *s = PM7250B(opaque);

    /* Mark conversion complete - key side effect */
    s->vadc_status1 |= 0x01;  /* Set EOC (End of Conversion) bit */
    s->vadc_conversion_active = false;

    /* Generate realistic ADC data based on selected channel */
    switch (s->vadc_channel) {
    case 0:  /* VPH_PWR - Main power rail (~4.2V) */
        s->vadc_data = 0x4200 + (rand() % 100);
        break;
    case 1:  /* VBAT - Battery voltage (~3.8V) */
        s->vadc_data = 0x3800 + (rand() % 200);
        break;
    case 2:  /* IBAT - Battery current (in mA, ~500mA) */
        s->vadc_data = 0x01F4 + (rand() % 100);  /* 500 +/- 100 mA */
        break;
    case 3:  /* L1 regulator (1.2V) */
        s->vadc_data = 0x1200 + (rand() % 30);
        break;
    case 4:  /* L2/L4/L6 regulator (1.8V) */
        s->vadc_data = 0x1800 + (rand() % 40);
        break;
    case 5:  /* DIE_TEMP - Die temperature */
        s->vadc_data = 0x0320 + (rand() % 50);  /* ~50C + variation */
        break;
    default: /* Default to VPH_PWR */
        s->vadc_data = 0x4250 + (rand() % 100);
        break;
    }

    /* Generate ADC completion interrupt if enabled */
    if (s->vadc_interrupt_enabled && s->adc_irq) {
        qemu_irq_pulse(s->adc_irq);
    }
}

static uint64_t pm7250b_read(void *opaque, hwaddr addr, unsigned size)
{
    PM7250BState *s = PM7250B(opaque);
    uint16_t offset = addr;
    uint64_t value = 0;

    /* PMIC identification registers */
    if (offset == 0x104) {
        value = 0x51;  /* PMIC_TYPE */
    } else if (offset == 0x105) {
        value = PM7250B_SUBTYPE_VALUE;  /* PMIC_SUBTYPE */
    } else if (offset >= 0x101 && offset <= 0x103) {
        value = s->pmic_rev[offset - 0x101];  /* REV2, REV3, REV4 */
    } else if (offset == 0x1f2) {
        value = s->fab_id;  /* FAB_ID */

    /* Battery charger registers */
    } else if (offset >= PM7250B_CHARGER_BASE &&
               offset < PM7250B_CHARGER_BASE + 0x100) {
        switch (offset - PM7250B_CHARGER_BASE) {
        case CHGR_BATTERY_CHARGER_STATUS_1:
            value = s->chgr_status1;
            break;
        case CHGR_BATTERY_CHARGER_STATUS_2:
            value = s->chgr_status2;
            break;
        case CHGR_BATTERY_CHARGER_STATUS_3:
            value = s->chgr_status3;
            break;
        case CHGR_CHARGING_ENABLE_CMD:
            value = s->chgr_enable_cmd;
            break;
        case CHGR_CHGR_CFG2:
            value = s->chgr_cfg2;
            break;
        case CHGR_FLOAT_VOLTAGE_CFG:
            value = s->chgr_float_voltage;
            break;
        default:
            break;
        }

    /* Regulator registers */
    } else {
        PM7250BRegulator *reg = pm7250b_find_regulator(s, offset);
        if (reg) {
            /* Calculate regulator index to get correct base address */
            static const uint16_t regulator_bases[] = {
                PM7250B_L1_BASE, PM7250B_L2_BASE, PM7250B_L3_BASE,
                PM7250B_L4_BASE, PM7250B_L5_BASE, PM7250B_L6_BASE
            };
            int reg_index = reg - s->regulators;
            uint8_t reg_offset = offset - regulator_bases[reg_index];
            switch (reg_offset) {
            case 0x04: /* Type */
                value = reg->type;
                break;
            case 0x05: /* Subtype */
                value = reg->subtype;
                break;
            case 0x40: /* Voltage range */
                value = reg->voltage_range;
                break;
            case 0x41: /* Voltage set */
                value = reg->voltage_set;
                break;
            case 0x45: /* Mode */
                value = reg->mode;
                break;
            case 0x46: /* Enable */
                value = reg->enable;
                break;
            default:
                break;
            }

        /* GPIO registers */
        } else if (offset >= PM7250B_GPIO_BASE &&
                   offset < PM7250B_GPIO_BASE + 16) {
            int gpio_num = offset - PM7250B_GPIO_BASE;
            value = s->gpio_states[gpio_num];

        /* VADC registers */
        } else if (offset >= PM7250B_VADC_BASE &&
                   offset < PM7250B_VADC_BASE + 0x100) {
            switch (offset - PM7250B_VADC_BASE) {
            case 0x08: /* STATUS1 */
                value = s->vadc_status1;
                /* Clear EOC bit on read */
                s->vadc_status1 &= ~0x01;
                break;
            case 0x40: /* MODE_CTL */
                value = s->vadc_mode_ctl;
                break;
            case 0x46: /* EN_CTL1 */
                value = s->vadc_en_ctl1;
                break;
            case 0x48: /* CHANNEL_SEL */
                value = s->vadc_channel;
                break;
            case 0x50: /* INT_EN */
                value = s->vadc_interrupt_enabled ? 0x01 : 0x00;
                break;
            case 0x60: /* DATA low */
                /* STATUS1 auto-cleared on DATA read (key side effect) */
                s->vadc_status1 &= ~0x01;
                value = s->vadc_data & 0xff;
                break;
            case 0x61: /* DATA high */
                value = (s->vadc_data >> 8) & 0xff;
                break;
            default:
                break;
            }
        }
    }

    return value;
}

static void pm7250b_write(void *opaque, hwaddr addr, uint64_t value,
                         unsigned size)
{
    PM7250BState *s = PM7250B(opaque);
    uint16_t offset = addr;
    uint8_t val = (uint8_t)value;

    /* Most PMIC identification registers are read-only */
    if (offset >= 0x101 && offset <= 0x1f2) {
        return;
    }

    /* Battery charger registers */
    if (offset >= PM7250B_CHARGER_BASE &&
        offset < PM7250B_CHARGER_BASE + 0x100) {
        switch (offset - PM7250B_CHARGER_BASE) {
        case CHGR_CHARGING_ENABLE_CMD:
            s->chgr_enable_cmd = val;
            break;
        case CHGR_CHGR_CFG2:
            s->chgr_cfg2 = val;
            break;
        case CHGR_FLOAT_VOLTAGE_CFG:
            s->chgr_float_voltage = val;
            break;
        default:
            break;
        }

    /* Regulator registers */
    } else {
        PM7250BRegulator *reg = pm7250b_find_regulator(s, offset);
        if (reg) {
            /* Calculate regulator index to get correct base address */
            static const uint16_t regulator_bases[] = {
                PM7250B_L1_BASE, PM7250B_L2_BASE, PM7250B_L3_BASE,
                PM7250B_L4_BASE, PM7250B_L5_BASE, PM7250B_L6_BASE
            };
            int reg_index = reg - s->regulators;
            uint8_t reg_offset = offset - regulator_bases[reg_index];
            switch (reg_offset) {
            case 0x40: /* Voltage range */
                reg->voltage_range = val;
                break;
            case 0x41: /* Voltage set */
                reg->voltage_set = val;
                break;
            case 0x45: /* Mode */
                reg->mode = val;
                break;
            case 0x46: /* Enable */
                reg->enable = val;
                reg->enabled = (val & 0x80) ? true : false;
                break;
            default:
                break;
            }

        /* GPIO registers */
        } else if (offset >= PM7250B_GPIO_BASE &&
                   offset < PM7250B_GPIO_BASE + 16) {
            int gpio_num = offset - PM7250B_GPIO_BASE;
            s->gpio_states[gpio_num] = val;

        /* VADC registers */
        } else if (offset >= PM7250B_VADC_BASE &&
                   offset < PM7250B_VADC_BASE + 0x100) {
            switch (offset - PM7250B_VADC_BASE) {
            case 0x40: /* MODE_CTL */
                s->vadc_mode_ctl = val;
                break;
            case 0x46: /* EN_CTL1 */
                s->vadc_en_ctl1 = val;
                break;
            case 0x48: /* CHANNEL_SEL */
                s->vadc_channel = val;
                break;
            case 0x50: /* INT_EN */
                s->vadc_interrupt_enabled = (val & 0x01) ? true : false;
                break;
            case 0x52: /* CONV_REQ */
                /* Trigger ADC conversion if request bit set */
                if (val & 0x80) {
                    if (!s->vadc_conversion_active) {
                        s->vadc_conversion_active = true;
                        s->vadc_status1 &= ~0x01;  /* Clear EOC bit */
                        /* Start conversion with realistic timing (~7ms avg) */
                        timer_mod(s->adc_timer,
                                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                                  (6500 + (rand() % 1000)) * 1000);
                    }
                }
                break;
            default:
                break;
            }
        }
    }
}

static const MemoryRegionOps pm7250b_ops = {
    .read = pm7250b_read,
    .write = pm7250b_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void pm7250b_reset_enter(Object *obj, ResetType type)
{
    PM7250BState *s = PM7250B(obj);

    /* Initialize PMIC identification */
    s->pmic_type = 0x51;
    s->pmic_subtype = PM7250B_SUBTYPE_VALUE;
    s->pmic_rev[0] = 0x00;  /* REV2 */
    s->pmic_rev[1] = 0x01;  /* REV3 */
    s->pmic_rev[2] = 0x02;  /* REV4 */
    s->fab_id = 0x00;

    /* Initialize regulators */
    memcpy(s->regulators, pm7250b_regulators, sizeof(pm7250b_regulators));

    /* Initialize battery charger state (simulate connected battery) */
    s->chgr_status1 = 0x01;  /* Battery present */
    s->chgr_status2 = 0x08;  /* Valid input voltage */
    s->chgr_status3 = 0x00;  /* Not charging initially */
    s->chgr_enable_cmd = 0x00;  /* Charger disabled */
    s->chgr_cfg2 = 0x06;    /* Battery detect enabled */
    s->chgr_float_voltage = 0x2A;  /* ~4.35V float voltage */

    /* Initialize GPIO states */
    memset(s->gpio_states, 0, sizeof(s->gpio_states));

    /* Initialize temperature alarm */
    s->temp_alarm_status = 0x00;
    s->temp_alarm_config = 0x00;

    /* Initialize VADC */
    s->vadc_status1 = 0x00;
    s->vadc_mode_ctl = 0x00;
    s->vadc_en_ctl1 = 0x00;
    s->vadc_data = 0x0000;
    s->vadc_conversion_active = false;
    s->vadc_interrupt_enabled = false;
}

static void pm7250b_realize(DeviceState *dev, Error **errp)
{
    PM7250BState *s = PM7250B(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &pm7250b_ops, s,
                          TYPE_PM7250B, 0x10000);
    sysbus_init_mmio(sbd, &s->iomem);

    /* Initialize interrupt lines */
    sysbus_init_irq(sbd, &s->adc_irq);    /* ADC End-of-Conversion interrupt */
    sysbus_init_irq(sbd, &s->gpio_irq);   /* GPIO state change interrupt */
    sysbus_init_irq(sbd, &s->temp_irq);   /* Temperature alarm interrupt */
    sysbus_init_irq(sbd, &s->chgr_irq);   /* Charger status interrupt */

    /* Initialize ADC conversion timer */
    s->adc_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                pm7250b_adc_conversion_complete, s);
}

static void pm7250b_unrealize(DeviceState *dev)
{
    PM7250BState *s = PM7250B(dev);

    if (s->adc_timer) {
        timer_del(s->adc_timer);
        timer_free(s->adc_timer);
    }
}

static void pm7250b_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = pm7250b_realize;
    dc->unrealize = pm7250b_unrealize;
    dc->desc = "Qualcomm PM7250B PMIC (Battery Management for QCS6490)";
    rc->phases.enter = pm7250b_reset_enter;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo pm7250b_info = {
    .name          = TYPE_PM7250B,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PM7250BState),
    .class_init    = pm7250b_class_init,
};

/* SPMI callback wrapper functions */
static uint64_t pm7250b_spmi_read(void *opaque, uint16_t addr, unsigned size)
{
    return pm7250b_read(opaque, addr, size);
}

static void pm7250b_spmi_write(void *opaque, uint16_t addr, uint64_t value,
                              unsigned size)
{
    pm7250b_write(opaque, addr, value, size);
}

/* Function to register with SPMI controller */
void pm7250b_register_with_spmi(PM7250BState *s,
                               SPMIControllerState *spmi_controller,
                               uint8_t slave_id)
{
    spmi_register_slave(spmi_controller, slave_id, DEVICE(s), "PM7250B",
                       pm7250b_spmi_read, pm7250b_spmi_write, s);
}

static void pm7250b_register_types(void)
{
    type_register_static(&pm7250b_info);
}

type_init(pm7250b_register_types)
