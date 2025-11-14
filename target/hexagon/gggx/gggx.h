/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright(c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 */
#ifndef HEXAGON_GGGX_H
#define HEXAGON_GGGX_H

#define NUM_GGGX_REGS 12
#define GGGX_VEC_SIZE_BYTES 64

typedef struct {
    union {
        uint64_t ud[GGGX_VEC_SIZE_BYTES / 8];
        int64_t  d[GGGX_VEC_SIZE_BYTES / 8];
        uint32_t uw[GGGX_VEC_SIZE_BYTES / 4];
        int32_t  w[GGGX_VEC_SIZE_BYTES / 4];
        uint16_t uh[GGGX_VEC_SIZE_BYTES / 2];
        int16_t  h[GGGX_VEC_SIZE_BYTES / 2];
        uint8_t  ub[GGGX_VEC_SIZE_BYTES / 1];
        int8_t   b[GGGX_VEC_SIZE_BYTES / 1];
    };
} GGGXVector;

static inline GGGXVector gggx_zero_vector(void)
{
    GGGXVector ret;
    memset(&ret, 0, sizeof(ret));
    return ret;
}

#endif
