/*
 * QTest testcase for Qualcomm SMEM device
 *
 * Verifies that qcom_smem_fill_region() produces the correct bootloader-
 * initialized SMEM layout by comparing it against the hardware-captured
 * reference binary tests/data/hexagon/sa8775p-smem.bin.
 *
 * The test does two things:
 *   1. Confirm the machine writes the reference binary to guest memory.
 *   2. Confirm the algorithm output matches the reference for every
 *      SBL-written structure (header, partition table, partition headers,
 *      and the items the algorithm explicitly allocates).
 *
 * Items written by the UEFI ABL after the SBL hands off (build strings,
 * platform tables, etc.) are excluded from the algorithm comparison
 * because they contain device-specific runtime data.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "hw/misc/qcom-smem.h"

#define SMEM_BASE_ADDR    0x90900000ULL
#define SMEM_SIZE         0x200000    /* 2MB */
#define SMEM_REF_BIN_PATH "tests/data/hexagon/sa8775p-smem.bin"

/*
 * Compare @len bytes: actual[act_off..] vs expected[exp_off..].
 */
static void check_range(const char *label,
                        const uint8_t *actual, size_t act_off,
                        const uint8_t *expected, size_t exp_off,
                        size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        if (actual[act_off + i] != expected[exp_off + i]) {
            size_t aw = (act_off + i) & ~3UL;
            size_t ew = (exp_off + i) & ~3UL;
            uint32_t av, ev;

            memcpy(&av, actual   + aw, sizeof(av));
            memcpy(&ev, expected + ew, sizeof(ev));
            g_test_message("%s: first mismatch at byte +%zu "
                           "inc@0x%zx=0x%08x algo@0x%zx=0x%08x",
                           label, i, aw, av, ew, ev);
            g_assert_cmphex(actual[act_off + i], ==, expected[exp_off + i]);
            return;
        }
    }
}

