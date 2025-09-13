/*
 * Qualcomm PMK8350 PMIC (Master PMIC)
 *
 * Copyright (c) 2025 Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This implements the PMK8350 Master PMIC device found on QCM6490 hardware.
 * Based on real hardware analysis and Linux drivers.
 */

#include "qemu/osdep.h"
#include "hw/misc/pmk8350.h"
#include "hw/misc/spmi-controller.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qapi/error.h"

/* Default regulator configurations for PMK8350 (minimal set for master PMIC) */
static const PMK8350Regulator pmk8350_regulators[] = {
    /* PMK8350 has limited regulators, mainly for PMIC management */
    { 0x400, 0x03, 0x0a, 0x00, 0x00, REGULATOR_MODE_NORMAL,
      REGULATOR_ENABLE, "ldo1" },   /* Reference regulator */
    { 0x700, 0x03, 0x0a, 0x00, 0x00, REGULATOR_MODE_NORMAL,
      REGULATOR_ENABLE, "bob" },    /* Buck-or-boost regulator */
};

static PMK8350Regulator *pmk8350_find_regulator(PMK8350State *s, uint16_t addr)
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
static void pmk8350_adc_conversion_complete(void *opaque)
{
    PMK8350State *s = PMK8350(opaque);

    /* Mark conversion complete - key side effect */
    s->vadc_status1 |= 0x01;  /* Set EOC (End of Conversion) bit */
    s->vadc_conversion_active = false;

    /* Generate realistic ADC data based on selected channel */
    switch (s->vadc_channel) {
    case 0:  /* VREF_1P25 - Internal reference (1.25V) */
        s->vadc_data = 0x1250 + (rand() % 10);
        break;
    case 1:  /* VPH_PWR - Main power rail (~4.2V) */
        s->vadc_data = 0x4200 + (rand() % 100);
        break;
    case 2:  /* VBAT_SNS - Battery voltage (~3.8V) */
        s->vadc_data = 0x3800 + (rand() % 150);
        break;
    case 3:  /* DIE_TEMP - Die temperature sensor */
        s->vadc_data = 0x0300 + (rand() % 50);  /* ~30C + variation */
        break;
    case 4:  /* XO_THERM - Crystal oscillator thermal */
        s->vadc_data = 0x0250 + (rand() % 30);
        break;
    default: /* Default to VREF_1P25 */
        s->vadc_data = 0x1250 + (rand() % 20);
        break;
    }

    qemu_log_mask(LOG_GUEST_ERROR, "PMK8350: ADC conversion complete, "
                  "channel=%d, data=0x%04x\n", s->vadc_channel, s->vadc_data);

    /* Generate ADC completion interrupt if enabled */
    if (s->vadc_interrupt_enabled && s->adc_irq) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "PMK8350: Generating ADC EOC interrupt\n");
        qemu_irq_pulse(s->adc_irq);
    }
}

static uint64_t pmk8350_read(void *opaque, hwaddr addr, unsigned size)
{
    PMK8350State *s = PMK8350(opaque);
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
        PMK8350Regulator *reg = pmk8350_find_regulator(s, offset);
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
                              "PMK8350: unimplemented regulator register "
                              "read at 0x%04x\n", offset);
                break;
            }
        } else if (offset >= PMK8350_GPIO_BASE &&
                   offset < PMK8350_GPIO_BASE + PMK8350_GPIO_COUNT) {
            /* GPIO registers - only 4 GPIOs */
            int gpio_num = offset - PMK8350_GPIO_BASE;
            value = s->gpio_states[gpio_num];
        } else if (offset >= PMK8350_PON_BASE &&
                   offset < PMK8350_PON_BASE + 0x100) {
            /* Power-on/off registers */
            switch (offset - PMK8350_PON_BASE) {
            case PON_PON_REASON1:
                value = s->pon_reason1;
                break;
            case PON_PON_REASON2:
                value = s->pon_reason2;
                break;
            case PON_POFF_REASON1:
                value = s->poff_reason1;
                break;
            case PON_POFF_REASON2:
                value = s->poff_reason2;
                break;
            default:
                qemu_log_mask(LOG_UNIMP,
                              "PMK8350: unimplemented PON register "
                              "read at 0x%04x\n", offset);
                break;
            }
        } else if (offset >= PMK8350_VADC_BASE &&
                   offset < PMK8350_VADC_BASE + 0x100) {
            /* VADC registers */
            switch (offset - PMK8350_VADC_BASE) {
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
                              "PMK8350: unimplemented VADC register "
                              "read at 0x%04x\n", offset);
                break;
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "PMK8350: invalid register read at 0x%04x\n", offset);
        }
        break;
    }

    qemu_log_mask(LOG_TRACE, "PMK8350: read 0x%02x from offset 0x%04x\n",
                  (uint32_t)value, offset);
    return value;
}

