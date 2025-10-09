/*
 * Qualcomm SPMI (System Power Management Interface) Controller
 *
 * Copyright (c) 2025 Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This implements the SPMI controller found on Qualcomm SoCs like QCS6490.
 * SPMI is the interface protocol used to communicate with PMICs.
 *
 * Based on real hardware analysis and Linux kernel drivers:
 * - drivers/spmi/spmi-pmic-arb.c
 * - Documentation/devicetree/bindings/spmi/qcom,spmi-pmic-arb.yaml
 */

#include "qemu/osdep.h"
#include "hw/misc/spmi-controller.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"

/* SPMI Slave Registration */
void spmi_register_slave(SPMIControllerState *controller, uint8_t slave_id,
                        DeviceState *device, const char *name,
                        uint64_t (*read_cb)(void *opaque, uint16_t addr,
                                            unsigned size),
                        void (*write_cb)(void *opaque, uint16_t addr,
                                         uint64_t value, unsigned size),
                        void *opaque)
{
    if (slave_id >= SPMI_MAX_SLAVES) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "SPMI: Invalid slave_id %d (max %d)\n",
                      slave_id, SPMI_MAX_SLAVES - 1);
        return;
    }

    if (controller->slaves[slave_id].present) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "SPMI: Slave %d already registered\n", slave_id);
        return;
    }

    controller->slaves[slave_id].slave_id = slave_id;
    controller->slaves[slave_id].device = device;
    controller->slaves[slave_id].present = true;
    controller->slaves[slave_id].read = read_cb;
    controller->slaves[slave_id].write = write_cb;
    controller->slaves[slave_id].opaque = opaque;
    g_strlcpy(controller->slaves[slave_id].name, name,
              sizeof(controller->slaves[slave_id].name));

}

void spmi_register_slave_simple(SPMIControllerState *controller,
                               uint8_t slave_id,
                               DeviceState *device, const char *name)
{
    spmi_register_slave(controller, slave_id, device, name, NULL, NULL, NULL);
}

void spmi_unregister_slave(SPMIControllerState *controller, uint8_t slave_id)
{
    if (slave_id >= SPMI_MAX_SLAVES || !controller->slaves[slave_id].present) {
        return;
    }

    controller->slaves[slave_id].present = false;
    controller->slaves[slave_id].device = NULL;
    controller->slaves[slave_id].read = NULL;
    controller->slaves[slave_id].write = NULL;
    controller->slaves[slave_id].opaque = NULL;
    controller->slaves[slave_id].name[0] = '\0';

}

/* SPMI Transaction Functions */
uint64_t spmi_read_slave(SPMIControllerState *controller, uint8_t slave_id,
                        uint16_t addr, unsigned size)
{
    if (slave_id >= SPMI_MAX_SLAVES || !controller->slaves[slave_id].present) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "SPMI: Read from non-existent slave %d\n", slave_id);
        return 0;
    }

    SPMISlave *slave = &controller->slaves[slave_id];


    /* If we have a registered callback, use it */
    if (slave->read && slave->opaque) {
        uint64_t result = slave->read(slave->opaque, addr, size);
        return result;
    }


    /* Fallback to stub data */
    switch (addr) {
    case 0x104: /* PMIC_TYPE */
        return 0x51;
    case 0x105: /* PMIC_SUBTYPE */
        return 0x00;
    default:
        return 0x00;
    }
}

void spmi_write_slave(SPMIControllerState *controller, uint8_t slave_id,
                     uint16_t addr, uint64_t value, unsigned size)
{
    if (slave_id >= SPMI_MAX_SLAVES || !controller->slaves[slave_id].present) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "SPMI: Write to non-existent slave %d\n", slave_id);
        return;
    }

    SPMISlave *slave = &controller->slaves[slave_id];


    /* If we have a registered callback, use it */
    if (slave->write && slave->opaque) {
        slave->write(slave->opaque, addr, value, size);
        return;
    }
}

static uint64_t spmi_controller_read(void *opaque, hwaddr addr, unsigned size)
{
    SPMIControllerState *s = SPMI_CONTROLLER(opaque);
    uint32_t offset = addr;
    uint64_t value = 0;


    if (offset >= SPMI_ARB_REG_CHN_OFFSET) {
        /* Register channel access - extract slave_id and register offset */
        uint32_t reg_offset = offset - SPMI_ARB_REG_CHN_OFFSET;
        uint8_t slave_id = reg_offset / 0x1000;      /* Each slave gets 4KB */
        uint16_t reg_addr = reg_offset % 0x1000;     /* Register within slave */

        value = spmi_read_slave(s, slave_id, reg_addr, size);
    } else if (offset >= SPMI_ARB_APID_MAP_OFFSET &&
               offset < SPMI_ARB_APID_MAP_OFFSET + 0x1000) {
        /* APID mapping registers - return identity mapping for now */
        value = (offset - SPMI_ARB_APID_MAP_OFFSET) / 4;
    } else if ((offset & 0xFFF) == SPMI_CHANNEL_STATUS) {
        /* Channel status - mark as done for now */
        value = SPMI_STATUS_DONE;
    } else if ((offset & 0xFFF) == SPMI_CHANNEL_CONFIG) {
        /* Channel config */
        value = 0x00;
    } else if (offset == SPMI_OBS_0_STATUS) {
        /* Observation channel status */
        value = SPMI_STATUS_DONE;
    } else {
    }

    return value;
}

