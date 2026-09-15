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

static HexagonDMAState *dma_of(CPUHexagonState *env)
{
    return &HEXAGON_CPU(env_cpu(env))->dma;
}

void HELPER(dmstart)(CPUHexagonState *env, uint32_t RsV)
{
    trace_hexagon_dma_start(env_cpu(env)->cpu_index, RsV);
    hexagon_dma_run_chain(env, dma_of(env), RsV, GETPC());
}

void HELPER(dmresume)(CPUHexagonState *env, uint32_t RsV)
{
    trace_hexagon_dma_resume(env_cpu(env)->cpu_index, RsV);
    hexagon_dma_run_chain(env, dma_of(env), RsV, GETPC());
}

void HELPER(dmlink)(CPUHexagonState *env, uint32_t RsV, uint32_t RtV)
{
    hexagon_dma_link(env, dma_of(env), RsV, RtV, GETPC());
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
    /* Nothing is ever in flight to pause; report the terminal status. */
    return dma_of(env)->status;
}