static void pmk8350_write(void *opaque, hwaddr addr, uint64_t value,
                         unsigned size)
{
    PMK8350State *s = PMK8350(opaque);
    uint16_t offset = addr;
    uint8_t val = (uint8_t)value;

    qemu_log_mask(LOG_TRACE, "PMK8350: write 0x%02x to offset 0x%04x\n",
                  val, offset);

    /* Most PMIC identification registers are read-only */
    if (offset >= PMIC_REV2 && offset <= PMIC_FAB_ID) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "PMK8350: write to read-only register 0x%04x\n", offset);
        return;
    }

    /* Check if this is a regulator register */
    PMK8350Regulator *reg = pmk8350_find_regulator(s, offset);
    if (reg) {
        uint8_t reg_offset = offset & 0xff;
        switch (reg_offset) {
        case SPMI_COMMON_REG_VOLTAGE_RANGE:
            reg->voltage_range = val;
            qemu_log_mask(LOG_TRACE,
                          "PMK8350: %s voltage range set to 0x%02x\n",
                          reg->name, val);
            break;
        case SPMI_COMMON_REG_VOLTAGE_SET:
            reg->voltage_set = val;
            qemu_log_mask(LOG_TRACE,
                          "PMK8350: %s voltage set to 0x%02x\n",
                          reg->name, val);
            break;
        case SPMI_COMMON_REG_MODE:
            reg->mode = val;
            qemu_log_mask(LOG_TRACE,
                          "PMK8350: %s mode set to 0x%02x\n",
                          reg->name, val);
            break;
        case SPMI_COMMON_REG_ENABLE:
            reg->enable = val;
            qemu_log_mask(LOG_TRACE,
                          "PMK8350: %s enable set to 0x%02x\n",
                          reg->name, val);
            break;
        default:
            qemu_log_mask(LOG_UNIMP,
                          "PMK8350: unimplemented regulator register "
                          "write at 0x%04x\n", offset);
            break;
        }
    } else if (offset >= PMK8350_GPIO_BASE &&
               offset < PMK8350_GPIO_BASE + PMK8350_GPIO_COUNT) {
        /* GPIO registers - implement cascading effects */
        int gpio_num = offset - PMK8350_GPIO_BASE;
        uint8_t old_val = s->gpio_states[gpio_num];
        s->gpio_states[gpio_num] = val;

        /* Log significant changes for master PMIC GPIOs */
        if ((old_val ^ val) & 0x80) {  /* Enable bit changed */
            qemu_log_mask(LOG_TRACE, "PMK8350: GPIO %d enable %s\n",
                          gpio_num, (val & 0x80) ? "enabled" : "disabled");
        }
    } else if (offset >= PMK8350_PON_BASE &&
               offset < PMK8350_PON_BASE + 0x100) {
        /* Power-on/off registers - mostly read-only but some control bits */
        qemu_log_mask(LOG_TRACE,
                      "PMK8350: PON register write at 0x%04x = 0x%02x\n",
                      offset, val);
    } else if (offset >= PMK8350_VADC_BASE &&
               offset < PMK8350_VADC_BASE + 0x100) {
        /* VADC registers - implement conversion triggering */
        switch (offset - PMK8350_VADC_BASE) {
        case VADC_MODE_CTL:
            s->vadc_mode_ctl = val;
            break;
        case VADC_EN_CTL1:
            s->vadc_en_ctl1 = val;
            break;
        case VADC_CHANNEL_SEL:
            s->vadc_channel = val;
            qemu_log_mask(LOG_TRACE, "PMK8350: ADC channel selected: %d\n",
                          val);
            break;
        case VADC_INT_EN:
            s->vadc_interrupt_enabled = (val & 0x01) ? true : false;
            qemu_log_mask(LOG_TRACE, "PMK8350: ADC interrupt %s\n",
                          s->vadc_interrupt_enabled ? "enabled" : "disabled");
            break;
        case VADC_CONV_REQ:
            s->vadc_conv_req = val;
            /* Trigger ADC conversion if request bit set */
            if (val & 0x80) {  /* VADC_CONV_REQ_SET */
                if (!s->vadc_conversion_active) {
                    s->vadc_conversion_active = true;
                    s->vadc_status1 &= ~0x01;  /* Clear EOC bit */
                    /* Start conversion with realistic timing (~6ms avg) */
                    timer_mod(s->adc_timer,
                              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                              (5500 + (rand() % 1000)) * 1000);  /* 5.5-6.5ms */
                }
            }
            break;
        default:
            qemu_log_mask(LOG_UNIMP,
                          "PMK8350: unimplemented VADC register "
                          "write at 0x%04x\n", offset);
            break;
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "PMK8350: invalid register write at 0x%04x\n", offset);
    }
}

