/*
 * Hexagon HVX Extension Context QOM Object
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HEXAGON_HVX_CONTEXT_H
#define HEXAGON_HVX_CONTEXT_H

#include "hw/core/sysbus.h"
#include "qom/object.h"
#include "target/hexagon/cpu.h"

#define TYPE_HEXAGON_HVX_CONTEXT "hexagon-hvx-context"
OBJECT_DECLARE_SIMPLE_TYPE(HexagonHVXContextState, HEXAGON_HVX_CONTEXT)

/*
 * One HVX extension context.  A core has cfgtable.ext_contexts of them,
 * fewer than its hardware threads in general, and each thread's SSR:XA
 * selects the one it uses.  A context is therefore not owned by any one
 * CPU, which is why it is a device of its own rather than CPU state.
 */
struct HexagonHVXContextState {
    SysBusDevice parent_obj;

    /* Physical context number, as SSR:XA maps to it. */
    uint32_t index;

    HexagonHVXContext regs;
};

#endif /* HEXAGON_HVX_CONTEXT_H */
