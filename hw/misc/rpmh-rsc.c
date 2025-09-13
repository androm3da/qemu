/*
 * Qualcomm RPMH-RSC
 *
 * Copyright (c) 2024 Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/misc/rpmh-rsc.h"
#include "hw/qdev-properties.h"
#include "hw/resettable.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"

enum rpmh_rsc_regs {
    RSC_DRV_TCS_OFFSET,
    RSC_DRV_CMD_OFFSET,
    DRV_SOLVER_CONFIG,
    DRV_PRNT_CHLD_CONFIG,
    RSC_DRV_IRQ_ENABLE,
    RSC_DRV_IRQ_STATUS,
    RSC_DRV_IRQ_CLEAR,
    RSC_DRV_CMD_WAIT_FOR_CMPL,
    RSC_DRV_CONTROL,
    RSC_DRV_STATUS,
    RSC_DRV_CMD_ENABLE,
    RSC_DRV_CMD_MSGID,
    RSC_DRV_CMD_ADDR,
    RSC_DRV_CMD_DATA,
    RSC_DRV_CMD_STATUS,
    RSC_DRV_CMD_RESP_DATA,
    RPMH_RSC_MAX_REGS
};

/* Version info */
#define MAJOR_VER_MASK     0xFF
#define MAJOR_VER_SHIFT    16
#define MINOR_VER_MASK     0xFF
#define MINOR_VER_SHIFT    8

/* DRV configuration */
#define DRV_HW_SOLVER_MASK   1
#define DRV_HW_SOLVER_SHIFT  24
#define DRV_NUM_TCS_MASK     0x3F
#define DRV_NUM_TCS_SHIFT    6
#define DRV_NCPT_MASK        0x1F
#define DRV_NCPT_SHIFT       27

/* TCS control bits */
#define TCS_AMC_MODE_ENABLE   BIT(16)
#define TCS_AMC_MODE_TRIGGER  BIT(24)

/* CMD register bits */
#define CMD_MSGID_LEN         8
#define CMD_MSGID_RESP_REQ    BIT(8)
#define CMD_MSGID_WRITE       BIT(16)
#define CMD_STATUS_ISSUED     BIT(8)
#define CMD_STATUS_COMPL      BIT(16)

/* Register offset arrays for different versions */
static const uint32_t rpmh_rsc_reg_offset_ver_2_7[RPMH_RSC_MAX_REGS] = {
    [RSC_DRV_TCS_OFFSET]        = 672,
    [RSC_DRV_CMD_OFFSET]        = 20,
    [DRV_SOLVER_CONFIG]         = 0x04,
    [DRV_PRNT_CHLD_CONFIG]      = 0x0C,
    [RSC_DRV_IRQ_ENABLE]        = 0x00,
    [RSC_DRV_IRQ_STATUS]        = 0x04,
    [RSC_DRV_IRQ_CLEAR]         = 0x08,
    [RSC_DRV_CMD_WAIT_FOR_CMPL] = 0x10,
    [RSC_DRV_CONTROL]           = 0x14,
    [RSC_DRV_STATUS]            = 0x18,
    [RSC_DRV_CMD_ENABLE]        = 0x1C,
    [RSC_DRV_CMD_MSGID]         = 0x30,
    [RSC_DRV_CMD_ADDR]          = 0x34,
    [RSC_DRV_CMD_DATA]          = 0x38,
    [RSC_DRV_CMD_STATUS]        = 0x3C,
    [RSC_DRV_CMD_RESP_DATA]     = 0x40,
};




/* DRV register space read handler */
static uint64_t rpmh_rsc_drv_read(void *opaque, hwaddr addr, unsigned size)
{
    RpmhRscState *s = RPMH_RSC(opaque);
    uint32_t offset = addr;
    uint64_t value = 0;

    if (offset == s->regs[DRV_SOLVER_CONFIG]) {
        value = s->drv_solver_config;
    } else if (offset == s->regs[DRV_PRNT_CHLD_CONFIG]) {
        value = s->drv_prnt_chld_config;
    } else if (offset == s->regs[RSC_DRV_IRQ_ENABLE]) {
        value = s->irq_enable;
    } else if (offset == s->regs[RSC_DRV_IRQ_CLEAR]) {
        /* Write-only register */
        value = 0;
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "RPMH-RSC DRV: invalid register offset 0x%x\n", offset);
    }

    qemu_log_mask(LOG_TRACE, "RPMH-RSC DRV: read 0x%08x from offset 0x%08x\n",
                  (uint32_t)value, offset);
    return value;
}

