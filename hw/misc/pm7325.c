/*
 * Qualcomm PM7325 PMIC
 *
 * Copyright (c) 2025 Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This implements the PM7325 PMIC device found on QCM6490 hardware.
 * Based on real hardware analysis and Linux drivers.
 */

#include "qemu/osdep.h"
#include "hw/misc/pm7325.h"
#include "hw/misc/spmi-controller.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qapi/error.h"

/* Default regulator configurations for PM7325 - slave 1 range is 0x000-0xFFF */
static const PM7325Regulator pm7325_regulators[] = {
    /* SMPS regulators (s1, s7) - only key ones tested */
    { 0x400, 0x03, 0x0a, 0x00, 0x00, REGULATOR_MODE_NORMAL,
      REGULATOR_ENABLE, true, 1872, "s1" },   /* 1.872V - enabled */
    { 0x500, 0x03, 0x0a, 0x00, 0x00, REGULATOR_MODE_NORMAL,
      REGULATOR_ENABLE, true, 824, "s7" },   /* 824mV - enabled */

    /* LDO regulators - only key ones tested */
    { 0x600, 0x04, 0x10, 0x02, 0x20, REGULATOR_MODE_NORMAL,
      REGULATOR_ENABLE, true, 3050, "l2" },   /* 3.05V - enabled */
    { 0x700, 0x04, 0x10, 0x02, 0x20, REGULATOR_MODE_NORMAL,
      REGULATOR_ENABLE, true, 1200, "l6" },   /* 1.2V - enabled */
    { 0x800, 0x04, 0x10, 0x02, 0x20, REGULATOR_MODE_NORMAL,
      REGULATOR_ENABLE, true, 2952, "l7" },   /* 2.952V - enabled */
    { 0x900, 0x04, 0x10, 0x02, 0x20, REGULATOR_MODE_NORMAL,
      REGULATOR_ENABLE, true, 1200, "l9" },   /* 1.2V - enabled */
    { 0xa00, 0x04, 0x10, 0x02, 0x20, REGULATOR_MODE_NORMAL,
      REGULATOR_ENABLE, true, 1800, "l18" },  /* 1.8V - enabled */
    { 0xb00, 0x04, 0x10, 0x02, 0x20, REGULATOR_MODE_NORMAL,
      REGULATOR_ENABLE, true, 1800, "l19" },  /* 1.8V - enabled */
};

static PM7325Regulator *pm7325_find_regulator(PM7325State *s, uint16_t addr)
{
    uint16_t base = addr & 0xff00; /* Get 256-byte aligned base */

    for (int i = 0; i < s->num_regulators; i++) {
        if (s->regulators[i].base_addr == base) {
            return &s->regulators[i];
        }
    }
    return NULL;
}

/* Enhanced timer callback for ADC conversion completion */
static void pm7325_adc_conversion_complete(void *opaque)
{
    PM7325State *s = PM7325(opaque);

    /* Mark conversion complete - key side effect */
    s->vadc_status1 |= 0x01;  /* Set EOC (End of Conversion) bit */
    s->vadc_conversion_active = false;

    /* Generate realistic ADC data based on selected channel */
    switch (s->vadc_channel) {
    case 0:  /* VPH_PWR - main power rail (~4.2V) */
        s->vadc_data = 0x4200 + (rand() % 100);
        break;
    case 1:  /* S1 Buck regulator (0.8V) */
        s->vadc_data = 0x0800 + (rand() % 20);
        break;
    case 2:  /* L7 LDO regulator (2.95V) */
        s->vadc_data = 0x2950 + (rand() % 50);
        break;
    case 3:  /* L6 LDO regulator (1.2V) */
        s->vadc_data = 0x1200 + (rand() % 30);
        break;
    case 4:  /* L9 LDO regulator (1.2V) */
        s->vadc_data = 0x1200 + (rand() % 25);
        break;
    default: /* Default to VPH_PWR measurement */
        s->vadc_data = 0x4270 + (rand() % 100);
        break;
    }

    qemu_log_mask(LOG_GUEST_ERROR, "PM7325: ADC conversion complete, "
                  "channel=%d, data=0x%04x\n", s->vadc_channel, s->vadc_data);

    /* Generate ADC completion interrupt if enabled */
    if (s->vadc_interrupt_enabled && s->adc_irq) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "PM7325: Generating ADC EOC interrupt\n");
        qemu_irq_pulse(s->adc_irq);
    }
}

