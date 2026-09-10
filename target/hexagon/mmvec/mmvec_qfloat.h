/*
 *  Copyright(c) 2019-2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * HVX qfloat (qf16/qf32) extended-precision arithmetic.
 *
 * qfloat is an internal "quasi-float" intermediate format used to chain
 * consecutive HVX floating-point operations without rounding at each step:
 * a qf16/qf32 result carries a few extra bits of precision beyond its
 * visible 16/32-bit mantissa/exponent, stashed in the vector register's
 * "extended bits" (MMVector.ext[], one nibble/2-bit-pair per element).
 * Those extra bits are only meaningful as the tail end of a qfloat chain;
 * any other instruction that writes the register leaves them unspecified
 * (see V_EXTENDED_BYTEVAL in mmvec.h).
 *
 * The parse/round algorithm here computes in host `double`, matching the
 * reference model this was ported from: `double` has far more precision
 * than qf32's 24-bit mantissa + 4 extension bits need, so no accuracy is
 * lost by using it as the intermediate representation, and it sidesteps
 * having to hand-roll wide-integer arithmetic for the multiply/add/round
 * steps.
 */

#ifndef MMVEC_QFLOAT_H
#define MMVEC_QFLOAT_H

#include "mmvec.h"

/* Forward declaration to avoid a circular include with cpu.h */
typedef struct CPUArchState CPUHexagonState;

typedef enum float_type {
    QF32,
    QF16,
    SF,
    HF,
    EXTQF32,
    EXTQF16,
    BF,
} f_type;

typedef enum {
    RND_TO_NEAREST_EVEN,
    RND_TO_ZERO,
    RND_TOWARDS_NEG_INF,
    RND_TOWARDS_POS_INF,
    MAX_RND_MODES
} qfrnd_mode_enum_t;

/* Only RND_TO_NEAREST_EVEN is ever selected today; see mmvec_qfloat.c */
#define CVI_QFRND_MODE RND_TO_NEAREST_EVEN

#define BIAS_QF32 127
#define BIAS_EXTQF32 383
#define BIAS_QF16 15
#define BIAS_SF 127
#define BIAS_HF 15

#define E_MAX_QF16 16
#define E_MIN_QF16 -15
#define E_MAX_EXTQF16 E_MAX_QF16
#define E_MIN_EXTQF16 E_MIN_QF16
#define E_MAX_EXTQF32 256
#define E_MIN_EXTQF32 -255
#define E_MAX_SF 128
#define E_MIN_SF -126
#define E_MAX_HF 16
#define E_MIN_HF -14

#define sf_MANTBITS 23
#define hf_MANTBITS 10
#define bf_MANTBITS 7

#define EPSILON32 (1.0 / (double)(1u << 23))
#define UNITS32   (1.0 * (double)(1u << 23))
#define EPSILON16 (1.0 / (double)(1u << 10))
#define UNITS16   (1.0 * (double)(1u << 10))
#define EPSILONBF (1.0 / (double)(1 << bf_MANTBITS))
#define UNITSBF   (1.0 * (double)(1 << bf_MANTBITS))

#define QF32_BITMASK 0xFFFFFFFFULL
#define EXTQF32_BITMASK 0xFFFFFFFFFULL
#define QF16_BITMASK 0xFFFFULL
#define EXTQF16_BITMASK 0x3FFFFULL
#define EXT32_BITMASK 0x0F
#define EXT16_BITMASK 0x03

#define MAX_SIG_QF16 0x3FF
#define MAX_SIG_QF32 0x7FFFFF
#define MAX_BIASED_E_SF (E_MAX_SF + BIAS_SF)
#define MAX_BIASED_E_HF (E_MAX_HF + BIAS_HF)

/* extqf32 NaN/Inf sentinels: 32-bit qf32 value + 4-bit LREQ, packed */
#define extqf32_pos_nan          0X7FFFFF7FDULL
#define extqf32_neg_nan          0X8000007F0ULL
#define extqf32_pos_nan_inexact  0X7FFFFF7FCULL
#define extqf32_neg_nan_inexact  0X8000007F1ULL
#define extqf32_pos_inf          0x7FFFFF7F8ULL
#define extqf32_pos_inf_exact    0x7FFFFF7F8ULL
#define extqf32_pos_inf_inexact  0x7FFFFF7F9ULL
#define extqf32_neg_inf          0x8000007F5ULL
#define extqf32_neg_inf_exact    0x8000007F5ULL
#define extqf32_neg_inf_inexact  0x8000007F4ULL

