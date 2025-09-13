/*
 * Qualcomm PM8350C PMIC device model
 *
 * Copyright (c) 2025 Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This model implements the PM8350C PMIC, which handles camera and display
 * power management on QCS6490 SoCs.
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/misc/spmi-controller.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "trace.h"
#include "hw/misc/pm8350c.h"

/* PMIC registers */
#define PM8350C_REV2                 0x101
#define PM8350C_REV3                 0x102
#define PM8350C_REV4                 0x103
#define PM8350C_TYPE                 0x104
#define PM8350C_SUBTYPE              0x105
#define PM8350C_FAB_ID               0x1f2

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

/* Flash LED registers */
#define FLASH_LED_CTRL               0x40
#define FLASH_LED_CURRENT            0x41

/* LPG PWM registers */
#define LPG_CTRL                     0x40
#define LPG_DUTY_CYCLE_LSB           0x42
#define LPG_DUTY_CYCLE_MSB           0x43

static void pm8350c_adc_conversion_complete(void *opaque)
{
    PM8350CState *s = PM8350C(opaque);

    /* Mark conversion complete - this is the key side effect */
    s->vadc_status1 |= 0x01;  /* Set EOC (End of Conversion) bit */
    s->vadc_conversion_active = false;

    /* Generate realistic ADC data based on actual power rails */
    switch (s->vadc_channel) {
    case 0:  /* Camera analog rail (L3 - 2.8V) */
        s->vadc_data = 0x2800 + (rand() % 50);
        break;
    case 1:  /* Camera digital rail (L1 - 1.8V) */
        s->vadc_data = 0x1800 + (rand() % 30);
        break;
    case 2:  /* Display analog rail (L6 - 3.0V) */
        s->vadc_data = 0x3000 + (rand() % 40);
        break;
    default: /* Default to VDD measurement */
        s->vadc_data = 0x3300 + (rand() % 100);
        break;
    }

    qemu_log_mask(LOG_GUEST_ERROR, "PM8350C: ADC conversion complete, "
                  "channel=%d, data=0x%04x\n", s->vadc_channel, s->vadc_data);

    /* Generate ADC completion interrupt */
    if (s->vadc_interrupt_enabled) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "PM8350C: Generating ADC EOC interrupt\n");
        qemu_irq_pulse(s->adc_irq);
    }
}

