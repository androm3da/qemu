/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QTest testcase for the QCS6490 CDSP machine: checks that the modelled
 * subsystem matches the values read from the hardware.
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"
#include "hw/hexagon/hexagon.h"

#include "hw/hexagon/machine_cfg_qcs6490_cdsp.h.inc"

/* Word offsets into the config table, as used by the H2 hypervisor. */
#define CFG_TABLE_L2TCM 0x00
#define CFG_TABLE_SSBASE 0x08
#define CFG_TABLE_CORECFG_BASE 0x0c
#define CFG_TABLE_L2REGS 0x10
#define CFG_TABLE_CLADEREGS 0x24
#define CFG_TABLE_FASTL2VIC_BASE 0x28
#define CFG_TABLE_JTLB_SIZE 0x2c
#define CFG_TABLE_COPROC_TYPE 0x30
#define CFG_TABLE_COPROC_CONTEXTS 0x34
#define CFG_TABLE_VTCM_BASE 0x38
#define CFG_TABLE_VTCM_SIZE 0x3c
#define CFG_TABLE_L2TAG_SIZE 0x40
#define CFG_TABLE_L2ARRAY_SIZE 0x44
#define CFG_TABLE_HTHREADS_MASK 0x48
#define CFG_TABLE_ECC_BASE 0x4c
#define CFG_TABLE_L2_LINE_SZ 0x50
#define CFG_TABLE_COPROC_VLENGTH 0x6c
#define CFG_TABLE_DMA_VERSION 0x68
#define CFG_TABLE_L1D_SZ 0xa4
#define CFG_TABLE_L1I_SZ 0xa8
#define CFG_TABLE_VTCM_BANK_WIDTH 0xb0

/* QTimer access-control frame registers, and the frames' CNTFRQ */
#define QTIMER_AC_CNTFRQ 0x000
#define QTIMER_AC_CNTTID 0x008
#define QTIMER_AC_VERSION 0xfd0
#define QTIMER_FRAME_CNTFRQ 0x010
#define QTIMER_FRAME_STRIDE 0x1000

#define QTIMER_FREQ_HZ 19200000
#define QTIMER_VERSION 0x20020000
/* Three frames, each with the physical view only */
#define QTIMER_CNTTID 0x111

#define DDR_BASE 0x80000000
#define VTCM_BASE 0x09c00000
#define VTCM_SIZE (2 * 1024 * 1024)
#define SUBSYSTEM_BASE 0x0a380000

static uint32_t cfg(uint32_t offset)
{
    return readl(qcs6490_cdsp.cfgbase + offset);
}

/* The values the H2 hypervisor derived from the CDSP's config table. */
static void test_config_table(void)
{
    g_assert_cmphex(qcs6490_cdsp.cfgbase, ==, 0x09980000);

    g_assert_cmphex(cfg(CFG_TABLE_L2TCM) << 16, ==, 0x09800000);
    g_assert_cmphex(cfg(CFG_TABLE_SSBASE) << 16, ==, SUBSYSTEM_BASE);
    g_assert_cmphex(cfg(CFG_TABLE_CORECFG_BASE) << 16, ==, 0x09990000);
    g_assert_cmphex(cfg(CFG_TABLE_L2REGS) << 16, ==, 0x099a0000);
    g_assert_cmphex(cfg(CFG_TABLE_CLADEREGS) << 16, ==, 0x099d0000);
    g_assert_cmphex(cfg(CFG_TABLE_FASTL2VIC_BASE) << 16, ==, 0x099e0000);
    g_assert_cmphex(cfg(CFG_TABLE_ECC_BASE) << 16, ==, 0x099f0000);
    g_assert_cmphex(cfg(CFG_TABLE_VTCM_BASE) << 16, ==, VTCM_BASE);
    g_assert_cmpuint(cfg(CFG_TABLE_VTCM_SIZE) * 1024, ==, VTCM_SIZE);
    g_assert_cmpuint(cfg(CFG_TABLE_VTCM_BANK_WIDTH), ==, 0x40);

    g_assert_cmpuint(cfg(CFG_TABLE_JTLB_SIZE), ==, 128);
    g_assert_cmphex(cfg(CFG_TABLE_HTHREADS_MASK), ==, 0x3f);
    g_assert_cmphex(cfg(CFG_TABLE_COPROC_TYPE), ==, 1);
    g_assert_cmpuint(cfg(CFG_TABLE_COPROC_CONTEXTS), ==, 2);
    g_assert_cmpuint(cfg(CFG_TABLE_DMA_VERSION), ==, 1);
    g_assert_cmpuint(1u << cfg(CFG_TABLE_COPROC_VLENGTH), ==, 128);

    g_assert_cmpuint(cfg(CFG_TABLE_L2TAG_SIZE) * 1024, ==, 1024 * 1024);
    g_assert_cmpuint(cfg(CFG_TABLE_L2ARRAY_SIZE) * 1024, ==, 1024 * 1024);
    g_assert_cmpuint(cfg(CFG_TABLE_L2_LINE_SZ), ==, 128);
    g_assert_cmpuint(cfg(CFG_TABLE_L1D_SZ), ==, 16);
    g_assert_cmpuint(cfg(CFG_TABLE_L1I_SZ), ==, 32);
}

/* The subsystem devices sit at fixed offsets from the subsystem base. */
static void test_device_layout(void)
{
    g_assert_cmphex(qcs6490_cdsp.l2vic_base, ==, SUBSYSTEM_BASE + 0x10000);
    g_assert_cmphex(qcs6490_cdsp.csr_base, ==, SUBSYSTEM_BASE + 0x20000);
    g_assert_cmphex(qcs6490_cdsp.qtmr_region, ==, SUBSYSTEM_BASE + 0x21000);
}

/* What the CDSP's QTimer reports for itself. */
static void test_qtimer_identity(void)
{
    uint64_t ac = qcs6490_cdsp.csr_base;
    uint64_t frames = qcs6490_cdsp.qtmr_region;

    g_assert_cmpuint(readl(ac + QTIMER_AC_CNTFRQ), ==, QTIMER_FREQ_HZ);
    g_assert_cmphex(readl(ac + QTIMER_AC_CNTTID), ==, QTIMER_CNTTID);
    g_assert_cmphex(readl(ac + QTIMER_AC_VERSION), ==, QTIMER_VERSION);

    for (int frame = 0; frame < 3; frame++) {
        g_assert_cmpuint(readl(frames + frame * QTIMER_FRAME_STRIDE +
                               QTIMER_FRAME_CNTFRQ), ==, QTIMER_FREQ_HZ);
    }
}

/* DDR starts at 0x80000000, and VTCM is real memory. */
static void test_memory(void)
{
    writel(DDR_BASE, 0xdeadbeef);
    g_assert_cmphex(readl(DDR_BASE), ==, 0xdeadbeef);

    writel(VTCM_BASE, 0x12345678);
    writel(VTCM_BASE + VTCM_SIZE - 4, 0x9abcdef0);
    g_assert_cmphex(readl(VTCM_BASE), ==, 0x12345678);
    g_assert_cmphex(readl(VTCM_BASE + VTCM_SIZE - 4), ==, 0x9abcdef0);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_start("-machine qcs6490-cdsp");

    qtest_add_func("/qcs6490-cdsp/config-table", test_config_table);
    qtest_add_func("/qcs6490-cdsp/device-layout", test_device_layout);
    qtest_add_func("/qcs6490-cdsp/qtimer-identity", test_qtimer_identity);
    qtest_add_func("/qcs6490-cdsp/memory", test_memory);

    ret = g_test_run();

    qtest_end();
    return ret;
}
