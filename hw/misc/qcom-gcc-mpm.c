/*
 * Qualcomm GCC-MPM (Global Control Counter MSM Power Manager)
 *
 * This device implements the GCC-MPM controller which provides timer/counter
 * functionality for system timing and memory protection management.
 *
 * Copyright (c) 2025 Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-properties.h"
#include "hw/misc/qcom-gcc-mpm.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "trace.h"


static uint64_t qcom_gcc_mpm_read(void *opaque, hwaddr offset, unsigned size)
{
    QcomGccMpmState *s = QCOM_GCC_MPM(opaque);

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "qcom-gcc-mpm: read with size %d at offset 0x%"
                      HWADDR_PRIx "\n", size, offset);
        return 0;
    }

    if (offset >= GCC_MPM_REGION_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "qcom-gcc-mpm: invalid read at offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }

    uint32_t reg_index = offset / 4;
    uint32_t value = 0;

    switch (offset) {
    case GCC_MPM_CONTROL_CNTCR:
    case GCC_MPM_CONTROL_CNTSR:
    case GCC_MPM_CONTROL_CNTFID0:
    case GCC_MPM_CONTROL_ID:
        value = s->regs[reg_index];
        break;
    case GCC_MPM_CONTROL_CNTCV_L:
        /* Calculate current counter value if counter is enabled */
        if (s->regs[GCC_MPM_CONTROL_CNTCR / 4] & 0x1) {
            uint64_t elapsed_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) -
                                  s->counter_offset;
            uint64_t freq = s->regs[GCC_MPM_CONTROL_CNTFID0 / 4];
            if (freq > 0) {
                uint64_t counter_ticks = (elapsed_ns * freq) / 1000000000ULL;
                s->regs[GCC_MPM_CONTROL_CNTCV_L / 4] =
                    (uint32_t)counter_ticks;
                s->regs[GCC_MPM_CONTROL_CNTCV_HI / 4] =
                    (uint32_t)(counter_ticks >> 32);
            }
        }
        value = s->regs[reg_index];
        break;
    case GCC_MPM_CONTROL_CNTCV_HI:
        /* Calculate current counter value if counter is enabled */
        if (s->regs[GCC_MPM_CONTROL_CNTCR / 4] & 0x1) {
            uint64_t elapsed_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) -
                                  s->counter_offset;
            uint64_t freq = s->regs[GCC_MPM_CONTROL_CNTFID0 / 4];
            if (freq > 0) {
                uint64_t counter_ticks = (elapsed_ns * freq) / 1000000000ULL;
                s->regs[GCC_MPM_CONTROL_CNTCV_L / 4] =
                    (uint32_t)counter_ticks;
                s->regs[GCC_MPM_CONTROL_CNTCV_HI / 4] =
                    (uint32_t)(counter_ticks >> 32);
            }
        }
        value = s->regs[reg_index];
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "qcom-gcc-mpm: unimplemented read at offset 0x%"
                      HWADDR_PRIx "\n", offset);
        value = 0;
        break;
    }

    trace_qcom_gcc_mpm_read(offset, value);
    return value;
}

static void qcom_gcc_mpm_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    QcomGccMpmState *s = QCOM_GCC_MPM(opaque);

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "qcom-gcc-mpm: write with size %d at offset 0x%"
                      HWADDR_PRIx "\n", size, offset);
        return;
    }

    if (offset >= GCC_MPM_REGION_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "qcom-gcc-mpm: invalid write at offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return;
    }

    uint32_t reg_index = offset / 4;

    trace_qcom_gcc_mpm_write(offset, value);

    switch (offset) {
    case GCC_MPM_CONTROL_CNTCR:
        s->regs[reg_index] = value;
        break;
    case GCC_MPM_CONTROL_CNTCV_L:
        /* Store written value and update counter offset */
        s->regs[reg_index] = value;
        {
            uint64_t new_counter =
                ((uint64_t)s->regs[GCC_MPM_CONTROL_CNTCV_HI / 4] << 32) |
                value;
            uint64_t freq = s->regs[GCC_MPM_CONTROL_CNTFID0 / 4];
            if (freq > 0) {
                uint64_t counter_ns = (new_counter * 1000000000ULL) / freq;
                s->counter_offset =
                    qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - counter_ns;
            }
        }
        break;
    case GCC_MPM_CONTROL_CNTCV_HI:
        /* Store written value and update counter offset */
        s->regs[reg_index] = value;
        {
            uint64_t new_counter = ((uint64_t)value << 32) |
                                   s->regs[GCC_MPM_CONTROL_CNTCV_L / 4];
            uint64_t freq = s->regs[GCC_MPM_CONTROL_CNTFID0 / 4];
            if (freq > 0) {
                uint64_t counter_ns = (new_counter * 1000000000ULL) / freq;
                s->counter_offset =
                    qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - counter_ns;
            }
        }
        break;
    case GCC_MPM_CONTROL_CNTFID0:
        s->regs[reg_index] = value;
        break;
    case GCC_MPM_CONTROL_CNTSR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "qcom-gcc-mpm: write to read-only register CNTSR\n");
        break;
    case GCC_MPM_CONTROL_ID:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "qcom-gcc-mpm: write to read-only register ID\n");
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "qcom-gcc-mpm: unimplemented write at offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps qcom_gcc_mpm_ops = {
    .read = qcom_gcc_mpm_read,
    .write = qcom_gcc_mpm_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void qcom_gcc_mpm_reset_hold(Object *obj, ResetType type)
{
    QcomGccMpmState *s = QCOM_GCC_MPM(obj);

    /* Reset all registers */
    memset(s->regs, 0, sizeof(s->regs));

    /* Set default values */
    s->regs[GCC_MPM_CONTROL_CNTFID0 / 4] = GCC_MPM_DEFAULT_FREQ;
    s->regs[GCC_MPM_CONTROL_ID / 4] = GCC_MPM_DEFAULT_ID;

    /* Reset counter offset */
    s->counter_offset = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}

static void qcom_gcc_mpm_realize(DeviceState *dev, Error **errp)
{
    QcomGccMpmState *s = QCOM_GCC_MPM(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    /* Initialize memory region */
    memory_region_init_io(&s->iomem, OBJECT(s), &qcom_gcc_mpm_ops, s,
                          TYPE_QCOM_GCC_MPM, GCC_MPM_REGION_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_qcom_gcc_mpm = {
    .name = "qcom-gcc-mpm",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, QcomGccMpmState, GCC_MPM_REGION_SIZE / 4),
        VMSTATE_INT64(counter_offset, QcomGccMpmState),
        VMSTATE_END_OF_LIST()
    }
};

static void qcom_gcc_mpm_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = qcom_gcc_mpm_realize;
    dc->vmsd = &vmstate_qcom_gcc_mpm;
    rc->phases.hold = qcom_gcc_mpm_reset_hold;
    dc->desc = "Qualcomm GCC-MPM (Global Control Counter MSM Power Manager)";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo qcom_gcc_mpm_info = {
    .name          = TYPE_QCOM_GCC_MPM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(QcomGccMpmState),
    .class_init    = qcom_gcc_mpm_class_init,
};

static void qcom_gcc_mpm_register_types(void)
{
    type_register_static(&qcom_gcc_mpm_info);
}

type_init(qcom_gcc_mpm_register_types)
