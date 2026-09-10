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

#include "qemu/osdep.h"
#include "cpu.h"
#include "reg_fields.h"
#include "mmvec_qfloat.h"
#include <math.h>

typedef union {
    uint32_t raw;
    struct {
        uint32_t mant:sf_MANTBITS;
        uint32_t exp:8;
        uint32_t sign:1;
    } x;
} sf_t;

typedef union {
    uint16_t raw;
    struct {
        uint16_t mant:hf_MANTBITS;
        uint16_t exp:5;
        uint16_t sign:1;
    } x;
} hf_t;

typedef struct {
    double even_pos_threshold;
    double odd_neg_threshold;
    bool rnd_to_pos_neg;
    uint32_t ovf_val32;
    uint32_t undf_val32;
    uint16_t ovf_val16;
    uint16_t undf_val16;
    sf_t ieee_pos_overflow;
    sf_t ieee_neg_overflow;
    hf_t ieee_hf_pos_overflow;
    hf_t ieee_hf_neg_overflow;
} qfrnd_mode_t;

/* Rounding-mode parameter table; see get_inc()/rnd_sat_extqf_sig() below */
#define SF_OVF(s, e, m) { .x = { .sign = (s), .exp = (e), .mant = (m) } }
#define HF_OVF(s, e, m) { .x = { .sign = (s), .exp = (e), .mant = (m) } }

static const qfrnd_mode_t qfrnd_modes[MAX_RND_MODES] = {
    /* Round to nearest even */
    { .even_pos_threshold = 0.75,
      .odd_neg_threshold  = 0.5,
      .rnd_to_pos_neg = false,
      .ovf_val32 = MAX_SIG_QF32, .undf_val32 = 0,
      .ovf_val16 = MAX_SIG_QF16, .undf_val16 = 0,
      .ieee_pos_overflow = SF_OVF(0, MAX_BIASED_E_SF, 0x0),
      .ieee_neg_overflow = SF_OVF(1, MAX_BIASED_E_SF, 0x0),
      .ieee_hf_pos_overflow = HF_OVF(0, MAX_BIASED_E_HF, 0x0),
      .ieee_hf_neg_overflow = HF_OVF(1, MAX_BIASED_E_HF, 0x0),
    },
    /* Round towards zero */
    { .even_pos_threshold = 1.0,
      .odd_neg_threshold  = 0.25,
      .rnd_to_pos_neg = true,
      .ovf_val32 = MAX_SIG_QF32, .undf_val32 = 0,
      .ovf_val16 = MAX_SIG_QF16, .undf_val16 = 0,
      .ieee_pos_overflow = SF_OVF(0, MAX_BIASED_E_SF - 1, MAX_SIG_QF32),
      .ieee_neg_overflow = SF_OVF(1, MAX_BIASED_E_SF - 1, MAX_SIG_QF32),
      .ieee_hf_pos_overflow = HF_OVF(0, MAX_BIASED_E_HF - 1, MAX_SIG_QF16),
      .ieee_hf_neg_overflow = HF_OVF(1, MAX_BIASED_E_HF - 1, MAX_SIG_QF16),
    },
    /* Round towards negative infinity */
    { .even_pos_threshold = 1.0,
      .odd_neg_threshold  = 1.0,
      .rnd_to_pos_neg = true,
      .ovf_val32 = MAX_SIG_QF32, .undf_val32 = 0,
      .ovf_val16 = MAX_SIG_QF16, .undf_val16 = 0,
      .ieee_pos_overflow = SF_OVF(0, MAX_BIASED_E_SF - 1, MAX_SIG_QF32),
      .ieee_neg_overflow = SF_OVF(1, MAX_BIASED_E_SF, 0x0),
      .ieee_hf_pos_overflow = HF_OVF(0, MAX_BIASED_E_HF - 1, MAX_SIG_QF16),
      .ieee_hf_neg_overflow = HF_OVF(1, MAX_BIASED_E_HF, 0x0),
    },
    /* Round towards positive infinity */
    { .even_pos_threshold = 0.25,
      .odd_neg_threshold  = 0.25,
      .rnd_to_pos_neg = true,
      .ovf_val32 = MAX_SIG_QF32, .undf_val32 = 0,
      .ovf_val16 = MAX_SIG_QF16, .undf_val16 = 0,
      .ieee_pos_overflow = SF_OVF(0, MAX_BIASED_E_SF, 0x0),
      .ieee_neg_overflow = SF_OVF(1, MAX_BIASED_E_SF - 1, MAX_SIG_QF32),
      .ieee_hf_pos_overflow = HF_OVF(0, MAX_BIASED_E_HF, 0x0),
      .ieee_hf_neg_overflow = HF_OVF(1, MAX_BIASED_E_HF - 1, MAX_SIG_QF16),
    },
};