/* TCS register space read handler */
static uint64_t rpmh_rsc_tcs_read(void *opaque, hwaddr addr, unsigned size)
{
    RpmhRscState *s = RPMH_RSC(opaque);
    uint32_t offset = addr;
    uint64_t value = 0;

    /* Handle TCS-level registers first */
    if (offset == s->regs[RSC_DRV_IRQ_STATUS]) {
        value = s->irq_status;
        qemu_log_mask(LOG_TRACE, "RPMH-RSC TCS: read IRQ status 0x%08x\n",
                      (uint32_t)value);
        return value;
    }

    uint32_t tcs_id = offset / s->regs[RSC_DRV_TCS_OFFSET];
    if (tcs_id >= RPMH_RSC_MAX_TCS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "RPMH-RSC TCS: invalid TCS id %u\n", tcs_id);
        return 0;
    }

    uint32_t tcs_reg_offset = offset - (tcs_id * s->regs[RSC_DRV_TCS_OFFSET]);
    RpmhRscTcsState *tcs = &s->tcs_states[tcs_id];

    if (tcs_reg_offset == s->regs[RSC_DRV_CONTROL]) {
        value = tcs->control;
    } else if (tcs_reg_offset == s->regs[RSC_DRV_STATUS]) {
        value = tcs->status;
    } else if (tcs_reg_offset == s->regs[RSC_DRV_CMD_ENABLE]) {
        value = tcs->cmd_enable;
    } else if (tcs_reg_offset >= s->regs[RSC_DRV_CMD_MSGID]) {
        /* Command register access */
        uint32_t cmd_offset = tcs_reg_offset - s->regs[RSC_DRV_CMD_MSGID];
        uint32_t cmd_id = cmd_offset / s->regs[RSC_DRV_CMD_OFFSET];

        if (cmd_id >= RPMH_RSC_MAX_CMDS_PER_TCS) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "RPMH-RSC TCS: invalid command id %u\n", cmd_id);
            return 0;
        }

        uint32_t cmd_reg_offset = cmd_offset -
            (cmd_id * s->regs[RSC_DRV_CMD_OFFSET]);
        RpmhRscCommand *cmd = &tcs->commands[cmd_id];

        if (cmd_reg_offset == 0) { /* RSC_DRV_CMD_MSGID */
            value = (cmd->wait ? CMD_MSGID_RESP_REQ : 0) |
                    (cmd->data ? CMD_MSGID_WRITE : 0);
        } else if (cmd_reg_offset == 4) { /* RSC_DRV_CMD_ADDR */
            value = cmd->addr;
        } else if (cmd_reg_offset == 8) { /* RSC_DRV_CMD_DATA */
            value = cmd->data;
        } else if (cmd_reg_offset == 12) { /* RSC_DRV_CMD_STATUS */
            value = cmd->status;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "RPMH-RSC TCS: invalid command register offset 0x%x\n",
                          cmd_reg_offset);
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "RPMH-RSC TCS: invalid register offset 0x%x\n",
                      tcs_reg_offset);
    }

    qemu_log_mask(LOG_TRACE, "RPMH-RSC TCS: read 0x%08x from offset 0x%08x\n",
                  (uint32_t)value, offset);
    return value;
}


/* DRV register space write handler */
static void rpmh_rsc_drv_write(void *opaque, hwaddr addr, uint64_t value,
                              unsigned size)
{
    RpmhRscState *s = RPMH_RSC(opaque);
    uint32_t offset = addr;
    uint32_t val = (uint32_t)value;

    if (offset == s->regs[RSC_DRV_IRQ_ENABLE]) {
        s->irq_enable = val;
        qemu_log_mask(LOG_UNIMP, "RPMH-RSC DRV: IRQ enable write 0x%08x\n",
                      val);
    } else if (offset == s->regs[RSC_DRV_IRQ_CLEAR]) {
        s->irq_status &= ~val;
        qemu_log_mask(LOG_UNIMP, "RPMH-RSC DRV: IRQ clear write 0x%08x\n", val);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "RPMH-RSC DRV: write to invalid register offset 0x%x\n",
                      offset);
    }
}