static void spmi_controller_write(void *opaque, hwaddr addr, uint64_t value,
                                 unsigned size)
{
    SPMIControllerState *s = SPMI_CONTROLLER(opaque);
    uint32_t offset = addr;
    uint8_t val = (uint8_t)value;


    if (offset >= SPMI_ARB_REG_CHN_OFFSET) {
        /* Register channel access - extract slave_id and register offset */
        uint32_t reg_offset = offset - SPMI_ARB_REG_CHN_OFFSET;
        uint8_t slave_id = reg_offset / 0x1000;      /* Each slave gets 4KB */
        uint16_t reg_addr = reg_offset % 0x1000;     /* Register within slave */

        spmi_write_slave(s, slave_id, reg_addr, val, size);
    } else if ((offset & 0xFFF) == SPMI_CHANNEL_CMD) {
        /* Channel command register */
        uint8_t channel = offset / 0x1000;
        if (channel < 32) {
            s->channels[channel].cmd = val;
            /* Process command - for now just mark as done */
            s->channels[channel].status |= SPMI_STATUS_DONE;
        }
    } else if ((offset & 0xFFF) == SPMI_CHANNEL_CONFIG) {
        /* Channel config register */
        uint8_t channel = offset / 0x1000;
        if (channel < 32) {
            s->channels[channel].config = val;
        }
    } else if ((offset & 0xFFF) == SPMI_CHANNEL_IRQ_CLEAR) {
        /* Clear channel interrupts */
        uint8_t channel = offset / 0x1000;
        if (channel < 32) {
            s->channels[channel].status &= ~val;
        }
    } else if (offset == SPMI_OBS_0_CMD) {
        /* Observation channel command */
        s->obs_channels[0].cmd = val;
        s->obs_channels[0].status |= SPMI_STATUS_DONE;
    } else {
    }
}

static const MemoryRegionOps spmi_controller_ops = {
    .read = spmi_controller_read,
    .write = spmi_controller_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void spmi_controller_reset_enter(Object *obj, ResetType type)
{
    SPMIControllerState *s = SPMI_CONTROLLER(obj);

    /* Initialize SPMI controller state */
    s->enabled = true;
    s->version = 0x20010000;  /* SPMI v2.0 */
    s->num_slaves = 0;

    /* Initialize slaves only if not already set up - preserve existing */
    for (int i = 0; i < SPMI_MAX_SLAVES; i++) {
        if (!s->slaves[i].present) {
            s->slaves[i].present = false;
            s->slaves[i].device = NULL;
            s->slaves[i].read = NULL;
            s->slaves[i].write = NULL;
            s->slaves[i].opaque = NULL;
            s->slaves[i].name[0] = '\0';
        }
    }

    /* Initialize channels */
    for (int i = 0; i < 32; i++) {
        s->channels[i].cmd = 0;
        s->channels[i].status = 0;
        s->channels[i].config = 0;
        s->channels[i].active = false;
        s->channels[i].data_len = 0;
        memset(s->channels[i].data, 0, sizeof(s->channels[i].data));
    }

    /* Initialize observation channels */
    for (int i = 0; i < 8; i++) {
        s->obs_channels[i].cmd = 0;
        s->obs_channels[i].status = 0;
        s->obs_channels[i].config = 0;
        s->obs_channels[i].active = false;
    }

    /* Initialize APID mapping - identity mapping for now */
    for (int i = 0; i < SPMI_MAX_PPID; i++) {
        s->apid_map[i] = i;
        s->ppid_to_apid[i] = i;
    }
}

static void spmi_controller_realize(DeviceState *dev, Error **errp)
{
    SPMIControllerState *s = SPMI_CONTROLLER(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &spmi_controller_ops, s,
                          TYPE_SPMI_CONTROLLER, SPMI_CONTROLLER_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    /* Initialize interrupt line */
    sysbus_init_irq(sbd, &s->irq);
}

static void spmi_controller_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = spmi_controller_realize;
    dc->desc = "Qualcomm SPMI Controller";
    rc->phases.enter = spmi_controller_reset_enter;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo spmi_controller_info = {
    .name          = TYPE_SPMI_CONTROLLER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SPMIControllerState),
    .class_init    = spmi_controller_class_init,
};

static void spmi_controller_register_types(void)
{
    type_register_static(&spmi_controller_info);
}

type_init(spmi_controller_register_types)
