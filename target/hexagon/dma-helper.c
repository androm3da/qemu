/*
 * Hexagon User-DMA instruction helpers
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "dma.h"
#include "exec/helper-proto.h"
#include "trace.h"
#ifndef CONFIG_USER_ONLY
#include "hw/hexagon/hexagon_globalreg.h"
#endif

static HexagonDMAState *dma_of(CPUHexagonState *env)
{
    HexagonCPU *cpu = HEXAGON_CPU(env_cpu(env));

#ifndef CONFIG_USER_ONLY
    cpu->dma.globalregs = cpu->globalregs;
#endif
    return &cpu->dma;
}

void HELPER(dmstart)(CPUHexagonState *env, uint32_t RsV)
{
    trace_hexagon_dma_start(env_cpu(env)->cpu_index, RsV);
    hexagon_dma_run_chain(env, dma_of(env), RsV, GETPC());
}

void HELPER(dmresume)(CPUHexagonState *env, uint32_t RsV)
{
    trace_hexagon_dma_resume(env_cpu(env)->cpu_index, RsV);
    hexagon_dma_run_chain(env, dma_of(env), RsV & ~0xf, GETPC());
}

void HELPER(dmlink)(CPUHexagonState *env, uint32_t RsV, uint32_t RtV)
{
    HexagonDMAState *dma = dma_of(env);

    hexagon_dma_link(env, dma, RsV, RtV, GETPC());
    if (dma->status == DM0_STATUS_IDLE) {
        hexagon_dma_run_chain(env, dma, RtV, GETPC());
    }
}

uint32_t HELPER(dmpoll)(CPUHexagonState *env)
{
    return dma_of(env)->status;
}

uint32_t HELPER(dmwait)(CPUHexagonState *env)
{
    /*
     * The engine is synchronous: there is never a "running" state to
     * wait for by the time dmwait executes.
     */
    return dma_of(env)->status;
}

uint32_t HELPER(dmpause)(CPUHexagonState *env)
{
    HexagonDMAState *dma = dma_of(env);
    uint32_t status = dma->status;

    dma->status = DM0_STATUS_IDLE;
    dma->desc_ptr = 0;
    return status;
}

uint32_t HELPER(dmcfgrd)(CPUHexagonState *env, uint32_t index)
{
    HexagonDMAState *dma = dma_of(env);

    if (index == 0) {
        return dma->status;
    }
#ifndef CONFIG_USER_ONLY
    if (dma->globalregs) {
        return hexagon_dma_config_read(dma->globalregs, index);
    }
#endif
    return 0;
}

void HELPER(dmcfgwr)(CPUHexagonState *env, uint32_t index, uint32_t value)
{
#ifndef CONFIG_USER_ONLY
    HexagonDMAState *dma = dma_of(env);

    if (dma->globalregs) {
        hexagon_dma_config_write(dma->globalregs, index, value);
    }
#endif
}

uint32_t HELPER(dmsyncht)(CPUHexagonState *env)
{
    /* Synchronous DMA leaves no posted transactions to drain. */
    return dma_of(env)->status;
}

uint32_t HELPER(dmtlbsynch)(CPUHexagonState *env)
{
    /* Synchronous DMA leaves no TLB-associated transactions to drain. */
    return dma_of(env)->status;
}
