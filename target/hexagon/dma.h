/*
 * Hexagon User-DMA Engine QOM Object
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * This models "Hexagon V68 Architecture System-Level Specification -
 * User DMA": the type0 (linear) and type1 (2D box-copy) descriptor
 * formats and the dmstart/dmlink/dmpoll/dmwait/dmpause/dmresume
 * instructions.  Privileged config-space access (dmcfgrd/dmcfgwr) and
 * TLB sync (dmsyncht/dmtlbsynch) are out of scope: they have no
 * linux-user access path and stay as the existing UNIMP stubs.
 */

#ifndef HEXAGON_DMA_H
#define HEXAGON_DMA_H

#include "exec/target_long.h"
#include "hw/core/qdev.h"
#include "qom/object.h"

#define TYPE_HEXAGON_DMA "hexagon-dma"
OBJECT_DECLARE_SIMPLE_TYPE(HexagonDMAState, HEXAGON_DMA)

/* DM0 status, as observed by dmpoll/dmwait/dmpause. */
#define DM0_STATUS_IDLE   0x00000000
#define DM0_STATUS_RUN    0x00000001
#define DM0_STATUS_ERROR  0x00000002

/*
 * Error syndrome codes, a subset of the real hardware's DM1 syndrome
 * encoding relevant to the validation this model performs.
 */
#define DMA_SYNDROME_DESCRIPTOR_INVALID_ALIGNMENT  1
#define DMA_SYNDROME_DESCRIPTOR_INVALID_TYPE       2

/*
 * Descriptor byte layout, common to both types.  Guest memory is never
 * aliased through a host struct: every field is read/written with the
 * normal cpu_ld/st*_data_ra() accessors at these offsets, so ordinary
 * guest MMU/fault behavior applies to descriptor and payload access
 * alike.
 */
#define DESC_OFF_NEXT               0
#define DESC_OFF_CTRL               4
#define DESC_OFF_SRC                8
#define DESC_OFF_DST                12
#define DESC_TYPE0_SIZE             16

#define DESC_OFF_ALLOC_PADDING      16
#define DESC_OFF_ROI                20   /* roiheight:roiwidth */
#define DESC_OFF_STRIDE             24   /* dststride:srcstride */
#define DESC_OFF_WIDTHOFFSET        28   /* dstwidthoffset:srcwidthoffset */
#define DESC_TYPE1_SIZE             32

#define DESC_ALIGNMENT              16

/* CTRL word (offset DESC_OFF_CTRL) */
#define DESC_DSTATE_MASK            0x80000000
#define DESC_DSTATE_SHIFT           31
#define DESC_ORDER_MASK             0x40000000
#define DESC_ORDER_SHIFT            30
#define DESC_BYPASSSRC_MASK         0x20000000
#define DESC_BYPASSDST_MASK         0x10000000
#define DESC_SRCCOMP_MASK           0x08000000
#define DESC_DSTCOMP_MASK           0x04000000
#define DESC_DESCTYPE_MASK          0x03000000
#define DESC_DESCTYPE_SHIFT         24
#define DESC_DESCTYPE_TYPE0         0
#define DESC_DESCTYPE_TYPE1         1
#define DESC_LENGTH_MASK            0x00FFFFFF

/* ROI word (offset DESC_OFF_ROI), type1 only */
#define DESC_ROIWIDTH_MASK          0x0000FFFF
#define DESC_ROIHEIGHT_MASK         0xFFFF0000
#define DESC_ROIHEIGHT_SHIFT        16

/* Stride word (offset DESC_OFF_STRIDE), type1 only */
#define DESC_SRCSTRIDE_MASK         0x0000FFFF
#define DESC_DSTSTRIDE_MASK         0xFFFF0000
#define DESC_DSTSTRIDE_SHIFT        16

struct HexagonDMAState {
    DeviceState parent_obj;

    /* DM0: idle / run / error, observed by dmpoll/dmwait/dmpause. */
    uint32_t status;
    /* DM1-shaped syndrome for the last error, valid while status is error. */
    uint32_t syndrome;
    /* Guest VA of the descriptor last started/resumed from. */
    target_ulong desc_ptr;
};

#endif /* HEXAGON_DMA_H */