#undef SF_OVF
#undef HF_OVF

/*
 *****************************************************************************
 * Common indicators
 *****************************************************************************
 */

bool is_double_neg(double f)
{
    return signbit(f) != 0;
}

static bool is_double_pos(double f)
{
    return signbit(f) == 0;
}

bool is_unfloat_neg(unfloat u)
{
    return is_double_neg(u.sig) ^ u.sign;
}

int signum(double val)
{
    return (val > 0) - (val < 0);
}

bool is_unfloat_zero(unfloat u)
{
    return fabs(u.sig) == 0.0;
}

/* One's complement the LRQ bits, but leave E (exponent extension) alone */
static LREQ_t negate_LREQ(LREQ_t LREQ)
{
    LREQ_t neg_LREQ;

    neg_LREQ.raw = ~LREQ.raw & 0xf;
    neg_LREQ.bits.E = LREQ.bits.E;
    return neg_LREQ;
}

static bool is_sf_infinity(unfloat u)
{
    return u.exp >= E_MAX_SF && fabs(u.sig) == 1.0;
}

static bool is_sf_nan(unfloat u)
{
    return u.exp >= E_MAX_SF && fabs(u.sig) != 1.0;
}

static bool is_hf_infinity(unfloat u)
{
    return u.exp >= E_MAX_HF && fabs(u.sig) == 1.0;
}

static bool is_hf_nan(unfloat u)
{
    return u.exp >= E_MAX_HF && fabs(u.sig) != 1.0;
}

static bool is_extqf32_nan(uint64_t in)
{
    return in == extqf32_pos_nan || in == extqf32_neg_nan ||
           in == extqf32_pos_nan_inexact || in == extqf32_neg_nan_inexact;
}

static bool is_extqf32_inf(uint64_t in)
{
    return in == extqf32_pos_inf_exact || in == extqf32_pos_inf_inexact ||
           in == extqf32_neg_inf_exact || in == extqf32_neg_inf_inexact;
}

static bool is_extqf16_nan(uint32_t in)
{
    return in == extqf16_pos_nan || in == extqf16_neg_nan;
}

static bool is_extqf16_inf(uint32_t in)
{
    return in == extqf16_pos_inf || in == extqf16_neg_inf;
}

/*
 *****************************************************************************
 * NaN/Inf input behavior
 *****************************************************************************
 */

uint64_t handle_infinity_nan_add(unfloat u, unfloat v, uint64_t pos_nan_val,
                                  uint64_t neg_nan_val, uint64_t pos_inf_val,
                                  uint64_t neg_inf_val)
{
    bool u_neg = is_unfloat_neg(u);
    bool v_neg = is_unfloat_neg(v);

    if (u.nan || v.nan) {
        /* +NaN has the highest priority */
        if ((!u_neg && u.nan) || (!v_neg && v.nan)) {
            return pos_nan_val;
        }
        return neg_nan_val;
    }

    /* Infinities of opposing sign added together produce a NaN */
    if (u.inf && v.inf && (u_neg ^ v_neg)) {
        return pos_nan_val;
    }
    if ((u_neg && u.inf) || (v_neg && v.inf)) {
        return neg_inf_val;
    }
    return pos_inf_val;
}

