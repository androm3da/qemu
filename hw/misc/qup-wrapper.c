/*
 * QUP wrapper device
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-properties.h"
#include "hw/misc/qup-wrapper.h"
#include "qemu/log.h"

#define REG_QUPV3_HW_VER 0x4

OBJECT_DEFINE_SIMPLE_TYPE(QupWrapperState, qup_wrapper, QUP_WRAPPER,
                          SYS_BUS_DEVICE)

static uint64_t qup_wrapper_read(void *opaque, hwaddr offset, unsigned size)
{
    QupWrapperState *s = QUP_WRAPPER(opaque);
    uint64_t value = 0;

    switch (offset) {
    case REG_QUPV3_HW_VER:
        value = s->hw_version;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "qup-wrapper: read from unknown offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }

    return value;
}

static void qup_wrapper_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "qup-wrapper: write to offset 0x%"
                  HWADDR_PRIx " value 0x%" PRIx64 "\n", offset, value);
}

static const MemoryRegionOps qup_wrapper_ops = {
    .read = qup_wrapper_read,
    .write = qup_wrapper_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void qup_wrapper_init(Object *obj)
{
    QupWrapperState *s = QUP_WRAPPER(obj);

    memory_region_init_io(&s->iomem, obj, &qup_wrapper_ops, s,
                          TYPE_QUP_WRAPPER, QUP_WRAPPER_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const Property qup_wrapper_properties[] = {
    DEFINE_PROP_UINT32("hw-version", QupWrapperState, hw_version,
                       0x30100000),
};

static void qup_wrapper_finalize(Object *obj)
{
}

static void qup_wrapper_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, qup_wrapper_properties);
    dc->desc = "Qualcomm QUP wrapper";
}
