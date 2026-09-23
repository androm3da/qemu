/*
 * RISC-V translation routines for the Xqccmi Extension.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define REQUIRE_XQCCMI(ctx) do {        \
    if (!ctx->cfg_ptr->ext_xqccmi) {    \
        return false;                   \
    }                                   \
} while (0)

static bool trans_qc_cm_ilut(DisasContext *ctx, arg_qc_cm_ilut *a)
{
    CPURISCVState *env = cpu_env(ctx->cs);
    uint64_t entry;
    uint64_t second;
    target_ulong addr;
    uint32_t dec;
    int first_len;
    int second_len;
    bool is_double;
    bool has_second = false;

    REQUIRE_XQCCMI(ctx);

    dec = extract32(env->qc_itdec, 3, 11);
    is_double = a->index < dec;
    addr = env->qc_itba + (is_double ? a->index * 8 :
                           dec * 8 + (a->index - dec) * 4);
    entry = cpu_ldl_code(env, addr);
    if (is_double) {
        entry |= (uint64_t)cpu_ldl_code(env, addr + 4) << 32;
    }

    first_len = insn_len(entry);
    if (first_len > (is_double ? 6 : 4) ||
        ilut_is_pc_referencing(entry, first_len)) {
        gen_exception_illegal(ctx);
        return true;
    }

    if (first_len == 2 && extract16(entry, 16, 16) != 1) {
        second = entry >> 16;
        has_second = true;
    } else if (first_len == 4 && is_double &&
               extract16(entry >> 32, 0, 16) != 1) {
        second = entry >> 32;
        has_second = true;
    }

    if (has_second) {
        second_len = insn_len(second);
        if (first_len + second_len > (is_double ? 8 : 4) ||
            ilut_is_pc_referencing(second, second_len)) {
            gen_exception_illegal(ctx);
            return true;
        }
    } else if (first_len == 6 &&
               (!is_double || extract16(entry >> 48, 0, 16) != 1)) {
        gen_exception_illegal(ctx);
        return true;
    }

    if (!decode_ilut_insn(ctx, entry)) {
        gen_exception_illegal(ctx);
        return true;
    }
    if (has_second && !decode_ilut_insn(ctx, second)) {
        gen_exception_illegal(ctx);
        return true;
    }

    ctx->cur_insn_len = 2;
    return true;
}