uint64_t handle_infinity_nan_mpy(unfloat u, unfloat v, uint64_t pos_nan_val,
                                  uint64_t neg_nan_val, uint64_t pos_inf_val,
                                  uint64_t neg_inf_val)
{
    bool u_neg = is_unfloat_neg(u);
    bool v_neg = is_unfloat_neg(v);

    if (u.nan || v.nan) {
        return (u_neg ^ v_neg) ? neg_nan_val : pos_nan_val;
    }
    /* Implicitly, one of the operands is infinite: 0 * inf is a NaN */
    if (u.zero || v.zero) {
        return (u_neg ^ v_neg) ? neg_nan_val : pos_nan_val;
    }
    return (u_neg ^ v_neg) ? neg_inf_val : pos_inf_val;
}

/*
 *****************************************************************************
 * Parse SF/HF/QF16/QF32 into an unfloat
 *****************************************************************************
 */

static double extqf32_pre_rounding(double orig32_mantissa, LREQ_t LREQ)
{
    return round(orig32_mantissa +
                 LREQ.bits.R * 0.5 + LREQ.bits.Q * 0.25 + 0.125);
}

static double extqf16_pre_rounding(double orig16_mantissa, LREQ_t LREQ)
{
    return round(orig16_mantissa + LREQ.bits.R * 0.5 + 0.25);
}

unfloat parse_hf(int16_t in)
{
    INIT_UNFLOAT(out)
    hf_t hf = { .raw = (uint16_t)in };
    uint16_t sig = hf.x.mant;

    out.exp = hf.x.exp;
    if (out.exp > 0) {
        sig |= (1 << 10);
    }
    out.exp -= BIAS_HF;
    if (out.exp < E_MIN_HF) {
        out.exp = E_MIN_HF;
    }

    out.sign = hf.x.sign;
    out.sig = (double)sig * EPSILON16;
    out.inf = is_hf_infinity(out);
    out.nan = is_hf_nan(out);
    out.zero = is_unfloat_zero(out);
    return out;
}

unfloat parse_sf(int32_t in)
{
    INIT_UNFLOAT(out)
    sf_t sf = { .raw = (uint32_t)in };
    uint32_t sig = sf.x.mant;

    out.exp = sf.x.exp;
    if (out.exp > 0) {
        sig |= (1 << 23);
    }
    out.exp -= BIAS_SF;
    if (out.exp < E_MIN_SF) {
        out.exp = E_MIN_SF;
    }

    out.sign = sf.x.sign;
    out.sig = (double)sig * EPSILON32;
    out.inf = is_sf_infinity(out);
    out.nan = is_sf_nan(out);
    out.zero = is_unfloat_zero(out);
    return out;
}

unfloat parse_sf_daz(uint32_t in, bool daz_mode)
{
    unfloat a = parse_sf(in);

    if (daz_mode && a.exp == E_MIN_SF && fabs(a.sig) < 1.0) {
        a.zero = true;
        a.sig = 0.0;
    }
    return a;
}

unfloat parse_extqf16(uint16_t in, uint8_t in_ext)
{
    INIT_UNFLOAT(out)
    LREQ_t LREQ = { .bits16.LR = in_ext & 0x3 };
    int16_t orig16 = (int16_t)in;
    /* Add the L bit at the LSB to produce an 11-bit mantissa */
    int16_t orig16_mantissa = ((orig16 >> 4) & ~0x1) | LREQ.bits.L;
    double mantissa = extqf16_pre_rounding((double)orig16_mantissa, LREQ);

    out.exp = ((uint8_t)(orig16 & 0x1f)) - BIAS_QF16;
    out.sig = mantissa * EPSILON16;
    out.sign = LREQ.bits.R;
    if (out.sign) {
        out.sig = -out.sig;
    }
    out.inf = is_extqf16_inf(((uint32_t)in << 4) | in_ext);
    out.nan = is_extqf16_nan(((uint32_t)in << 4) | in_ext);
    out.zero = is_unfloat_zero(out);
    return out;
}

