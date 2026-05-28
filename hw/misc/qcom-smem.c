/*
 * Qualcomm Shared Memory (SMEM) Device Model
 *
 * SMEM is a shared memory region used for inter-processor communication
 * on Qualcomm SoCs. This device model creates the SMEM data structures
 * that would normally be initialized by the bootloader.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-properties.h"
#include "hw/misc/qcom-smem.h"
#include "qapi/error.h"
#include "qemu/module.h"

static void qcom_smem_realize(DeviceState *dev, Error **errp)
{
    QcomSMEMState *s = QCOM_SMEM(dev);
    const QcomSMEMTargetConfig *cfg;

    /* Find target configuration */
    if (!s->target_name) {
        s->target_name = g_strdup("sa8775p");  /* Default */
    }

    cfg = qcom_smem_find_target_config(s->target_name);
    if (!cfg) {
        error_setg(errp, "Unknown SMEM target: %s", s->target_name);
        return;
    }
    s->target_config = cfg;
    s->smem_size = cfg->smem_size;

    /* Allocate backing memory */
    s->smem_base = g_malloc0(s->smem_size);

    /* Initialize all SMEM structures */
    qcom_smem_fill_region(s->smem_base, s->smem_size, s->target_config);

    /* Create memory region */
    memory_region_init_ram_ptr(&s->smem_region, OBJECT(dev),
                               "qcom-smem", s->smem_size, s->smem_base);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->smem_region);
}

static void qcom_smem_unrealize(DeviceState *dev)
{
    QcomSMEMState *s = QCOM_SMEM(dev);

    g_free(s->smem_base);
    s->smem_base = NULL;
}

static const Property qcom_smem_properties[] = {
    DEFINE_PROP_STRING("target", QcomSMEMState, target_name),
    DEFINE_PROP_UINT64("map-addr", QcomSMEMState, map_addr, 0x90900000),
};

static void qcom_smem_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = qcom_smem_realize;
    dc->unrealize = qcom_smem_unrealize;
    device_class_set_props(dc, qcom_smem_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo qcom_smem_info = {
    .name          = TYPE_QCOM_SMEM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(QcomSMEMState),
    .class_init    = qcom_smem_class_init,
};

static void qcom_smem_register_types(void)
{
    type_register_static(&qcom_smem_info);
}

type_init(qcom_smem_register_types)

/*
 * Public API implementations
 */

QcomSMEMState *qcom_smem_create(const char *target_name, hwaddr map_addr)
{
    DeviceState *dev;

    dev = qdev_new(TYPE_QCOM_SMEM);
    if (target_name) {
        qdev_prop_set_string(dev, "target", target_name);
    }
    qdev_prop_set_uint64(dev, "map-addr", map_addr);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, map_addr);

    return QCOM_SMEM(dev);
}

void *qcom_smem_get_base(QcomSMEMState *s)
{
    return s->smem_base;
}

uint32_t qcom_smem_get_size(QcomSMEMState *s)
{
    return s->smem_size;
}
