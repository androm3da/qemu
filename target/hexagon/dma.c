/*
 * Hexagon User-DMA Engine
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "dma.h"
#include "accel/tcg/cpu-ldst.h"
#include "hw/core/resettable.h"
#include "trace.h"

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

/*
 * Copy one type0 (linear) descriptor's payload: length bytes, src to dst.
 */
static void dma_copy_type0(CPUHexagonState *env, target_ulong desc_va,
                           uint32_t ctrl, uintptr_t ra)
{
    target_ulong src = cpu_ldl_le_data_ra(env, desc_va + DESC_OFF_SRC, ra);
    target_ulong dst = cpu_ldl_le_data_ra(env, desc_va + DESC_OFF_DST, ra);
    uint32_t length = ctrl & DESC_LENGTH_MASK;

    for (uint32_t i = 0; i < length; i++) {
        uint8_t byte = cpu_ldub_data_ra(env, src + i, ra);
        cpu_stb_data_ra(env, dst + i, byte, ra);
    }
}

/*
 * Copy one type1 (2D box) descriptor's payload: a roiheight x roiwidth
 * block, advancing src/dst by srcstride/dststride between rows.
 *
 * The descriptor's srcwidthoffset/dstwidthoffset fields are not modeled:
 * the public v68 spec text describing them is ambiguous about the
 * resulting padding behavior, so this implementation sticks to the
 * unambiguous stride-based row copy rather than guess.
 */
static void dma_copy_type1(CPUHexagonState *env, target_ulong desc_va,
                           uintptr_t ra)
{
    target_ulong src = cpu_ldl_le_data_ra(env, desc_va + DESC_OFF_SRC, ra);
    target_ulong dst = cpu_ldl_le_data_ra(env, desc_va + DESC_OFF_DST, ra);
    uint32_t roi = cpu_ldl_le_data_ra(env, desc_va + DESC_OFF_ROI, ra);
    uint32_t stride = cpu_ldl_le_data_ra(env, desc_va + DESC_OFF_STRIDE, ra);
    uint32_t width = roi & DESC_ROIWIDTH_MASK;
    uint32_t height = (roi & DESC_ROIHEIGHT_MASK) >> DESC_ROIHEIGHT_SHIFT;
    uint32_t srcstride = stride & DESC_SRCSTRIDE_MASK;
    uint32_t dststride = (stride & DESC_DSTSTRIDE_MASK) >> DESC_DSTSTRIDE_SHIFT;

    for (uint32_t row = 0; row < height; row++) {
        target_ulong srow = src + (target_ulong)row * srcstride;
        target_ulong drow = dst + (target_ulong)row * dststride;

        for (uint32_t col = 0; col < width; col++) {
            uint8_t byte = cpu_ldub_data_ra(env, srow + col, ra);
            cpu_stb_data_ra(env, drow + col, byte, ra);
        }
    }
}

void hexagon_dma_run_chain(CPUHexagonState *env, HexagonDMAState *dma,
                           target_ulong desc_va, uintptr_t ra)
{
    uint32_t htid = env_cpu(env)->cpu_index;

    dma->status = DM0_STATUS_RUN;

    while (desc_va != 0) {
        uint32_t ctrl, desctype;

        if (desc_va % DESC_ALIGNMENT != 0) {
            dma->status = DM0_STATUS_ERROR;
            dma->syndrome = DMA_SYNDROME_DESCRIPTOR_INVALID_ALIGNMENT;
            dma->desc_ptr = desc_va;
            trace_hexagon_dma_error(htid, desc_va, dma->syndrome);
            return;
        }

        dma->desc_ptr = desc_va;
        ctrl = cpu_ldl_le_data_ra(env, desc_va + DESC_OFF_CTRL, ra);
        desctype = (ctrl & DESC_DESCTYPE_MASK) >> DESC_DESCTYPE_SHIFT;

        switch (desctype) {
        case DESC_DESCTYPE_TYPE0:
            dma_copy_type0(env, desc_va, ctrl, ra);
            break;
        case DESC_DESCTYPE_TYPE1:
            dma_copy_type1(env, desc_va, ra);
            break;
        default:
            dma->status = DM0_STATUS_ERROR;
            dma->syndrome = DMA_SYNDROME_DESCRIPTOR_INVALID_TYPE;
            trace_hexagon_dma_error(htid, desc_va, dma->syndrome);
            return;
        }

        /* Mark the descriptor complete before following the chain. */
        ctrl |= DESC_DSTATE_MASK;
        cpu_stl_le_data_ra(env, desc_va + DESC_OFF_CTRL, ctrl, ra);

        desc_va = cpu_ldl_le_data_ra(env, desc_va + DESC_OFF_NEXT, ra);
        trace_hexagon_dma_desc(htid, dma->desc_ptr, desctype, desc_va);
    }

    dma->status = DM0_STATUS_IDLE;
    trace_hexagon_dma_done(htid, dma->desc_ptr);
}

void hexagon_dma_link(CPUHexagonState *env, HexagonDMAState *dma,
                      target_ulong new_va, target_ulong tail_va, uintptr_t ra)
{
    uint32_t htid = env_cpu(env)->cpu_index;

    trace_hexagon_dma_link(htid, new_va, tail_va);

    if (tail_va % DESC_ALIGNMENT != 0) {
        dma->status = DM0_STATUS_ERROR;
        dma->syndrome = DMA_SYNDROME_DESCRIPTOR_INVALID_ALIGNMENT;
        dma->desc_ptr = tail_va;
        trace_hexagon_dma_error(htid, tail_va, dma->syndrome);
        return;
    }

    cpu_stl_le_data_ra(env, tail_va + DESC_OFF_NEXT, new_va, ra);
}