unfloat parse_extqf32(uint32_t in, uint8_t in_ext)
{
    INIT_UNFLOAT(out)
    LREQ_t LREQ = { .raw = in_ext & 0xf };
    int32_t orig32 = (int32_t)in;
    uint16_t orig32_exponent = orig32 & 0xff;
    /* Add the L bit at the LSB to produce a 24-bit mantissa */
    int32_t orig32_mantissa = ((orig32 >> 7) & ~0x1) | LREQ.bits.L;
    double mantissa = extqf32_pre_rounding((double)orig32_mantissa, LREQ);
    /* Add the E bit at the MSB of a 9-bit exponent */
    int16_t exponent = (int16_t)(orig32_exponent | (LREQ.bits.E ? 0x100 : 0x0));

    /* First step, sub 128 to get to IEEE-like; then sub 255 for qf32 bias */
    out.exp = ((exponent - 128) & 0x1ff) - 255;
    out.sig = mantissa * EPSILON32;
    out.sign = LREQ.bits.R;
    if (out.sign) {
        out.sig = -out.sig;
    }
    out.inexact = LREQ.bits.R != LREQ.bits.Q;

    out.inf = is_extqf32_inf(((uint64_t)in << 4) | in_ext);
    out.nan = is_extqf32_nan(((uint64_t)in << 4) | in_ext);
    out.zero = is_unfloat_zero(out);
    return out;
}

/*
 *****************************************************************************
 * Rounding
 *****************************************************************************
 */

static double qmod(double sig_s, double modnum)
{
    double r2 = floor(sig_s / modnum) * modnum;
    return sig_s - r2;
}

static bool get_inc(const qfrnd_mode_t *qfrnd, ulp_t ulp)
{
    double cond, threshold;

    if (qfrnd->rnd_to_pos_neg) {
        cond = ulp.integer < 0.0;
    } else {
        cond = qmod(ulp.integer, 2.0);
    }
    threshold = cond ? qfrnd->odd_neg_threshold : qfrnd->even_pos_threshold;
    return ulp.fractional >= threshold;
}

static double floor_to_frac(double v, double mod)
{
    double integral;
    double fractional = modf(v, &integral);

    if (is_double_pos(integral)) {
        if (fractional >= mod) {
            integral += mod;
        }
    } else {
        if (fabs(fractional) <= mod && fractional != 0.0) {
            integral -= mod;
        } else if (fabs(fractional) > mod) {
            integral -= 1;
        }
    }
    return integral;
}

static ulp_t ulp_modf(double sig_scaled_in_ulp, int inexact)
{
    ulp_t ulp;
    double floorhalf = floor_to_frac(sig_scaled_in_ulp, 0.5);
    double modhalf = sig_scaled_in_ulp - floorhalf;
    double ulp_rx = floorhalf
        - 0.5 * (modhalf == 0) * (inexact < 0)
        + 0.25 * (modhalf != 0 || inexact != 0);

    ulp.fractional = qmod(ulp_rx, 1.0);
    ulp.integer = floor(ulp_rx);
    return ulp;
}

