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
#include "accel/tcg/probe.h"
#include "hw/core/resettable.h"
#include "trace.h"

/*
 * This common source must also link into hexagon-linux-user, where migration
 * is unavailable.  System emulation migrates the embedded state as part of
 * vmstate_hexagon_cpu in machine.c rather than through dc->vmsd here.
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

static bool dma_probe_range(CPUHexagonState *env, target_ulong addr,
                            uint32_t length, MMUAccessType access_type,
                            uintptr_t ra)
{
    int mmu_idx = cpu_mmu_index(env_cpu(env), false);

    if (length != 0 && addr + length - 1 < addr) {
        return false;
    }

    while (length != 0) {
        uint32_t page_left = (1u << TARGET_PAGE_BITS) -
                             (addr & ((1u << TARGET_PAGE_BITS) - 1));
        uint32_t chunk = MIN(length, page_left);

        probe_access(env, addr, chunk, access_type, mmu_idx, ra);
        addr += chunk;
        length -= chunk;
    }
    return true;
}

static void dma_set_error(HexagonDMAState *dma, uint32_t htid,
                          target_ulong desc_va, uint32_t syndrome)
{
    dma->status = DM0_STATUS_ERROR;
    dma->syndrome = syndrome;
    dma->desc_ptr = desc_va;
    trace_hexagon_dma_error(htid, desc_va, syndrome);
}

/*
 * Copy one type1 (2D box) descriptor's payload: a roiheight x roiwidth
 * block, advancing src/dst by srcstride/dststride between rows.
 *
 * Descriptors with nonzero srcwidthoffset/dstwidthoffset are rejected before
 * this function is called because those fields are not modeled.
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
    uint32_t desc_count = 0;
    uint64_t bytes_copied = 0;
    target_ulong visited[DMA_MAX_CHAIN_DESCRIPTORS];

    while (desc_va != 0) {
        uint32_t ctrl, desctype, length = 0;

        if (desc_va % DESC_ALIGNMENT != 0) {
            dma_set_error(dma, htid, desc_va,
                          DMA_SYNDROME_DESCRIPTOR_INVALID_ALIGNMENT);
            return;
        }

        if (desc_count == DMA_MAX_CHAIN_DESCRIPTORS) {
            dma_set_error(dma, htid, desc_va,
                          DMA_SYNDROME_DESCRIPTOR_CHAIN_LIMIT);
            return;
        }
        for (uint32_t i = 0; i < desc_count; i++) {
            if (visited[i] == desc_va) {
                dma_set_error(dma, htid, desc_va,
                              DMA_SYNDROME_DESCRIPTOR_CHAIN_LIMIT);
                return;
            }
        }
        visited[desc_count++] = desc_va;

        /* Leave a terminal state behind if probing raises an exception. */
        dma->status = DM0_STATUS_ERROR;
        dma->syndrome = DMA_SYNDROME_MEMORY_ACCESS;
        dma->desc_ptr = desc_va;
        if (!dma_probe_range(env, desc_va, DESC_TYPE0_SIZE,
                             MMU_DATA_LOAD, ra) ||
            !dma_probe_range(env, desc_va + DESC_OFF_CTRL, sizeof(ctrl),
                             MMU_DATA_STORE, ra)) {
            return;
        }

        dma->desc_ptr = desc_va;
        ctrl = cpu_ldl_le_data_ra(env, desc_va + DESC_OFF_CTRL, ra);
        desctype = (ctrl & DESC_DESCTYPE_MASK) >> DESC_DESCTYPE_SHIFT;

        if (ctrl & DESC_UNSUPPORTED_CTRL_MASK) {
            dma_set_error(dma, htid, desc_va,
                          DMA_SYNDROME_DESCRIPTOR_UNSUPPORTED);
            return;
        }

        switch (desctype) {
        case DESC_DESCTYPE_TYPE0:
            length = ctrl & DESC_LENGTH_MASK;
            if (bytes_copied + length > DMA_MAX_CHAIN_BYTES) {
                dma_set_error(dma, htid, desc_va,
                              DMA_SYNDROME_DESCRIPTOR_CHAIN_LIMIT);
                return;
            }
            if (!dma_probe_range(env,
                                 cpu_ldl_le_data_ra(env,
                                     desc_va + DESC_OFF_SRC, ra),
                                 length, MMU_DATA_LOAD, ra) ||
                !dma_probe_range(env,
                                 cpu_ldl_le_data_ra(env,
                                     desc_va + DESC_OFF_DST, ra),
                                 length, MMU_DATA_STORE, ra)) {
                return;
            }
            dma->status = DM0_STATUS_RUN;
            dma_copy_type0(env, desc_va, ctrl, ra);
            break;
        case DESC_DESCTYPE_TYPE1: {
            uint32_t roi, stride, width, height, srcstride, dststride;
            target_ulong src, dst;

            if (!dma_probe_range(env, desc_va, DESC_TYPE1_SIZE,
                                 MMU_DATA_LOAD, ra) ||
                cpu_ldl_le_data_ra(env,
                    desc_va + DESC_OFF_WIDTHOFFSET, ra) != 0) {
                dma_set_error(dma, htid, desc_va,
                              DMA_SYNDROME_DESCRIPTOR_UNSUPPORTED);
                return;
            }
            src = cpu_ldl_le_data_ra(env, desc_va + DESC_OFF_SRC, ra);
            dst = cpu_ldl_le_data_ra(env, desc_va + DESC_OFF_DST, ra);
            roi = cpu_ldl_le_data_ra(env, desc_va + DESC_OFF_ROI, ra);
            stride = cpu_ldl_le_data_ra(env, desc_va + DESC_OFF_STRIDE, ra);
            width = roi & DESC_ROIWIDTH_MASK;
            height = (roi & DESC_ROIHEIGHT_MASK) >> DESC_ROIHEIGHT_SHIFT;
            srcstride = stride & DESC_SRCSTRIDE_MASK;
            dststride = (stride & DESC_DSTSTRIDE_MASK) >>
                        DESC_DSTSTRIDE_SHIFT;
            length = width * height;
            if (bytes_copied + length > DMA_MAX_CHAIN_BYTES) {
                dma_set_error(dma, htid, desc_va,
                              DMA_SYNDROME_DESCRIPTOR_CHAIN_LIMIT);
                return;
            }
            for (uint32_t row = 0; row < height; row++) {
                uint64_t srow = src + (uint64_t)row * srcstride;
                uint64_t drow = dst + (uint64_t)row * dststride;

                if (srow > UINT32_MAX || drow > UINT32_MAX ||
                    !dma_probe_range(env, srow, width, MMU_DATA_LOAD, ra) ||
                    !dma_probe_range(env, drow, width, MMU_DATA_STORE, ra)) {
                    return;
                }
            }
            dma->status = DM0_STATUS_RUN;
            dma_copy_type1(env, desc_va, ra);
            break;
        }
        default:
            dma_set_error(dma, htid, desc_va,
                          DMA_SYNDROME_DESCRIPTOR_INVALID_TYPE);
            return;
        }
        bytes_copied += length;

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

    dma->status = DM0_STATUS_ERROR;
    dma->syndrome = DMA_SYNDROME_MEMORY_ACCESS;
    dma->desc_ptr = tail_va;
    if (!dma_probe_range(env, tail_va + DESC_OFF_NEXT, sizeof(uint32_t),
                         MMU_DATA_STORE, ra)) {
        return;
    }

    cpu_stl_le_data_ra(env, tail_va + DESC_OFF_NEXT, new_va, ra);
    dma->status = DM0_STATUS_IDLE;
}