static uint64_t pm8350c_read(void *opaque, hwaddr offset, unsigned size)
{
    PM8350CState *s = PM8350C(opaque);
    uint32_t regulator_base;
    int regulator_idx;
    uint64_t value = 0;

    /* PMIC identification registers */
    switch (offset) {
    case PM8350C_REV2:
        value = s->pmic_rev[0];
        break;
    case PM8350C_REV3:
        value = s->pmic_rev[1];
        break;
    case PM8350C_REV4:
        value = s->pmic_rev[2];
        break;
    case PM8350C_TYPE:
        value = s->pmic_type;
        break;
    case PM8350C_SUBTYPE:
        value = s->pmic_subtype;
        break;
    case PM8350C_FAB_ID:
        value = s->fab_id;
        break;
    }

    /* GPIO registers - simple addressing for test compatibility */
    if (offset >= PM8350C_GPIO_BASE &&
        offset < PM8350C_GPIO_BASE + PM8350C_GPIO_COUNT) {
        int gpio_num = offset - PM8350C_GPIO_BASE;
        value = s->gpio_states[gpio_num];
        goto done;
    }

    /* Temperature alarm */
    if (offset == PM8350C_TEMP_ALARM_BASE + TEMP_ALARM_STATUS) {
        value = s->temp_alarm_status;
        goto done;
    }
    if (offset == PM8350C_TEMP_ALARM_BASE + TEMP_ALARM_CONFIG) {
        value = s->temp_alarm_config;
        goto done;
    }

    /* VADC registers */
    if (offset == PM8350C_VADC_BASE + VADC_STATUS1) {
        value = s->vadc_status1;
        goto done;
    }
    if (offset == PM8350C_VADC_BASE + VADC_MODE_CTL) {
        value = s->vadc_mode_ctl;
        goto done;
    }
    if (offset == PM8350C_VADC_BASE + VADC_EN_CTL1) {
        value = s->vadc_en_ctl1;
        goto done;
    }
    if (offset == PM8350C_VADC_BASE + VADC_CHANNEL_SEL) {
        value = s->vadc_channel;
        goto done;
    }
    if (offset == PM8350C_VADC_BASE + VADC_INT_EN) {
        value = s->vadc_interrupt_enabled ? 0x01 : 0x00;
        goto done;
    }
    if (offset == PM8350C_VADC_BASE + VADC_DATA) {
        /* STATUS1 automatically cleared on DATA read (key side effect) */
        s->vadc_status1 &= ~0x01;
        value = s->vadc_data & 0xFF;
        goto done;
    }
    if (offset == PM8350C_VADC_BASE + VADC_DATA + 1) {
        value = (s->vadc_data >> 8) & 0xFF;
        goto done;
    }

    /* Flash LED registers */
    if (offset == PM8350C_FLASH_BASE + FLASH_LED_CTRL) {
        value = s->flash_led_ctrl;
        goto done;
    }
    if (offset == PM8350C_FLASH_BASE + FLASH_LED_CURRENT) {
        value = s->flash_led_current;
        goto done;
    }

    /* LPG PWM registers */
    if (offset == PM8350C_LPG_BASE + LPG_CTRL) {
        value = s->lpg_ctrl;
        goto done;
    }
    if (offset == PM8350C_LPG_BASE + LPG_DUTY_CYCLE_LSB) {
        value = s->lpg_duty_cycle & 0xFF;
        goto done;
    }
    if (offset == PM8350C_LPG_BASE + LPG_DUTY_CYCLE_MSB) {
        value = (s->lpg_duty_cycle >> 8) & 0xFF;
        goto done;
    }

    /* Regulator registers */
    regulator_base = 0;
    regulator_idx = -1;

    if (offset >= PM8350C_L1_BASE && offset < PM8350C_L1_BASE + 0x100) {
        regulator_base = PM8350C_L1_BASE;
        regulator_idx = 0;
    } else if (offset >= PM8350C_L2_BASE && offset < PM8350C_L2_BASE + 0x100) {
        regulator_base = PM8350C_L2_BASE;
        regulator_idx = 1;
    } else if (offset >= PM8350C_L3_BASE && offset < PM8350C_L3_BASE + 0x100) {
        regulator_base = PM8350C_L3_BASE;
        regulator_idx = 2;
    } else if (offset >= PM8350C_L4_BASE && offset < PM8350C_L4_BASE + 0x100) {
        regulator_base = PM8350C_L4_BASE;
        regulator_idx = 3;
    } else if (offset >= PM8350C_L5_BASE && offset < PM8350C_L5_BASE + 0x100) {
        regulator_base = PM8350C_L5_BASE;
        regulator_idx = 4;
    } else if (offset >= PM8350C_L6_BASE && offset < PM8350C_L6_BASE + 0x100) {
        regulator_base = PM8350C_L6_BASE;
        regulator_idx = 5;
    }

    if (regulator_idx >= 0) {
        PM8350CRegulator *reg = &s->regulators[regulator_idx];
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
    qemu_log_mask(LOG_GUEST_ERROR, "PM8350C: read 0x%02lx from offset "
                  "0x%04lx\n", value, offset);
    return value;
}

static void pm8350c_write(void *opaque, hwaddr offset, uint64_t value,
                          unsigned size)
{
    PM8350CState *s = PM8350C(opaque);
    uint32_t regulator_base;
    int regulator_idx;

    qemu_log_mask(LOG_GUEST_ERROR, "PM8350C: write 0x%02lx to offset "
                  "0x%04lx\n", value, offset);

    /* Read-only registers */
    if (offset >= PM8350C_REV2 && offset <= PM8350C_FAB_ID) {
        return;  /* Ignore writes to read-only identification registers */
    }

    /* GPIO registers - simple addressing for test compatibility */
    if (offset >= PM8350C_GPIO_BASE &&
        offset < PM8350C_GPIO_BASE + PM8350C_GPIO_COUNT) {
        int gpio_num = offset - PM8350C_GPIO_BASE;
        s->gpio_states[gpio_num] = value & 0xFF;

        /* Check for GPIO enable bit to trigger side effects */
        if (value & 0x80) {
            qemu_log_mask(LOG_GUEST_ERROR, "PM8350C: GPIO %d enable set, "
                          "triggering state change\n", gpio_num);
            /* Generate GPIO change interrupt */
            qemu_irq_pulse(s->gpio_irq);
        }
        return;
    }

    /* Temperature alarm */
    if (offset == PM8350C_TEMP_ALARM_BASE + TEMP_ALARM_CONFIG) {
        s->temp_alarm_config = value & 0xFF;
        qemu_log_mask(LOG_GUEST_ERROR, "PM8350C: temp alarm config set to "
                      "0x%02lx\n", value);
        return;
    }

    /* VADC control */
    if (offset == PM8350C_VADC_BASE + VADC_MODE_CTL) {
        s->vadc_mode_ctl = value & 0xFF;
        qemu_log_mask(LOG_GUEST_ERROR, "PM8350C: VADC mode set to "
                      "0x%02lx\n", value);
        return;
    }
    if (offset == PM8350C_VADC_BASE + VADC_EN_CTL1) {
        s->vadc_en_ctl1 = value & 0xFF;
        qemu_log_mask(LOG_GUEST_ERROR, "PM8350C: VADC enable set to "
                      "0x%02lx\n", value);
        return;
    }
    if (offset == PM8350C_VADC_BASE + VADC_CHANNEL_SEL) {
        s->vadc_channel = value & 0x0F;  /* 4-bit channel selection */
        qemu_log_mask(LOG_GUEST_ERROR, "PM8350C: VADC channel set to %d\n",
                      s->vadc_channel);
        return;
    }
    if (offset == PM8350C_VADC_BASE + VADC_INT_EN) {
        s->vadc_interrupt_enabled = (value & 0x01) != 0;
        qemu_log_mask(LOG_GUEST_ERROR, "PM8350C: VADC interrupt %s\n",
                      s->vadc_interrupt_enabled ? "enabled" : "disabled");
        return;
    }
    if (offset == PM8350C_VADC_BASE + VADC_CONV_REQ) {
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

    /* Flash LED control */
    if (offset == PM8350C_FLASH_BASE + FLASH_LED_CTRL) {
        s->flash_led_ctrl = value & 0xFF;
        qemu_log_mask(LOG_GUEST_ERROR, "PM8350C: flash LED ctrl set to "
                      "0x%02lx\n", value);
        return;
    }
    if (offset == PM8350C_FLASH_BASE + FLASH_LED_CURRENT) {
        s->flash_led_current = value & 0xFF;
        qemu_log_mask(LOG_GUEST_ERROR, "PM8350C: flash LED current set to "
                      "0x%02lx\n", value);
        return;
    }

    /* LPG PWM control */
    if (offset == PM8350C_LPG_BASE + LPG_CTRL) {
        s->lpg_ctrl = value & 0xFF;
        qemu_log_mask(LOG_GUEST_ERROR, "PM8350C: LPG ctrl set to "
                      "0x%02lx\n", value);
        return;
    }
    if (offset == PM8350C_LPG_BASE + LPG_DUTY_CYCLE_LSB) {
        s->lpg_duty_cycle = (s->lpg_duty_cycle & 0xFF00) | (value & 0xFF);
        qemu_log_mask(LOG_GUEST_ERROR, "PM8350C: LPG duty cycle LSB set\n");
        return;
    }
    if (offset == PM8350C_LPG_BASE + LPG_DUTY_CYCLE_MSB) {
        s->lpg_duty_cycle = (s->lpg_duty_cycle & 0x00FF) |
                            ((value & 0xFF) << 8);
        qemu_log_mask(LOG_GUEST_ERROR, "PM8350C: LPG duty cycle MSB set\n");
        return;
    }

    /* Regulator registers */
    regulator_base = 0;
    regulator_idx = -1;

    if (offset >= PM8350C_L1_BASE && offset < PM8350C_L1_BASE + 0x100) {
        regulator_base = PM8350C_L1_BASE;
        regulator_idx = 0;
    } else if (offset >= PM8350C_L2_BASE && offset < PM8350C_L2_BASE + 0x100) {
        regulator_base = PM8350C_L2_BASE;
        regulator_idx = 1;
    } else if (offset >= PM8350C_L3_BASE && offset < PM8350C_L3_BASE + 0x100) {
        regulator_base = PM8350C_L3_BASE;
        regulator_idx = 2;
    } else if (offset >= PM8350C_L4_BASE && offset < PM8350C_L4_BASE + 0x100) {
        regulator_base = PM8350C_L4_BASE;
        regulator_idx = 3;
    } else if (offset >= PM8350C_L5_BASE && offset < PM8350C_L5_BASE + 0x100) {
        regulator_base = PM8350C_L5_BASE;
        regulator_idx = 4;
    } else if (offset >= PM8350C_L6_BASE && offset < PM8350C_L6_BASE + 0x100) {
        regulator_base = PM8350C_L6_BASE;
        regulator_idx = 5;
    }

    if (regulator_idx >= 0) {
        PM8350CRegulator *reg = &s->regulators[regulator_idx];
        uint32_t reg_offset = offset - regulator_base;

        switch (reg_offset) {
        case SPMI_COMMON_REG_VOLTAGE_RANGE:
            reg->voltage_range = value & 0xFF;
            qemu_log_mask(LOG_GUEST_ERROR,
                          "PM8350C: L%d voltage range set to 0x%02lx\n",
                          regulator_idx + 1, value);
            break;
        case SPMI_COMMON_REG_VOLTAGE_SET:
            /* Voltage can only be set if range is configured */
            if (reg->voltage_range == 0) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "PM8350C: L%d voltage set without range configured\n",
                              regulator_idx + 1);
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
            qemu_log_mask(LOG_GUEST_ERROR,
                          "PM8350C: L%d voltage set to %dmV (reg=0x%02lx)\n",
                          regulator_idx + 1, reg->voltage_mv, value);
            break;
        case SPMI_COMMON_REG_MODE:
            reg->mode = value & 0xFF;
            /* Mode changes affect power consumption */
            if ((value & 0x07) == 0x05) {  /* LPM mode */
                qemu_log_mask(LOG_GUEST_ERROR,
                              "PM8350C: L%d switching to low power mode\n",
                              regulator_idx + 1);
            } else if ((value & 0x07) == 0x07) {  /* Auto/HPM mode */
                qemu_log_mask(LOG_GUEST_ERROR,
                              "PM8350C: L%d switching to high power mode\n",
                              regulator_idx + 1);
            }
            break;
        case SPMI_COMMON_REG_ENABLE:
            reg->enable = value & 0xFF;
            reg->enabled = (value & 0x80) != 0;
            if (reg->enabled) {
                /* Verify voltage range and setting are configured */
                if (reg->voltage_range == 0 || reg->voltage_set == 0) {
                    qemu_log_mask(LOG_GUEST_ERROR,
                                  "PM8350C: L%d enabled without proper voltage config\n",
                                  regulator_idx + 1);
                }
            }
            qemu_log_mask(LOG_GUEST_ERROR, "PM8350C: L%d regulator %s\n",
                          regulator_idx + 1,
                          reg->enabled ? "enabled" : "disabled");
            break;
        }
    }
}

static const MemoryRegionOps pm8350c_ops = {
    .read = pm8350c_read,
    .write = pm8350c_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void pm8350c_reset_enter(Object *obj, ResetType type)
{
    PM8350CState *s = PM8350C(obj);

    /* Initialize PMIC identification */
    s->pmic_type = 0x51;  /* Standard PMIC type */
    s->pmic_subtype = PM8350C_SUBTYPE_VALUE;
    s->pmic_rev[0] = 0x00;  /* Rev2 */
    s->pmic_rev[1] = 0x01;  /* Rev3 */
    s->pmic_rev[2] = 0x03;  /* Rev4 */
    s->fab_id = 0x00;

    /* Initialize camera/display regulators with typical configurations */
    /* L1: 1.8V camera digital */
    s->regulators[0] = (PM8350CRegulator){
        .type = 0x03, .subtype = 0x0a, .voltage_range = 0x02,
        .voltage_set = 0x38, .mode = 0x07, .enable = 0x80,
        .enabled = true, .voltage_mv = 1800
    };

    /* L2: 1.2V camera core */
    s->regulators[1] = (PM8350CRegulator){
        .type = 0x03, .subtype = 0x0a, .voltage_range = 0x01,
        .voltage_set = 0x18, .mode = 0x07, .enable = 0x80,
        .enabled = true, .voltage_mv = 1200
    };

    /* L3: 2.8V camera analog */
    s->regulators[2] = (PM8350CRegulator){
        .type = 0x03, .subtype = 0x0a, .voltage_range = 0x03,
        .voltage_set = 0x38, .mode = 0x07, .enable = 0x80,
        .enabled = true, .voltage_mv = 2800
    };

    /* L4: 1.8V camera I/O */
    s->regulators[3] = (PM8350CRegulator){
        .type = 0x03, .subtype = 0x0a, .voltage_range = 0x02,
        .voltage_set = 0x38, .mode = 0x07, .enable = 0x80,
        .enabled = true, .voltage_mv = 1800
    };

    /* L5: 1.8V display I/O */
    s->regulators[4] = (PM8350CRegulator){
        .type = 0x03, .subtype = 0x0a, .voltage_range = 0x02,
        .voltage_set = 0x38, .mode = 0x07, .enable = 0x80,
        .enabled = true, .voltage_mv = 1800
    };

    /* L6: 3.0V display analog */
    s->regulators[5] = (PM8350CRegulator){
        .type = 0x03, .subtype = 0x0a, .voltage_range = 0x03,
        .voltage_set = 0x3C, .mode = 0x07, .enable = 0x80,
        .enabled = true, .voltage_mv = 3000
    };

    /* Initialize GPIO states */
    memset(s->gpio_states, 0, sizeof(s->gpio_states));

    /* Initialize other states */
    s->temp_alarm_status = 0x00;
    s->temp_alarm_config = 0x00;
    s->vadc_status1 = 0x00;
    s->vadc_mode_ctl = 0x00;
    s->vadc_en_ctl1 = 0x00;
    s->vadc_channel = 0x00;
    s->vadc_data = 0x0000;
    s->vadc_conversion_active = false;
    s->vadc_interrupt_enabled = false;
    s->flash_led_ctrl = 0x00;
    s->flash_led_current = 0x00;
    s->lpg_ctrl = 0x00;
    s->lpg_duty_cycle = 0x0000;
}

static void pm8350c_realize(DeviceState *dev, Error **errp)
{
    PM8350CState *s = PM8350C(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &pm8350c_ops, s,
                          TYPE_PM8350C, 0x10000);
    sysbus_init_mmio(sbd, &s->iomem);

    /* Initialize timer for ADC conversions */
    s->adc_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                pm8350c_adc_conversion_complete, s);

    /* Initialize IRQ outputs */
    sysbus_init_irq(sbd, &s->gpio_irq);
    sysbus_init_irq(sbd, &s->temp_irq);
    sysbus_init_irq(sbd, &s->flash_irq);
    sysbus_init_irq(sbd, &s->adc_irq);
}

static void pm8350c_unrealize(DeviceState *dev)
{
    PM8350CState *s = PM8350C(dev);

    if (s->adc_timer) {
        timer_free(s->adc_timer);
    }
}

static void pm8350c_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = pm8350c_realize;
    dc->unrealize = pm8350c_unrealize;
    rc->phases.enter = pm8350c_reset_enter;
}

static const TypeInfo pm8350c_info = {
    .name = TYPE_PM8350C,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PM8350CState),
    .class_init = pm8350c_class_init,
};

/* SPMI callback wrapper functions */
static uint64_t pm8350c_spmi_read(void *opaque, uint16_t addr, unsigned size)
{
    return pm8350c_read(opaque, addr, size);
}

static void pm8350c_spmi_write(void *opaque, uint16_t addr, uint64_t value,
                               unsigned size)
{
    pm8350c_write(opaque, addr, value, size);
}

/* Function to register with SPMI controller */
void pm8350c_register_with_spmi(PM8350CState *s,
                                SPMIControllerState *spmi_controller,
                                uint8_t slave_id)
{
    spmi_register_slave(spmi_controller, slave_id, DEVICE(s), "PM8350C",
                       pm8350c_spmi_read, pm8350c_spmi_write, s);
}

static void pm8350c_register_types(void)
{
    type_register_static(&pm8350c_info);
}

type_init(pm8350c_register_types)