/* Round+pack an unfloat into a fixed-point qf32/qf16 sig+exp+LREQ */
static fixed_float_t rnd_sat_extqf_sig(unfloat u, qfrnd_mode_enum_t qfrnd_mode,
                                        f_type ft, int e_min, int e_max,
                                        int bias, double units)
{
    const qfrnd_mode_t *qfrnd = &qfrnd_modes[qfrnd_mode];
    fixed_float_t f;
    LREQ_t LREQ = { .raw = 0 };
    double scale = 1.0;
    bool prod_ovf = false;
    bool exp_ovf = false;
    bool exp_undf = false;
    uint32_t sig_32 = 0;
    int exp = u.exp;
    double sig = u.sig;
    int inexact = u.inexact;

    if (fabs(sig) >= 2.0) {
        if (!((sig == 2.0 && inexact < 0) || (sig == -2.0 && inexact >= 0))) {
            prod_ovf = true;
        }
    }
    if (prod_ovf && exp < e_max) {
        scale = 0.5;
        exp += 1;
    }

    double sig_scaled = sig * scale;

    if (exp > e_max ||
        (exp == e_max &&
         (sig_scaled > 1.0 || (sig_scaled == 1.0 && inexact >= 0) ||
          sig_scaled < -1.0 || (sig_scaled == -1.0 && inexact < 0)))) {
        exp_ovf = true;
    } else if (exp < e_min) {
        exp_undf = true;
    }

    if (!exp_ovf && !exp_undf) {
        double sig_scaled_in_ulp = sig_scaled * units;
        ulp_t ulp = ulp_modf(sig_scaled_in_ulp, inexact);
        bool inc = get_inc(qfrnd, ulp);
        double ulp_RQ1;

        if (ft == EXTQF32) {
            ulp_RQ1 = ulp.integer + inc * 0.5 +
                      (inc ^ (ulp.fractional != 0.0)) * 0.25 + 0.125;
        } else {
            ulp_RQ1 = ulp.integer + inc * 0.5 + 0.25;
        }
        if (u.sign) {
            ulp_RQ1 = -ulp_RQ1;
        }

        double ulp2 = floor(ulp_RQ1) - qmod(floor(ulp_RQ1), 2.0);
        double LRQ = ulp_RQ1 - ulp2 - 0.125;

        LREQ.bits.L = (uint32_t)floor(LRQ);
        LREQ.bits.R = ((uint32_t)floor(2.0 * LRQ)) % 2;
        LREQ.bits.Q = ((uint32_t)floor(4.0 * LRQ)) % 2;
        sig_32 = ((int32_t)ulp2) >> 1;

        /*
         * The deferred-negation convention only works cleanly for the
         * symmetric rounding modes; undo its over-application here for
         * the directed (towards +/-inf) modes.
         */
        if (u.sign && LREQ.bits.R != LREQ.bits.Q &&
            (qfrnd_mode == RND_TOWARDS_NEG_INF ||
             qfrnd_mode == RND_TOWARDS_POS_INF)) {
            LREQ.bits.R = !LREQ.bits.R;
            LREQ.bits.Q = !LREQ.bits.Q;
        }
    } else if (exp_ovf) {
        exp = e_max;
        if (qfrnd_mode == RND_TO_ZERO) {
            exp--;
        }
        if (qfrnd_mode == RND_TOWARDS_NEG_INF && !is_unfloat_neg(u)) {
            exp--;
        }
        if (qfrnd_mode == RND_TOWARDS_POS_INF && is_unfloat_neg(u)) {
            exp--;
        }
        sig_32 = (ft == EXTQF32) ? qfrnd->ovf_val32 : qfrnd->ovf_val16;
        LREQ.bits.L = 0x1;
        LREQ.bits.Q = 0x1;
        if (is_unfloat_neg(u)) {
            sig_32 = ~sig_32;
            LREQ = negate_LREQ(LREQ);
        }
    } else {
        exp = e_min;
        sig_32 = (ft == EXTQF32) ? qfrnd->undf_val32 : qfrnd->undf_val16;
        if (qfrnd_mode == RND_TOWARDS_POS_INF) {
            LREQ.bits.R = 0x1;
            LREQ.bits.Q = 0x0;
        } else if (qfrnd_mode == RND_TOWARDS_NEG_INF) {
            LREQ.bits.R = 0x0;
            LREQ.bits.Q = 0x1;
        } else {
            LREQ.bits.Q = 1;
        }
        if (is_unfloat_neg(u)) {
            LREQ = negate_LREQ(LREQ);
            sig_32 = ~sig_32;
        }
    }

    exp += bias;
    /* qf32's exponent is 9 bits, including the E bit; unused for qf16 */
    LREQ.bits.E = (exp >> 8) & 0x1;

    f.exp = exp;
    f.sig = sig_32;
    f.LREQ = LREQ;
    return f;
}