static const MemoryRegionOps pmk8350_ops = {
    .read = pmk8350_read,
    .write = pmk8350_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void pmk8350_reset_enter(Object *obj, ResetType type)
{
    PMK8350State *s = PMK8350(obj);

    /* Initialize PMIC identification */
    s->pmic_rev2 = 0x00;
    s->pmic_rev3 = 0x01;
    s->pmic_rev4 = 0x03;
    s->pmic_type = PMIC_TYPE_VALUE;
    s->pmic_subtype = PMK8350_SUBTYPE_VALUE;
    s->pmic_fab_id = 0x00;

    /* Initialize regulators */
    s->num_regulators = ARRAY_SIZE(pmk8350_regulators);
    memcpy(s->regulators, pmk8350_regulators, sizeof(pmk8350_regulators));

    /* Initialize GPIO states - only 4 GPIOs */
    memset(s->gpio_states, 0, sizeof(s->gpio_states));

    /* Initialize power-on/off reasons (simulate normal power-on) */
    s->pon_reason1 = 0x20;  /* Power key pressed */
    s->pon_reason2 = 0x00;
    s->poff_reason1 = 0x00;
    s->poff_reason2 = 0x00;

    /* Initialize VADC */
    s->vadc_status1 = 0x00;
    s->vadc_mode_ctl = 0x00;
    s->vadc_en_ctl1 = 0x00;
    s->vadc_channel = 0;  /* Start with channel 0 (VREF_1P25) */
    s->vadc_conv_req = 0x00;
    s->vadc_data = 0x0000;
    s->vadc_conversion_active = false;
    s->vadc_interrupt_enabled = false;
}

static void pmk8350_realize(DeviceState *dev, Error **errp)
{
    PMK8350State *s = PMK8350(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &pmk8350_ops, s,
                          TYPE_PMK8350, PMK8350_REGISTER_SPACE_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    /* Initialize interrupt lines */
    sysbus_init_irq(sbd, &s->adc_irq);    /* ADC End-of-Conversion interrupt */
    sysbus_init_irq(sbd, &s->gpio_irq);   /* GPIO state change interrupt */
    sysbus_init_irq(sbd, &s->temp_irq);   /* Temperature alarm interrupt */
    sysbus_init_irq(sbd, &s->pon_irq);    /* Power-on interrupt */

    /* Initialize ADC conversion timer */
    s->adc_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                pmk8350_adc_conversion_complete, s);
}

static void pmk8350_unrealize(DeviceState *dev)
{
    PMK8350State *s = PMK8350(dev);

    if (s->adc_timer) {
        timer_del(s->adc_timer);
        timer_free(s->adc_timer);
    }
}

static void pmk8350_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = pmk8350_realize;
    dc->unrealize = pmk8350_unrealize;
    dc->desc = "Qualcomm PMK8350 Master PMIC (for QCM6490)";
    rc->phases.enter = pmk8350_reset_enter;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo pmk8350_info = {
    .name          = TYPE_PMK8350,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PMK8350State),
    .class_init    = pmk8350_class_init,
};

/* SPMI callback wrapper functions */
static uint64_t pmk8350_spmi_read(void *opaque, uint16_t addr, unsigned size)
{
    return pmk8350_read(opaque, addr, size);
}

static void pmk8350_spmi_write(void *opaque, uint16_t addr, uint64_t value,
                               unsigned size)
{
    pmk8350_write(opaque, addr, value, size);
}

/* Function to register with SPMI controller */
void pmk8350_register_with_spmi(PMK8350State *s,
                                SPMIControllerState *spmi_controller,
                                uint8_t slave_id)
{
    spmi_register_slave(spmi_controller, slave_id, DEVICE(s), "PMK8350",
                       pmk8350_spmi_read, pmk8350_spmi_write, s);
}

static void pmk8350_register_types(void)
{
    type_register_static(&pmk8350_info);
}

type_init(pmk8350_register_types)