static uint64_t pm7325_read(void *opaque, hwaddr addr, unsigned size)
{
    PM7325State *s = PM7325(opaque);
    uint16_t offset = addr;
    uint64_t value = 0;

    switch (offset) {
    case PMIC_REV2:
        value = s->pmic_rev2;
        break;
    case PMIC_REV3:
        value = s->pmic_rev3;
        break;
    case PMIC_REV4:
        value = s->pmic_rev4;
        break;
    case PMIC_TYPE:
        value = s->pmic_type;
        break;
    case PMIC_SUBTYPE:
        value = s->pmic_subtype;
        break;
    case PMIC_FAB_ID:
        value = s->pmic_fab_id;
        break;
    default:
        /* Check if this is a regulator register */
        PM7325Regulator *reg = pm7325_find_regulator(s, offset);
        if (reg) {
            uint8_t reg_offset = offset & 0xff;
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
            default:
                qemu_log_mask(LOG_UNIMP,
                              "PM7325: unimplemented regulator register "
                              "read at 0x%04x\n", offset);
                break;
            }
        } else if (offset >= PM7325_GPIO_BASE &&
                   offset < PM7325_GPIO_BASE + PM7325_GPIO_COUNT) {
            /* GPIO registers */
            int gpio_num = offset - PM7325_GPIO_BASE;
            value = s->gpio_states[gpio_num];
        } else if (offset >= PM7325_TEMP_ALARM_BASE &&
                   offset < PM7325_TEMP_ALARM_BASE + 0x100) {
            /* Temperature alarm registers */
            switch (offset - PM7325_TEMP_ALARM_BASE) {
            case 0x08: /* Status register */
                value = s->temp_alarm_status;
                break;
            case 0x58: /* Config register */
                value = s->temp_alarm_config;
                break;
            default:
                qemu_log_mask(LOG_UNIMP,
                              "PM7325: unimplemented temp alarm register "
                              "read at 0x%04x\n", offset);
                break;
            }
        } else if (offset >= PM7325_VADC_BASE &&
                   offset < PM7325_VADC_BASE + 0x100) {
            /* VADC registers */
            switch (offset - PM7325_VADC_BASE) {
            case VADC_STATUS1:
                value = s->vadc_status1;
                /* Clear EOC bit on read */
                s->vadc_status1 &= ~0x01;
                break;
            case VADC_MODE_CTL:
                value = s->vadc_mode_ctl;
                break;
            case VADC_EN_CTL1:
                value = s->vadc_en_ctl1;
                break;
            case VADC_CHANNEL_SEL:
                value = s->vadc_channel;
                break;
            case VADC_INT_EN:
                value = s->vadc_interrupt_enabled ? 0x01 : 0x00;
                break;
            case VADC_CONV_REQ:
                value = s->vadc_conv_req;
                break;
            case VADC_DATA:
                /* STATUS1 auto-cleared on DATA read (key side effect) */
                s->vadc_status1 &= ~0x01;
                value = s->vadc_data & 0xff;  /* Low byte */
                break;
            case VADC_DATA + 1:
                value = (s->vadc_data >> 8) & 0xff;  /* High byte */
                break;
            default:
                qemu_log_mask(LOG_UNIMP,
                              "PM7325: unimplemented VADC register "
                              "read at 0x%04x\n", offset);
                break;
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "PM7325: invalid register read at 0x%04x\n", offset);
        }
        break;
    }

    qemu_log_mask(LOG_TRACE, "PM7325: read 0x%02x from offset 0x%04x\n",
                  (uint32_t)value, offset);
    return value;
}