uint64_t rnd_sat_extqf32(unfloat u, qfrnd_mode_enum_t qfrnd_mode)
{
    fixed_float_t f = rnd_sat_extqf_sig(u, qfrnd_mode, EXTQF32, E_MIN_EXTQF32,
                                         E_MAX_EXTQF32, BIAS_EXTQF32, UNITS32);
    uint64_t result = ((uint64_t)f.sig << 8 | (f.exp & 0xff)) & QF32_BITMASK;

    return ((result << 4) | (f.LREQ.raw & 0xf)) & EXTQF32_BITMASK;
}

uint64_t rnd_sat_extqf16(unfloat u, qfrnd_mode_enum_t qfrnd_mode)
{
    fixed_float_t f = rnd_sat_extqf_sig(u, qfrnd_mode, EXTQF16, E_MIN_QF16,
                                         E_MAX_QF16, BIAS_QF16, UNITS16);
    uint64_t result = ((uint64_t)f.sig << 5 | (f.exp & 0x1f)) & QF16_BITMASK;

    return ((result << 2) | (f.LREQ.bits16.LR & 0x3)) & EXTQF16_BITMASK;
}

static double qf_ilogb(double sig)
{
    if (is_double_neg(sig)) {
        return ceil(log2(-sig)) - 1;
    }
    return floor(log2(sig));
}

int get_unfloat_exp(unfloat A, unfloat B, int sub, int e_min)
{
    int result;

    if (A.exp > B.exp) {
        result = A.exp + (int)qf_ilogb(A.sig) - sub;
        if (result < B.exp || A.sig == 0.0) {
            result = B.exp;
        }
    } else {
        result = B.exp + (int)qf_ilogb(B.sig) - sub;
        if (result < A.exp || B.sig == 0.0) {
            result = A.exp;
        }
    }
    return result;
}

/*
 *****************************************************************************
 * Convert QF16/QF32 to HF/SF/BF
 *****************************************************************************
 */

static uint32_t negate_by_type(f_type ft, uint32_t res)
{
    if (ft == SF) {
        return (res | 0x80000000) & 0xffffffff;
    }
    /* Covers HF and BF */
    return (res | 0x8000) & 0xffff;
}

static uint32_t conv_unfloat_to_ieee(unfloat u, f_type ft,
                                      qfrnd_mode_enum_t qfrnd, double units,
                                      double epsilon, int max_exp, int sig_bits)
{
    double sig = fabs(u.sig);
    int minlg = -(max_exp - 2) - u.exp;
    int lgslg0, exp;
    ulp_t ulp;

    if (u.nan) {
        uint32_t result = (ft == SF) ? ieee_pos_NaN_32 :
                           (ft == HF) ? ieee_pos_NaN_16 : BF_POS_NAN;
        return is_unfloat_neg(u) ? negate_by_type(ft, result) : result;
    }
    if (u.inf) {
        uint32_t result = (ft == SF) ? ieee_pos_inf_32 :
                           (ft == HF) ? ieee_pos_inf_16 : BF_POS_INF;
        return is_unfloat_neg(u) ? negate_by_type(ft, result) : result;
    }

    /*
     * The deferred-negation convention (see unfloat's `sign` field) only
     * works for symmetric rounding modes; un-defer it up front for the
     * directed (towards +/-inf) modes.
     */
    if ((qfrnd == RND_TOWARDS_POS_INF || qfrnd == RND_TOWARDS_NEG_INF) &&
        u.sign) {
        u.sign = 0;
        u.sig = -u.sig;
        u.inexact = -u.inexact;
    }

    if (sig < pow(2, minlg)) {
        lgslg0 = minlg;
    } else {
        lgslg0 = (int)floor(log2(sig));
    }
    exp = u.exp + lgslg0;
    sig = u.sig * pow(2, -lgslg0);

    if (sig != 0.0 &&
        (exp > max_exp || (exp == max_exp && (sig >= 1.0 || sig < -1.0)))) {
        const qfrnd_mode_t *m = &qfrnd_modes[qfrnd];
        uint32_t result = (ft == SF) ? m->ieee_pos_overflow.raw :
                           (ft == HF) ? m->ieee_hf_pos_overflow.raw :
                           (m->ieee_pos_overflow.raw >> 16);
        return is_unfloat_neg(u) ? negate_by_type(ft, result) : result;
    }

    double sig_scaled_in_ulp = sig * units;
    ulp = ulp_modf(sig_scaled_in_ulp, u.inexact);
    ulp.integer += get_inc(&qfrnd_modes[qfrnd], ulp);

    if (exp > max_exp - 1) {
        exp = max_exp;
        ulp.integer = pow(2, sig_bits + (ulp.integer == 0)) -
                      (ulp.integer == 0);
    }
    sig = fabs(ulp.integer) * epsilon;
    double exp_frac = exp + fabs(sig) - 1;
    uint32_t res = (uint32_t)((exp_frac + max_exp - 1) * units);

    return is_unfloat_neg(u) ? negate_by_type(ft, res) : res;
}

