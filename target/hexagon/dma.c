/*
 * Hexagon User-DMA Engine
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "dma.h"
#include "hw/core/resettable.h"

/*
 * migration/vmstate.c is system_ss-only (see migration/meson.build), so
 * this device deliberately has no dc->vmsd: it lives in the common
 * hexagon_ss source set and must link into hexagon-linux-user too, where
 * migration doesn't exist at all.
 */
static void hexagon_dma_reset_hold(Object *obj, ResetType type)
{
    HexagonDMAState *s = HEXAGON_DMA(obj);

    s->status = DM0_STATUS_IDLE;
    s->syndrome = 0;
    s->desc_ptr = 0;
}

static void hexagon_dma_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = hexagon_dma_reset_hold;
    dc->user_creatable = false;
}

static const TypeInfo hexagon_dma_info = {
    .name = TYPE_HEXAGON_DMA,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(HexagonDMAState),
    .class_init = hexagon_dma_class_init,
};

static void hexagon_dma_register_types(void)
{
    type_register_static(&hexagon_dma_info);
}

type_init(hexagon_dma_register_types)
