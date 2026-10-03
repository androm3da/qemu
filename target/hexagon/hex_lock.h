/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HEXAGON_HEX_LOCK_H
#define HEXAGON_HEX_LOCK_H

#include "cpu.h"
#include "hw/hexagon/hexagon_globalreg.h"

void hexagon_lock(CPUHexagonState *env, HexagonGlobalLock which);
void hexagon_unlock(CPUHexagonState *env, HexagonGlobalLock which);
bool hexagon_locks_owned(CPUHexagonState *env);
bool hexagon_locks_waiting(CPUHexagonState *env);

#endif