int32_t conv_sf_extqf32(uint32_t a, uint8_t a_ext, qfrnd_mode_enum_t qfrnd)
{
    unfloat u = parse_extqf32(a, a_ext);
    return conv_unfloat_to_ieee(u, SF, qfrnd, UNITS32, EPSILON32, E_MAX_SF,
                                 sf_MANTBITS);
}

int16_t conv_hf_extqf32(uint32_t a, uint8_t a_ext, qfrnd_mode_enum_t qfrnd)
{
    unfloat u = parse_extqf32(a, a_ext);
    return conv_unfloat_to_ieee(u, HF, qfrnd, UNITS16, EPSILON16, E_MAX_HF,
                                 hf_MANTBITS);
}

int16_t conv_hf_extqf16(uint16_t a, uint8_t a_ext, qfrnd_mode_enum_t qfrnd)
{
    unfloat u = parse_extqf16(a, a_ext);
    return conv_unfloat_to_ieee(u, HF, qfrnd, UNITS16, EPSILON16, E_MAX_HF,
                                 hf_MANTBITS);
}

uint16_t conv_qf32_to_bf(uint32_t a, uint8_t a_ext, qfrnd_mode_enum_t qfrnd)
{
    unfloat u = parse_extqf32(a, a_ext);
    return conv_unfloat_to_ieee(u, BF, qfrnd, UNITSBF, EPSILONBF, E_MAX_SF,
                                 bf_MANTBITS);
}

int qf_vilog2(f_type type, uint32_t a, uint8_t a_ext)
{
    unfloat u = { 0 };

    switch (type) {
    case EXTQF32:
        u = parse_extqf32(a, a_ext);
        break;
    case EXTQF16:
        u = parse_extqf16(a, a_ext);
        break;
    case SF:
        u = parse_sf(a);
        break;
    case HF:
        u = parse_hf(a);
        break;
    default:
        g_assert_not_reached();
    }

    double af = ldexp(u.sig, u.exp);
    if (type == EXTQF32 || type == SF) {
        if (u.zero) {
            return QF32_ILOG2_ZERO_EXP;
        } else if (u.nan) {
            return QF32_ILOG2_NAN_EXP;
        } else if (u.inf) {
            return QF32_ILOG2_INF_EXP;
        }
    } else {
        if (u.zero) {
            return QF16_ILOG2_ZERO_EXP;
        } else if (u.nan) {
            return QF16_ILOG2_NAN_EXP;
        } else if (u.inf) {
            return QF16_ILOG2_INF_EXP;
        }
    }
    return ilogb(fabs(af));
}

/*
 *****************************************************************************
 * USR field accessors
 *****************************************************************************
 */

static uint32_t get_usr_field(CPUHexagonState *env, int field)
{
    return extract32(env->gpr[HEX_REG_USR], reg_field_info[field].offset,
                      reg_field_info[field].width);
}

bool is_daz_mode(CPUHexagonState *env)
{
    HexagonCPU *cpu = env_archcpu(env);

    return cpu->cfg.hex_def->hex_version >= HEX_VER_V81 &&
           get_usr_field(env, USR_FPDAZ);
}

uint32_t get_usr_reg_fpsat_field(CPUHexagonState *env)
{
    return get_usr_field(env, USR_FPSAT);
}
