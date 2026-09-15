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
#include "trace.h"

typedef struct HexagonDMAMemory {
    CPUHexagonState *env;
    int mmu_idx;
    uintptr_t ra;
} HexagonDMAMemory;

void hexagon_dma_reset(HexagonDMAState *dma)
{
    dma->status = DM0_STATUS_IDLE;
    dma->syndrome = 0;
    dma->desc_ptr = 0;
}

static uint32_t dma_ldl(const HexagonDMAMemory *mem, target_ulong addr)
{
    return cpu_ldl_le_data_ra(mem->env, addr, mem->ra);
}

static void dma_stl(const HexagonDMAMemory *mem, target_ulong addr,
                    uint32_t value)
{
    cpu_stl_le_data_ra(mem->env, addr, value, mem->ra);
}

/*
 * Copy one type0 (linear) descriptor's payload: length bytes, src to dst.
 */
static void dma_copy(const HexagonDMAMemory *mem, target_ulong dst,
                     target_ulong src, uint32_t length)
{
    while (length != 0) {
        uint32_t src_page = (1u << TARGET_PAGE_BITS) -
                            (src & ((1u << TARGET_PAGE_BITS) - 1));
        uint32_t dst_page = (1u << TARGET_PAGE_BITS) -
                            (dst & ((1u << TARGET_PAGE_BITS) - 1));
        uint32_t chunk = MIN(length, MIN(src_page, dst_page));
        void *src_host = probe_read(mem->env, src, chunk, mem->mmu_idx,
                                    mem->ra);
        void *dst_host = probe_write(mem->env, dst, chunk, mem->mmu_idx,
                                     mem->ra);

        if (src_host && dst_host) {
            memmove(dst_host, src_host, chunk);
        } else {
            for (uint32_t i = 0; i < chunk; i++) {
                uint8_t byte = cpu_ldub_data_ra(mem->env, src + i, mem->ra);
                cpu_stb_data_ra(mem->env, dst + i, byte, mem->ra);
            }
        }
        src += chunk;
        dst += chunk;
        length -= chunk;
    }
}

static bool dma_probe_range(const HexagonDMAMemory *mem, target_ulong addr,
                            uint32_t length, MMUAccessType access_type)
{
    if (length != 0 && addr + length - 1 < addr) {
        return false;
    }

    while (length != 0) {
        uint32_t page_left = (1u << TARGET_PAGE_BITS) -
                             (addr & ((1u << TARGET_PAGE_BITS) - 1));
        uint32_t chunk = MIN(length, page_left);

        probe_access(mem->env, addr, chunk, access_type, mem->mmu_idx,
                     mem->ra);
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
static void dma_copy_type1(const HexagonDMAMemory *mem, target_ulong desc_va)
{
    target_ulong src = dma_ldl(mem, desc_va + DESC_OFF_SRC);
    target_ulong dst = dma_ldl(mem, desc_va + DESC_OFF_DST);
    uint32_t roi = dma_ldl(mem, desc_va + DESC_OFF_ROI);
    uint32_t stride = dma_ldl(mem, desc_va + DESC_OFF_STRIDE);
    uint32_t width = roi & DESC_ROIWIDTH_MASK;
    uint32_t height = (roi & DESC_ROIHEIGHT_MASK) >> DESC_ROIHEIGHT_SHIFT;
    uint32_t srcstride = stride & DESC_SRCSTRIDE_MASK;
    uint32_t dststride = (stride & DESC_DSTSTRIDE_MASK) >> DESC_DSTSTRIDE_SHIFT;

    for (uint32_t row = 0; row < height; row++) {
        target_ulong srow = src + (target_ulong)row * srcstride;
        target_ulong drow = dst + (target_ulong)row * dststride;

        dma_copy(mem, drow, srow, width);
    }
}

void hexagon_dma_run_chain(CPUHexagonState *env, HexagonDMAState *dma,
                           target_ulong desc_va, uintptr_t ra)
{
    HexagonDMAMemory mem = {
        .env = env,
        .mmu_idx = cpu_mmu_index(env_cpu(env), false),
        .ra = ra,
    };
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
        if (!dma_probe_range(&mem, desc_va, DESC_TYPE0_SIZE,
                             MMU_DATA_LOAD) ||
            !dma_probe_range(&mem, desc_va + DESC_OFF_CTRL, sizeof(ctrl),
                             MMU_DATA_STORE)) {
            return;
        }

        dma->desc_ptr = desc_va;
        ctrl = dma_ldl(&mem, desc_va + DESC_OFF_CTRL);
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
            dma_copy(&mem, dma_ldl(&mem, desc_va + DESC_OFF_DST),
                     dma_ldl(&mem, desc_va + DESC_OFF_SRC), length);
            break;
        case DESC_DESCTYPE_TYPE1: {
            uint32_t roi, stride, width, height, srcstride, dststride;
            target_ulong src, dst;

            if (!dma_probe_range(&mem, desc_va, DESC_TYPE1_SIZE,
                                 MMU_DATA_LOAD) ||
                dma_ldl(&mem, desc_va + DESC_OFF_WIDTHOFFSET) != 0) {
                dma_set_error(dma, htid, desc_va,
                              DMA_SYNDROME_DESCRIPTOR_UNSUPPORTED);
                return;
            }
            src = dma_ldl(&mem, desc_va + DESC_OFF_SRC);
            dst = dma_ldl(&mem, desc_va + DESC_OFF_DST);
            roi = dma_ldl(&mem, desc_va + DESC_OFF_ROI);
            stride = dma_ldl(&mem, desc_va + DESC_OFF_STRIDE);
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

                if (srow > UINT32_MAX || drow > UINT32_MAX) {
                    dma_set_error(dma, htid, desc_va,
                                  DMA_SYNDROME_DESCRIPTOR_UNSUPPORTED);
                    return;
                }
            }
            dma_copy_type1(&mem, desc_va);
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
        dma_stl(&mem, desc_va + DESC_OFF_CTRL, ctrl);

        desc_va = dma_ldl(&mem, desc_va + DESC_OFF_NEXT);
        trace_hexagon_dma_desc(htid, dma->desc_ptr, desctype, desc_va);
    }

    dma->status = DM0_STATUS_IDLE;
    trace_hexagon_dma_done(htid, dma->desc_ptr);
}

void hexagon_dma_link(CPUHexagonState *env, HexagonDMAState *dma,
                      target_ulong new_va, target_ulong tail_va, uintptr_t ra)
{
    HexagonDMAMemory mem = {
        .env = env,
        .mmu_idx = cpu_mmu_index(env_cpu(env), false),
        .ra = ra,
    };
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
    if (!dma_probe_range(&mem, tail_va + DESC_OFF_NEXT, sizeof(uint32_t),
                         MMU_DATA_STORE)) {
        return;
    }

    dma_stl(&mem, tail_va + DESC_OFF_NEXT, new_va);
    dma->status = DM0_STATUS_IDLE;
}