/* extqf16 NaN/Inf sentinels: 16-bit qf16 value + 2-bit LR, packed */
#define extqf16_pos_nan 0x7FFF3
#define extqf16_neg_nan 0x801F0
#define extqf16_pos_inf 0x7FFF2
#define extqf16_neg_inf 0x801F1

/* ieee NaNs can have multiple representations; use a single canonical one */
#define ieee_pos_NaN_32 0x7FFFFFFF
#define ieee_pos_NaN_16 0x7FFF
#define ieee_pos_inf_32 0x7f800000
#define ieee_pos_inf_16 0x7C00
#define BF_POS_NAN 0x7FFF
#define BF_POS_INF 0x7f80

#define QF32_ILOG2_ZERO_EXP 0x80000000
#define QF32_ILOG2_NAN_EXP  0x7FFFFFFF
#define QF32_ILOG2_INF_EXP  0x7FFFFFFE
#define QF16_ILOG2_ZERO_EXP 0x8000
#define QF16_ILOG2_NAN_EXP  0x7FFF
#define QF16_ILOG2_INF_EXP  0x7FFE

#define INIT_UNFLOAT(U) \
    unfloat U = { .sign = 0, .exp = 0, .sig = 0, .inexact = 0, \
                  .inf = false, .nan = false, .zero = false };

/*
 * "Un-normalized float": the common intermediate representation used while
 * parsing/adding/multiplying/rounding qfloat and IEEE values.
 *   exp:     biased exponent
 *   sig:     un-normalized significand, as a real (host double)
 *   inexact: -1, 0, or +1 -- sticky/rounding-direction hint from a prior op
 *   sign:    literal sign bit for IEEE-parsed values; for qfloat-parsed
 *            values this is instead the "deferred negation" flag left by
 *            the extended-bits' R bit (see is_unfloat_neg()) -- not a
 *            literal sign, except after is_unfloat_neg() resolves it.
 */
typedef struct {
    int exp;
    double sig;
    int inexact;
    bool inf;
    bool nan;
    int sign;
    bool zero;
} unfloat;

typedef union {
    uint8_t raw;
    struct {
        uint8_t Q:1;
        uint8_t E:1;
        uint8_t R:1;
        uint8_t L:1;
    } bits;
    struct {
        uint8_t EQ:2;
        uint8_t LR:2;
    } bits16;
} LREQ_t;

typedef struct {
    uint32_t sig;
    uint32_t exp;
    LREQ_t LREQ;
} fixed_float_t;

typedef struct {
    double fractional;
    double integer;
} ulp_t;

unfloat parse_hf(int16_t in);
unfloat parse_sf(int32_t in);
unfloat parse_sf_daz(uint32_t in, bool daz_mode);
unfloat parse_extqf16(uint16_t in, uint8_t in_ext);
unfloat parse_extqf32(uint32_t in, uint8_t in_ext);

uint64_t rnd_sat_extqf32(unfloat u, qfrnd_mode_enum_t qfrnd_mode);
uint64_t rnd_sat_extqf16(unfloat u, qfrnd_mode_enum_t qfrnd_mode);

int32_t conv_sf_extqf32(uint32_t a, uint8_t a_ext, qfrnd_mode_enum_t qfrnd);
int16_t conv_hf_extqf32(uint32_t a, uint8_t a_ext, qfrnd_mode_enum_t qfrnd);
int16_t conv_hf_extqf16(uint16_t a, uint8_t a_ext, qfrnd_mode_enum_t qfrnd);
uint16_t conv_qf32_to_bf(uint32_t a, uint8_t a_ext, qfrnd_mode_enum_t qfrnd);

int get_unfloat_exp(unfloat A, unfloat B, int sub, int e_min);
int qf_vilog2(f_type type, uint32_t a, uint8_t a_ext);

uint64_t handle_infinity_nan_add(unfloat u, unfloat v, uint64_t pos_nan_val,
                                  uint64_t neg_nan_val, uint64_t pos_inf_val,
                                  uint64_t neg_inf_val);
uint64_t handle_infinity_nan_mpy(unfloat u, unfloat v, uint64_t pos_nan_val,
                                  uint64_t neg_nan_val, uint64_t pos_inf_val,
                                  uint64_t neg_inf_val);

bool is_unfloat_neg(unfloat u);
bool is_unfloat_zero(unfloat u);
bool is_double_neg(double f);
int signum(double val);

bool is_daz_mode(CPUHexagonState *env);
uint32_t get_usr_reg_fpsat_field(CPUHexagonState *env);

#endif