static void test_smem_matches_inc(void)
{
    const QcomSMEMTargetConfig *cfg;
    QTestState *qts;
    uint8_t *actual;
    uint8_t *reference;
    uint8_t *expected;
    gsize ref_size;
    GError *err = NULL;
    uint32_t i;

    cfg = qcom_smem_find_target_config("sa8775p");
    g_assert_nonnull(cfg);
    g_assert_cmpuint(cfg->smem_size, ==, SMEM_SIZE);

    /* Load the hardware-captured reference binary */
    g_assert_true(g_file_get_contents(SMEM_REF_BIN_PATH,
                                      (gchar **)&reference, &ref_size,
                                      &err));
    g_assert_no_error(err);
    g_assert_cmpuint(ref_size, ==, SMEM_SIZE);

    /* Boot the machine — writes the reference binary to SMEM_BASE_ADDR */
    qts = qtest_init("-M SA8775P_CDSP0");

    actual = g_malloc(SMEM_SIZE);
    qtest_memread(qts, SMEM_BASE_ADDR, actual, SMEM_SIZE);

    /* 0. Verify the machine wrote exactly the reference binary */
    check_range("machine vs reference",
                actual, 0, reference, 0, SMEM_SIZE);

    /* Generate the algorithm's expected layout */
    expected = g_malloc0(SMEM_SIZE);
    qcom_smem_fill_region(expected, SMEM_SIZE, cfg);

    /*
     * 1. First 4KB: contains all the SMEMHeader fields we set
     *    (version[], initialized, free_offset).  The SMEMHeader struct
     *    itself extends further but overlaps partition space; the SBL
     *    owns only this first page.
     */
    check_range("SMEMHeader page",
                actual, 0, expected, 0, 0x1000);

    /*
     * 2. Partition table: SMEMPtable header + all entries + SMEMInfo.
     */
    {
        size_t ptable_off = SMEM_SIZE - 4096;
        size_t ptable_len = sizeof(SMEMPtable) +
                            cfg->num_partitions * sizeof(SMEMPtableEntry) +
                            sizeof(SMEMInfo);
        check_range("partition table",
                    actual, ptable_off, expected, ptable_off, ptable_len);
    }

    /*
     * 3. Partition header invariant fields.
     *    Skip self-describing entries (host0 == host1 == 0xffff).
     */
    for (i = 0; i < cfg->num_partitions; i++) {
        const QcomSMEMPartitionConfig *p = &cfg->partitions[i];
        const SMEMPartitionHeader *act_hdr;
        const SMEMPartitionHeader *exp_hdr;
        char label[64];
        size_t f;

        if (p->host0 == 0xffff && p->host1 == 0xffff) {
            continue;
        }
        act_hdr = (const SMEMPartitionHeader *)(actual   + p->offset);
        exp_hdr = (const SMEMPartitionHeader *)(expected + p->offset);

        /* Fields set once by SBL and never changed */
        static const struct { size_t off; size_t sz; const char *name; }
        invariant_fields[] = {
            { offsetof(SMEMPartitionHeader, magic),
              sizeof(act_hdr->magic),       "magic"        },
            { offsetof(SMEMPartitionHeader, host0),
              sizeof(act_hdr->host0),       "host0"        },
            { offsetof(SMEMPartitionHeader, host1),
              sizeof(act_hdr->host1),       "host1"        },
            { offsetof(SMEMPartitionHeader, size),
              sizeof(act_hdr->size),        "size"         },
            { offsetof(SMEMPartitionHeader, offset_free_cached),
              sizeof(act_hdr->offset_free_cached), "free_cached" },
        };

        for (f = 0; f < ARRAY_SIZE(invariant_fields); f++) {
            snprintf(label, sizeof(label), "part h0=%u h1=%u %s",
                     p->host0, p->host1, invariant_fields[f].name);
            check_range(label,
                        actual,   p->offset + invariant_fields[f].off,
                        expected, p->offset + invariant_fields[f].off,
                        invariant_fields[f].sz);
        }

        /* free_uncached must be at least as far as the algo's value */
        snprintf(label, sizeof(label),
                 "part h0=%u h1=%u free_uncached >=", p->host0, p->host1);
        g_assert_cmpuint(le32_to_cpu(act_hdr->offset_free_uncached), >=,
                         le32_to_cpu(exp_hdr->offset_free_uncached));
    }

    /*
     * 4. Items written by the algorithm.
     *
     * Non-global partitions (GLINK/SMP2P): algorithm is the only writer,
     * items sit right after the partition header.
     *
     * Global partition: ABL writes items before socinfo, so we search the
     * hardware item list by item ID to find each one, then compare data.
     */
    for (i = 0; i < cfg->num_partitions; i++) {
        const QcomSMEMPartitionConfig *p = &cfg->partitions[i];
        const SMEMPartitionHeader *exp_hdr;
        size_t hdr_sz = sizeof(SMEMPartitionHeader);
        size_t priv_sz = sizeof(SMEMPrivateEntry);
        uint32_t algo_end;
        char label[64];

        if (p->host0 == 0xffff && p->host1 == 0xffff) {
            continue;
        }

        exp_hdr = (const SMEMPartitionHeader *)(expected + p->offset);
        algo_end = le32_to_cpu(exp_hdr->offset_free_uncached);

        if (algo_end <= hdr_sz) {
            continue;  /* algorithm wrote no items here */
        }

        if (p->host0 == SMEM_GLOBAL_HOST && p->host1 == SMEM_GLOBAL_HOST) {
            /*
             * Global partition: walk the algorithm's items, find each by
             * item ID in the hardware's list, and compare data.
             */
            size_t exp_off = p->offset + hdr_sz;
            size_t exp_end = p->offset + algo_end;

            while (exp_off < exp_end) {
                const SMEMPrivateEntry *exp_e, *act_e;
                size_t act_off;
                uint16_t want_item;
                uint32_t want_size;
                bool found;

                exp_e = (const SMEMPrivateEntry *)(expected + exp_off);
                if (le16_to_cpu(exp_e->canary) != SMEM_PRIVATE_CANARY) {
                    break;
                }
                want_item = le16_to_cpu(exp_e->item);
                want_size = le32_to_cpu(exp_e->size);

                /* Locate this item ID in the hardware's list */
                found = false;
                act_off = p->offset + hdr_sz;
                while (act_off < p->offset + p->size) {
                    act_e = (const SMEMPrivateEntry *)(actual + act_off);
                    if (le16_to_cpu(act_e->canary) != SMEM_PRIVATE_CANARY) {
                        break;
                    }
                    if (le16_to_cpu(act_e->item) == want_item) {
                        found = true;
                        break;
                    }
                    act_off += priv_sz + le32_to_cpu(act_e->size);
                }

                snprintf(label, sizeof(label),
                         "global partition item %u", want_item);
                g_assert_true(found);

                if (want_item == SMEM_HW_SW_BUILD_ID) {
                    /*
                     * Socinfo (item 137): compare all fields except those
                     * that are device-specific and written at runtime:
                     *   serial_num  (offset 0x60, 4 bytes)
                     *   chip_id[32] (offset 0x7c, 32 bytes)
                     * Zero those fields in temporary copies before comparing.
                     */
                    uint8_t *act_copy = g_memdup2(
                        actual + act_off + priv_sz, want_size);
                    uint8_t *exp_copy = g_memdup2(
                        expected + exp_off + priv_sz, want_size);

                    memset(act_copy + offsetof(SMEMSocinfo, serial_num),
                           0, sizeof(((SMEMSocinfo *)0)->serial_num));
                    memset(exp_copy + offsetof(SMEMSocinfo, serial_num),
                           0, sizeof(((SMEMSocinfo *)0)->serial_num));
                    memset(act_copy + offsetof(SMEMSocinfo, chip_id),
                           0, sizeof(((SMEMSocinfo *)0)->chip_id));
                    memset(exp_copy + offsetof(SMEMSocinfo, chip_id),
                           0, sizeof(((SMEMSocinfo *)0)->chip_id));

                    check_range(label,
                                act_copy, 0, exp_copy, 0, want_size);
                    g_free(act_copy);
                    g_free(exp_copy);
                } else {
                    check_range(label,
                                actual,   act_off + priv_sz,
                                expected, exp_off + priv_sz,
                                want_size);
                }

                exp_off += priv_sz + want_size;
            }
        } else {
            /*
             * Non-global partitions: algorithm is sole writer; items start
             * at the same position in both buffers.
             */
            snprintf(label, sizeof(label),
                     "partition items h0=%u h1=%u", p->host0, p->host1);
            check_range(label,
                        actual,   p->offset + hdr_sz,
                        expected, p->offset + hdr_sz,
                        algo_end - hdr_sz);
        }
    }

    g_free(actual);
    g_free(reference);
    g_free(expected);
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/qcom-smem/matches-inc", test_smem_matches_inc);

    return g_test_run();
}
