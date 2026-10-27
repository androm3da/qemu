/*
 * Hexagon HMX (Matrix eXtensions) XFP arithmetic
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Hexagon HMX XFP arithmetic: generic width-carrying HexagonXfp
 * primitives (FP convert path).
 *
 * This is bit-exact arithmetic that matches the hardware, not something
 * that can be re-derived from the architecture documents, so it is
 * written as literally as possible rather than simplified.
 * hmx_xfp.c's own header comment documents one deliberate
 * "bug-for-bug" case (a dead overflow-flag variable in the generic
 * normalize step) that must stay unfixed; see that file.
 */

#ifndef HEXAGON_HMX_XFP_H
#define HEXAGON_HMX_XFP_H

struct HmxConfig;

/*
 * xfp status type.  The XFP_ST_* masks below address `val`, so the
 * bitfields must put `zero` in bit 0 whatever the host's bitfield order is.
 */
typedef union HexagonXfpStatus {
    struct {
#if HOST_BIG_ENDIAN
        uint32_t reserved:25;
        uint32_t in1_zero:1;
        uint32_t in0_zero:1;   /* product-side all-zero detection */
        uint32_t under:1;
        uint32_t negative:1;
        uint32_t inf:2;        /* 0 finite, 1 +Inf, 2 -Inf, 3 NaN */
        uint32_t zero:1;
#else
        uint32_t zero:1;
        uint32_t inf:2;        /* 0 finite, 1 +Inf, 2 -Inf, 3 NaN */
        uint32_t negative:1;
        uint32_t under:1;
        uint32_t in0_zero:1;   /* product-side all-zero detection */
        uint32_t in1_zero:1;
        uint32_t reserved:25;
#endif
    };
    uint32_t val;
} HexagonXfpStatus;

/* xfp data type */
typedef struct HexagonXfp {
    HexagonXfpStatus status;
    int32_t exp;        /* unbiased exponent */
    int64_t sig;        /* signed significand, Q INT.FRAC */
    uint8_t bits_int;   /* INT width */
    uint8_t bits_frac;  /* FRAC width */
    uint8_t bits_exp;   /* EXP width */
    uint8_t lza;
} HexagonXfp;

/*
 * USR FP control bits.
 * inf_nan_enable = USR[20], nan_propagate = USR[21].
 */
typedef struct HexagonXfpUsr {
    uint32_t inf_nan_enable;
    uint32_t nan_propagate;
} HexagonXfpUsr;

/* cvt Rs fields consumed by the XFP-to-FP convert path. */
typedef struct HexagonXfpCvtRs {
    uint32_t relu;
    uint32_t fp_rnd;
    uint32_t fp_maxnorm;
    uint32_t fb_dst;       /* 0=none, 1=outbias, 2=scale */
    uint32_t fb_limit;
    uint32_t is_bf16;
} HexagonXfpCvtRs;

/* Decoded FP bias-register fields. */
typedef struct HexagonXfpBias {
    uint32_t scale;
    uint32_t out_bias;
    uint32_t scale_extra;
    uint32_t out_bias_extra;
    uint32_t shape;
    uint32_t negate;
    uint32_t acc_bias_extra;
    uint32_t acc_bias;
} HexagonXfpBias;

#define HEXAGON_XFP_MIN 0
#define HEXAGON_XFP_MAX 1

/* True-zero cell. */
void hexagon_xfp_zero(const struct HmxConfig *hmx_cfg, HexagonXfp *out);

/* fp -> xfp. */
HexagonXfp hexagon_xfp_from_fp(const struct HmxConfig *hmx_cfg,
                               HexagonXfpUsr usr, uint32_t in,
                               uint32_t frac_in, uint32_t exp_in,
                               uint32_t int_out, uint32_t frac_out,
                               uint32_t exp_out, uint32_t normalize);

/* xfp -> fp output. */
uint32_t hexagon_xfp_to_fp(const struct HmxConfig *hmx_cfg, int is_f8,
                           HexagonXfpUsr usr, HexagonXfp in,
                           uint32_t fp_frac, uint32_t fp_exp,
                           HexagonXfpCvtRs rs);

/* convert-normalize. */
HexagonXfp hexagon_xfp_cvt_normalize(const struct HmxConfig *hmx_cfg,
                                     HexagonXfpUsr usr, HexagonXfp in,
                                     uint32_t int_out, uint32_t frac_out,
                                     int32_t exp_out);

/* add. */
HexagonXfp hexagon_xfp_add(const struct HmxConfig *hmx_cfg,
                           HexagonXfpUsr usr, HexagonXfp in_a, HexagonXfp in_b);

/* multiply. */
HexagonXfp hexagon_xfp_mult(const struct HmxConfig *hmx_cfg,
                            HexagonXfpUsr usr, HexagonXfp in_a,
                            HexagonXfp in_b, uint32_t exp_out);

/* compare. */
HexagonXfp hexagon_xfp_cmp(const struct HmxConfig *hmx_cfg,
                           HexagonXfpUsr usr, HexagonXfp a, HexagonXfp b,
                           int32_t min_max);

/* 20-bit convert result split/combine. */
static inline uint16_t hexagon_xfp_cvt_out_lo(uint32_t in)
{
    return (in >> 0) & 0xFFF;
}
static inline uint16_t hexagon_xfp_cvt_out_hi(uint32_t in)
{
    return (in >> 8) & 0xFF0;
}
static inline uint32_t hexagon_xfp_cvt_combine_feedback(uint16_t hi,
                                                        uint16_t lo)
{
    uint32_t result = (uint32_t)(hi & 0xFF0) << 8;
    return result | ((uint32_t)lo & 0xFFF);
}

#endif /* HEXAGON_HMX_XFP_H */