static void pm7325_write(void *opaque, hwaddr addr, uint64_t value,
                        unsigned size)
{
    PM7325State *s = PM7325(opaque);
    uint16_t offset = addr;
    uint8_t val = (uint8_t)value;

    qemu_log_mask(LOG_TRACE, "PM7325: write 0x%02x to offset 0x%04x\n",
                  val, offset);

    /* Most PMIC identification registers are read-only */
    if (offset >= PMIC_REV2 && offset <= PMIC_FAB_ID) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "PM7325: write to read-only register 0x%04x\n", offset);
        return;
    }

    /* Check if this is a regulator register */
    PM7325Regulator *reg = pm7325_find_regulator(s, offset);
    if (reg) {
        uint8_t reg_offset = offset & 0xff;
        switch (reg_offset) {
        case SPMI_COMMON_REG_VOLTAGE_RANGE:
            reg->voltage_range = val;
            qemu_log_mask(LOG_TRACE,
                          "PM7325: %s voltage range set to 0x%02x\n",
                          reg->name, val);
            break;
        case SPMI_COMMON_REG_VOLTAGE_SET:
            reg->voltage_set = val;
            qemu_log_mask(LOG_TRACE,
                          "PM7325: %s voltage set to 0x%02x\n",
                          reg->name, val);
            break;
        case SPMI_COMMON_REG_MODE:
            reg->mode = val;
            qemu_log_mask(LOG_TRACE,
                          "PM7325: %s mode set to 0x%02x\n",
                          reg->name, val);
            break;
        case SPMI_COMMON_REG_ENABLE:
            reg->enable = val;
            qemu_log_mask(LOG_TRACE,
                          "PM7325: %s enable set to 0x%02x\n",
                          reg->name, val);
            break;
        default:
            qemu_log_mask(LOG_UNIMP,
                          "PM7325: unimplemented regulator register "
                          "write at 0x%04x\n", offset);
            break;
        }
    } else if (offset >= PM7325_GPIO_BASE &&
               offset < PM7325_GPIO_BASE + PM7325_GPIO_COUNT) {
        /* GPIO registers - implement cascading effects */
        int gpio_num = offset - PM7325_GPIO_BASE;
        uint8_t old_val = s->gpio_states[gpio_num];
        s->gpio_states[gpio_num] = val;

        /* Log significant changes that might affect external pins */
        if ((old_val ^ val) & 0x80) {  /* Enable bit changed */
            qemu_log_mask(LOG_TRACE, "PM7325: GPIO %d enable %s\n",
                          gpio_num, (val & 0x80) ? "enabled" : "disabled");
        }
    } else if (offset >= PM7325_TEMP_ALARM_BASE &&
               offset < PM7325_TEMP_ALARM_BASE + 0x100) {
        /* Temperature alarm registers */
        switch (offset - PM7325_TEMP_ALARM_BASE) {
        case 0x58: /* Config register */
            s->temp_alarm_config = val;
            qemu_log_mask(LOG_TRACE,
                          "PM7325: temp alarm config set to 0x%02x\n", val);
            break;
        default:
            qemu_log_mask(LOG_UNIMP,
                          "PM7325: unimplemented temp alarm register "
                          "write at 0x%04x\n", offset);
            break;
        }
    } else if (offset >= PM7325_VADC_BASE &&
               offset < PM7325_VADC_BASE + 0x100) {
        /* VADC registers - implement conversion triggering */
        switch (offset - PM7325_VADC_BASE) {
        case VADC_MODE_CTL:
            s->vadc_mode_ctl = val;
            break;
        case VADC_EN_CTL1:
            s->vadc_en_ctl1 = val;
            break;
        case VADC_CHANNEL_SEL:
            s->vadc_channel = val;
            qemu_log_mask(LOG_TRACE, "PM7325: ADC channel selected: %d\n", val);
            break;
        case VADC_INT_EN:
            s->vadc_interrupt_enabled = (val & 0x01) ? true : false;
            qemu_log_mask(LOG_TRACE, "PM7325: ADC interrupt %s\n",
                          s->vadc_interrupt_enabled ? "enabled" : "disabled");
            break;
        case VADC_CONV_REQ:
            s->vadc_conv_req = val;
            /* Trigger ADC conversion if request bit set */
            if (val & 0x80) {  /* VADC_CONV_REQ_SET */
                if (!s->vadc_conversion_active) {
                    s->vadc_conversion_active = true;
                    s->vadc_status1 &= ~0x01;  /* Clear EOC bit */
                    /* Start conversion with realistic timing (~6.7ms avg) */
                    timer_mod(s->adc_timer,
                              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                              (6500 + (rand() % 1000)) * 1000);  /* 6.5-7.5ms */
                }
            }
            break;
        default:
            qemu_log_mask(LOG_UNIMP,
                          "PM7325: unimplemented VADC register "
                          "write at 0x%04x\n", offset);
            break;
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "PM7325: invalid register write at 0x%04x\n", offset);
    }
}