/* TCS register space write handler */
static void rpmh_rsc_tcs_write(void *opaque, hwaddr addr, uint64_t value,
                              unsigned size)
{
    RpmhRscState *s = RPMH_RSC(opaque);
    uint32_t offset = addr;
    uint32_t val = (uint32_t)value;

    uint32_t tcs_id = offset / s->regs[RSC_DRV_TCS_OFFSET];
    if (tcs_id >= RPMH_RSC_MAX_TCS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "RPMH-RSC TCS: invalid TCS id %u\n", tcs_id);
        return;
    }

    uint32_t tcs_reg_offset = offset - (tcs_id * s->regs[RSC_DRV_TCS_OFFSET]);
    RpmhRscTcsState *tcs = &s->tcs_states[tcs_id];

    if (tcs_reg_offset == s->regs[RSC_DRV_CONTROL]) {
        tcs->control = val;

        /* Handle trigger */
        if (val & TCS_AMC_MODE_TRIGGER) {
            qemu_log_mask(LOG_UNIMP,
                          "RPMH-RSC TCS: TCS %u triggered with "
                          "cmd_enable=0x%08x\n",
                          tcs_id, tcs->cmd_enable);
            tcs->triggered = true;
            tcs->status = 0; /* Clear busy bit */

            /* Mark all enabled commands as completed */
            for (int i = 0; i < RPMH_RSC_MAX_CMDS_PER_TCS; i++) {
                if (tcs->cmd_enable & BIT(i)) {
                    tcs->commands[i].status = CMD_STATUS_ISSUED |
                                              CMD_STATUS_COMPL;
                }
            }

            /* Set IRQ status */
            s->irq_status |= (1 << tcs_id);
        }
    } else if (tcs_reg_offset == s->regs[RSC_DRV_CMD_ENABLE]) {
        tcs->cmd_enable = val;
    } else if (tcs_reg_offset >= s->regs[RSC_DRV_CMD_MSGID]) {
        /* Command register access */
        uint32_t cmd_offset = tcs_reg_offset - s->regs[RSC_DRV_CMD_MSGID];
        uint32_t cmd_id = cmd_offset / s->regs[RSC_DRV_CMD_OFFSET];

        if (cmd_id >= RPMH_RSC_MAX_CMDS_PER_TCS) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "RPMH-RSC TCS: invalid command id %u\n", cmd_id);
            return;
        }

        uint32_t cmd_reg_offset = cmd_offset -
            (cmd_id * s->regs[RSC_DRV_CMD_OFFSET]);
        RpmhRscCommand *cmd = &tcs->commands[cmd_id];

        if (cmd_reg_offset == 0) { /* RSC_DRV_CMD_MSGID */
            cmd->wait = !!(val & CMD_MSGID_RESP_REQ);
        } else if (cmd_reg_offset == 4) { /* RSC_DRV_CMD_ADDR */
            cmd->addr = val;
        } else if (cmd_reg_offset == 8) { /* RSC_DRV_CMD_DATA */
            cmd->data = val;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "RPMH-RSC TCS: invalid command register offset 0x%x\n",
                          cmd_reg_offset);
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "RPMH-RSC TCS: write to invalid register offset 0x%x\n",
                      tcs_reg_offset);
    }
}


static const MemoryRegionOps rpmh_rsc_drv_ops = {
    .read = rpmh_rsc_drv_read,
    .write = rpmh_rsc_drv_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps rpmh_rsc_tcs_ops = {
    .read = rpmh_rsc_tcs_read,
    .write = rpmh_rsc_tcs_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void rpmh_rsc_reset_enter(Object *obj, ResetType type)
{
    RpmhRscState *s = RPMH_RSC(obj);
    RpmhRscClass *rpmh_class = RPMH_RSC_GET_CLASS(obj);

    /* Call parent class enter phase */
    if (rpmh_class->parent_phases.enter) {
        rpmh_class->parent_phases.enter(obj, type);
    }

    /* Reset to default values - local state only */
    s->version = (2 << MAJOR_VER_SHIFT) | (7 << MINOR_VER_SHIFT);
    s->drv_solver_config = 0;
    s->drv_prnt_chld_config = (8 << DRV_NUM_TCS_SHIFT) | (16 << DRV_NCPT_SHIFT);
    s->irq_enable = 0;
    s->irq_status = 0;
    s->tcs_base = 0x00000D00; /* Default TCS base offset */

    /* Reset all TCS states */
    memset(s->tcs_states, 0, sizeof(s->tcs_states));
}

static void rpmh_rsc_realize(DeviceState *dev, Error **errp)
{
    RpmhRscState *s = RPMH_RSC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    /* Use version 2.7 register offsets by default */
    memcpy(s->regs, rpmh_rsc_reg_offset_ver_2_7, sizeof(s->regs));

    /* Initialize TCS base before creating memory regions */
    s->tcs_base = 0x00000D00; /* Default TCS base offset */

    /* Initialize DRV register space (covers base registers) */
    memory_region_init_io(&s->drv_iomem, OBJECT(s), &rpmh_rsc_drv_ops, s,
                          TYPE_RPMH_RSC "-drv", s->tcs_base);
    sysbus_init_mmio(sbd, &s->drv_iomem);

    /* Initialize TCS register space (covers TCS and command registers) */
    memory_region_init_io(&s->tcs_iomem, OBJECT(s), &rpmh_rsc_tcs_ops, s,
                          TYPE_RPMH_RSC "-tcs",
                          RPMH_RSC_REGISTER_SPACE_SIZE - s->tcs_base);
    sysbus_init_mmio(sbd, &s->tcs_iomem);
}

static void rpmh_rsc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    RpmhRscClass *rpmh_class = RPMH_RSC_CLASS(klass);

    dc->realize = rpmh_rsc_realize;
    resettable_class_set_parent_phases(rc, rpmh_rsc_reset_enter, NULL, NULL,
                                       &rpmh_class->parent_phases);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo rpmh_rsc_info = {
    .name          = TYPE_RPMH_RSC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RpmhRscState),
    .class_size    = sizeof(RpmhRscClass),
    .class_init    = rpmh_rsc_class_init,
};

static void rpmh_rsc_register_types(void)
{
    type_register_static(&rpmh_rsc_info);
}

type_init(rpmh_rsc_register_types)