static const MemoryRegionOps pm7325_ops = {
    .read = pm7325_read,
    .write = pm7325_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void pm7325_reset_enter(Object *obj, ResetType type)
{
    PM7325State *s = PM7325(obj);

    /* Initialize PMIC identification */
    s->pmic_rev2 = 0x00;
    s->pmic_rev3 = 0x01;
    s->pmic_rev4 = 0x03;
    s->pmic_type = PMIC_TYPE_VALUE;
    s->pmic_subtype = PM7325_SUBTYPE_VALUE;
    s->pmic_fab_id = 0x00;

    /* Initialize regulators */
    s->num_regulators = ARRAY_SIZE(pm7325_regulators);
    memcpy(s->regulators, pm7325_regulators, sizeof(pm7325_regulators));

    /* Initialize GPIO states */
    memset(s->gpio_states, 0, sizeof(s->gpio_states));

    /* Initialize temperature alarm */
    s->temp_alarm_status = 0x00; /* Normal temperature */
    s->temp_alarm_config = 0x00;

    /* Initialize VADC */
    s->vadc_status1 = 0x00;
    s->vadc_mode_ctl = 0x00;
    s->vadc_en_ctl1 = 0x00;
    s->vadc_channel = 0;  /* Start with channel 0 (VPH_PWR) */
    s->vadc_conv_req = 0x00;
    s->vadc_data = 0x0000;
    s->vadc_conversion_active = false;
    s->vadc_interrupt_enabled = false;
}

static void pm7325_realize(DeviceState *dev, Error **errp)
{
    PM7325State *s = PM7325(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &pm7325_ops, s,
                          TYPE_PM7325, PM7325_REGISTER_SPACE_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    /* Initialize interrupt lines */
    sysbus_init_irq(sbd, &s->adc_irq);    /* ADC End-of-Conversion interrupt */
    sysbus_init_irq(sbd, &s->gpio_irq);   /* GPIO state change interrupt */
    sysbus_init_irq(sbd, &s->temp_irq);   /* Temperature alarm interrupt */

    /* Initialize ADC conversion timer */
    s->adc_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                pm7325_adc_conversion_complete, s);
}

static void pm7325_unrealize(DeviceState *dev)
{
    PM7325State *s = PM7325(dev);

    if (s->adc_timer) {
        timer_del(s->adc_timer);
        timer_free(s->adc_timer);
    }
}

static void pm7325_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = pm7325_realize;
    dc->unrealize = pm7325_unrealize;
    dc->desc = "Qualcomm PM7325 PMIC (Primary PMIC for QCM6490)";
    rc->phases.enter = pm7325_reset_enter;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo pm7325_info = {
    .name          = TYPE_PM7325,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PM7325State),
    .class_init    = pm7325_class_init,
};

/* SPMI callback wrapper functions */
static uint64_t pm7325_spmi_read(void *opaque, uint16_t addr, unsigned size)
{
    return pm7325_read(opaque, addr, size);
}

static void pm7325_spmi_write(void *opaque, uint16_t addr, uint64_t value,
                              unsigned size)
{
    pm7325_write(opaque, addr, value, size);
}

/* Function to register with SPMI controller */
void pm7325_register_with_spmi(PM7325State *s,
                               SPMIControllerState *spmi_controller,
                               uint8_t slave_id)
{
    spmi_register_slave(spmi_controller, slave_id, DEVICE(s), "PM7325",
                       pm7325_spmi_read, pm7325_spmi_write, s);
}

static void pm7325_register_types(void)
{
    type_register_static(&pm7325_info);
}

type_init(pm7325_register_types)
