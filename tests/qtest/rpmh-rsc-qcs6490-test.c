/*
 * QTest testcase for RPMH-RSC
 *
 * Copyright (c) Qualcomm Technologies, Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/bitops.h"

#define RPMH_RSC_BASE       0x18200000

/* TCS Types */
#define SLEEP_TCS           0
#define WAKE_TCS            1
#define ACTIVE_TCS          2
#define CONTROL_TCS         3

/* Register offsets (v2.7) */
#define DRV_SOLVER_CONFIG           0x04
#define DRV_PRNT_CHLD_CONFIG       0x0C
#define RSC_DRV_IRQ_ENABLE         0x00
#define RSC_DRV_IRQ_STATUS         0x04
#define RSC_DRV_IRQ_CLEAR          0x08
#define RSC_DRV_CMD_WAIT_FOR_CMPL  0x10
#define RSC_DRV_CONTROL            0x14
#define RSC_DRV_STATUS             0x18
#define RSC_DRV_CMD_ENABLE         0x1C
#define RSC_DRV_CMD_MSGID          0x30
#define RSC_DRV_CMD_ADDR           0x34
#define RSC_DRV_CMD_DATA           0x38
#define RSC_DRV_CMD_STATUS         0x3C
#define RSC_DRV_CMD_RESP_DATA      0x40

#define RSC_DRV_TCS_OFFSET         672
#define RSC_DRV_CMD_OFFSET         20

/* Configuration bits */
#define DRV_NUM_TCS_MASK           0x3F
#define DRV_NUM_TCS_SHIFT          6
#define DRV_NCPT_MASK              0x1F
#define DRV_NCPT_SHIFT             27

/* TCS control bits */
#define TCS_AMC_MODE_ENABLE        BIT(16)
#define TCS_AMC_MODE_TRIGGER       BIT(24)

/* Command bits */
#define CMD_MSGID_RESP_REQ         BIT(8)
#define CMD_MSGID_WRITE            BIT(16)
#define CMD_STATUS_ISSUED          BIT(8)
#define CMD_STATUS_COMPL           BIT(16)

/* Test fixture */
typedef struct {
    QTestState *qts;
    uint64_t base;
    uint32_t tcs_offset;
    uint32_t num_tcs;
    uint32_t ncpt;
} RpmhRscTestFixture;

static uint32_t rpmh_rsc_readl(RpmhRscTestFixture *f, uint32_t offset)
{
    return qtest_readl(f->qts, f->base + offset);
}

static void rpmh_rsc_writel(RpmhRscTestFixture *f, uint32_t offset,
                            uint32_t value)
{
    qtest_writel(f->qts, f->base + offset, value);
}

static uint32_t tcs_reg_addr(RpmhRscTestFixture *f, int tcs_id, uint32_t reg)
{
    return f->tcs_offset + RSC_DRV_TCS_OFFSET * tcs_id + reg;
}

static uint32_t tcs_cmd_addr(RpmhRscTestFixture *f, int tcs_id, int cmd_id,
                             uint32_t reg)
{
    return tcs_reg_addr(f, tcs_id, reg) + RSC_DRV_CMD_OFFSET * cmd_id;
}

static void rpmh_rsc_setup(RpmhRscTestFixture *f)
{
    f->qts = qtest_init("-machine qcs6490");
    f->base = RPMH_RSC_BASE;

    /* Read hardware configuration */
    uint32_t config = rpmh_rsc_readl(f, DRV_PRNT_CHLD_CONFIG);

    /* Extract number of TCS and commands per TCS */
    f->num_tcs = (config >> DRV_NUM_TCS_SHIFT) & DRV_NUM_TCS_MASK;
    f->ncpt = (config >> DRV_NCPT_SHIFT) & DRV_NCPT_MASK;
    f->tcs_offset = 0x00000D00; /* Default TCS offset */

    g_assert_cmpuint(f->num_tcs, >, 0);
    g_assert_cmpuint(f->num_tcs, <=, 12); /* Reasonable limit */
    g_assert_cmpuint(f->ncpt, >, 0);
    g_assert_cmpuint(f->ncpt, <=, 16);
}

static void rpmh_rsc_teardown(RpmhRscTestFixture *f)
{
    qtest_quit(f->qts);
}

/* Test basic register access */
static void test_rpmh_rsc_basic_registers(void)
{
    RpmhRscTestFixture f = {0};
    rpmh_rsc_setup(&f);

    /* Test IRQ enable/clear registers */
    rpmh_rsc_writel(&f, RSC_DRV_IRQ_ENABLE, 0xFF);
    g_assert_cmphex(rpmh_rsc_readl(&f, RSC_DRV_IRQ_ENABLE), ==, 0xFF);

    /* IRQ clear is write-only, status should be 0 initially */
    g_assert_cmphex(rpmh_rsc_readl(&f, RSC_DRV_IRQ_STATUS), ==, 0);

    /* Test configuration registers are read-only */
    uint32_t solver_cfg = rpmh_rsc_readl(&f, DRV_SOLVER_CONFIG);
    uint32_t prnt_chld_cfg = rpmh_rsc_readl(&f, DRV_PRNT_CHLD_CONFIG);

    /* Try to write - should not change */
    rpmh_rsc_writel(&f, DRV_SOLVER_CONFIG, 0xDEADBEEF);
    rpmh_rsc_writel(&f, DRV_PRNT_CHLD_CONFIG, 0xDEADBEEF);

    g_assert_cmphex(rpmh_rsc_readl(&f, DRV_SOLVER_CONFIG), ==, solver_cfg);
    g_assert_cmphex(rpmh_rsc_readl(&f, DRV_PRNT_CHLD_CONFIG), ==,
                    prnt_chld_cfg);

    rpmh_rsc_teardown(&f);
}

/* Test TCS command programming */
static void test_rpmh_rsc_tcs_commands(void)
{
    RpmhRscTestFixture f = {0};
    rpmh_rsc_setup(&f);

    int tcs_id = 0; /* Use first TCS */
    int cmd_id = 0; /* First command */

    /* Program a command */
    uint32_t test_addr = 0x12345678;
    uint32_t test_data = 0xABCDEF01;
    uint32_t msgid = CMD_MSGID_WRITE | CMD_MSGID_RESP_REQ;

    /* Write command registers */
    rpmh_rsc_writel(&f, tcs_cmd_addr(&f, tcs_id, cmd_id,
                                    RSC_DRV_CMD_MSGID), msgid);
    rpmh_rsc_writel(&f, tcs_cmd_addr(&f, tcs_id, cmd_id,
                                    RSC_DRV_CMD_ADDR), test_addr);
    rpmh_rsc_writel(&f, tcs_cmd_addr(&f, tcs_id, cmd_id,
                                    RSC_DRV_CMD_DATA), test_data);

    /* Verify writes */
    g_assert_cmphex(rpmh_rsc_readl(&f,
                                   tcs_cmd_addr(&f, tcs_id, cmd_id,
                                                RSC_DRV_CMD_MSGID)),
                    ==, msgid);
    g_assert_cmphex(rpmh_rsc_readl(&f,
                                   tcs_cmd_addr(&f, tcs_id, cmd_id,
                                                RSC_DRV_CMD_ADDR)),
                    ==, test_addr);
    g_assert_cmphex(rpmh_rsc_readl(&f,
                                   tcs_cmd_addr(&f, tcs_id, cmd_id,
                                                RSC_DRV_CMD_DATA)),
                    ==, test_data);

    /* Command status should be 0 before trigger */
    g_assert_cmphex(rpmh_rsc_readl(&f,
                                   tcs_cmd_addr(&f, tcs_id, cmd_id,
                                                RSC_DRV_CMD_STATUS)),
                    ==, 0);

    rpmh_rsc_teardown(&f);
}

/* Test TCS trigger and completion */
static void test_rpmh_rsc_tcs_trigger(void)
{
    RpmhRscTestFixture f = {0};
    rpmh_rsc_setup(&f);

    int tcs_id = 0;
    int cmd_id = 0;

    /* Setup a command */
    uint32_t msgid = CMD_MSGID_WRITE;
    rpmh_rsc_writel(&f, tcs_cmd_addr(&f, tcs_id, cmd_id, RSC_DRV_CMD_MSGID),
                    msgid);
    rpmh_rsc_writel(&f, tcs_cmd_addr(&f, tcs_id, cmd_id, RSC_DRV_CMD_ADDR),
                    0x11111111);
    rpmh_rsc_writel(&f, tcs_cmd_addr(&f, tcs_id, cmd_id, RSC_DRV_CMD_DATA),
                    0x22222222);

    /* Enable command 0 */
    rpmh_rsc_writel(&f, tcs_reg_addr(&f, tcs_id, RSC_DRV_CMD_ENABLE),
                    BIT(cmd_id));

    /* Enable TCS AMC mode */
    rpmh_rsc_writel(&f, tcs_reg_addr(&f, tcs_id, RSC_DRV_CONTROL),
                    TCS_AMC_MODE_ENABLE);

    /* Trigger the TCS */
    rpmh_rsc_writel(&f, tcs_reg_addr(&f, tcs_id, RSC_DRV_CONTROL),
                    TCS_AMC_MODE_ENABLE | TCS_AMC_MODE_TRIGGER);

    /* Check command status - should show issued and completed */
    uint32_t status = rpmh_rsc_readl(&f,
                                     tcs_cmd_addr(&f, tcs_id, cmd_id,
                                                  RSC_DRV_CMD_STATUS));
    g_assert_cmphex(status & (CMD_STATUS_ISSUED | CMD_STATUS_COMPL), ==,
                    CMD_STATUS_ISSUED | CMD_STATUS_COMPL);

    /* Check IRQ status - should have TCS0 bit set */
    uint32_t irq_status = rpmh_rsc_readl(&f,
                                         f.tcs_offset + RSC_DRV_IRQ_STATUS);
    g_assert_cmphex(irq_status & BIT(tcs_id), ==, BIT(tcs_id));

    /* Clear IRQ */
    rpmh_rsc_writel(&f, RSC_DRV_IRQ_CLEAR, BIT(tcs_id));
    g_assert_cmphex(rpmh_rsc_readl(&f,
                                   f.tcs_offset + RSC_DRV_IRQ_STATUS) &
                    BIT(tcs_id), ==, 0);

    rpmh_rsc_teardown(&f);
}

/* Test multiple commands in a TCS */
static void test_rpmh_rsc_multi_command_tcs(void)
{
    RpmhRscTestFixture f = {0};
    rpmh_rsc_setup(&f);

    int tcs_id = 1; /* Use second TCS */
    int num_cmds = 3;
    uint32_t cmd_enable_mask = 0;

    /* Program multiple commands */
    for (int i = 0; i < num_cmds; i++) {
        uint32_t msgid = CMD_MSGID_WRITE | (i == 0 ? CMD_MSGID_RESP_REQ : 0);
        uint32_t addr = 0x10000000 + (i * 0x1000);
        uint32_t data = 0xAABBCC00 + i;

        rpmh_rsc_writel(&f, tcs_cmd_addr(&f, tcs_id, i, RSC_DRV_CMD_MSGID),
                        msgid);
        rpmh_rsc_writel(&f, tcs_cmd_addr(&f, tcs_id, i, RSC_DRV_CMD_ADDR),
                        addr);
        rpmh_rsc_writel(&f, tcs_cmd_addr(&f, tcs_id, i, RSC_DRV_CMD_DATA),
                        data);

        cmd_enable_mask |= BIT(i);
    }

    /* Enable all commands */
    rpmh_rsc_writel(&f, tcs_reg_addr(&f, tcs_id, RSC_DRV_CMD_ENABLE),
                    cmd_enable_mask);

    /* Trigger TCS */
    rpmh_rsc_writel(&f, tcs_reg_addr(&f, tcs_id, RSC_DRV_CONTROL),
                    TCS_AMC_MODE_ENABLE | TCS_AMC_MODE_TRIGGER);

    /* Verify all commands completed */
    for (int i = 0; i < num_cmds; i++) {
        uint32_t status = rpmh_rsc_readl(&f,
                                         tcs_cmd_addr(&f, tcs_id, i,
                                                      RSC_DRV_CMD_STATUS));
        g_assert_cmphex(status & CMD_STATUS_COMPL, ==, CMD_STATUS_COMPL);
    }

    rpmh_rsc_teardown(&f);
}

/* Test command invalidation (typical Linux usage pattern) */
static void test_rpmh_rsc_tcs_invalidate(void)
{
    RpmhRscTestFixture f = {0};
    rpmh_rsc_setup(&f);

    int tcs_id = 2;

    /* Setup and trigger a command */
    rpmh_rsc_writel(&f, tcs_cmd_addr(&f, tcs_id, 0,
                                    RSC_DRV_CMD_MSGID), CMD_MSGID_WRITE);
    rpmh_rsc_writel(&f, tcs_cmd_addr(&f, tcs_id, 0,
                                    RSC_DRV_CMD_ADDR), 0x33333333);
    rpmh_rsc_writel(&f, tcs_cmd_addr(&f, tcs_id, 0,
                                    RSC_DRV_CMD_DATA), 0x44444444);
    rpmh_rsc_writel(&f, tcs_reg_addr(&f, tcs_id, RSC_DRV_CMD_ENABLE), BIT(0));

    /* Verify command is enabled */
    g_assert_cmphex(rpmh_rsc_readl(&f,
                                   tcs_reg_addr(&f, tcs_id,
                                                RSC_DRV_CMD_ENABLE)),
                    ==, BIT(0));

    /* Invalidate TCS by writing 0 to CMD_ENABLE (Linux pattern) */
    rpmh_rsc_writel(&f, tcs_reg_addr(&f, tcs_id, RSC_DRV_CMD_ENABLE), 0);

    /* Verify invalidation */
    g_assert_cmphex(rpmh_rsc_readl(&f,
                                   tcs_reg_addr(&f, tcs_id,
                                                RSC_DRV_CMD_ENABLE)),
                    ==, 0);

    rpmh_rsc_teardown(&f);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/qcs6490/rpmh-rsc/basic-registers",
                   test_rpmh_rsc_basic_registers);
    qtest_add_func("/qcs6490/rpmh-rsc/tcs-commands",
                   test_rpmh_rsc_tcs_commands);
    qtest_add_func("/qcs6490/rpmh-rsc/tcs-trigger",
                   test_rpmh_rsc_tcs_trigger);
    qtest_add_func("/qcs6490/rpmh-rsc/multi-command-tcs",
                   test_rpmh_rsc_multi_command_tcs);
    qtest_add_func("/qcs6490/rpmh-rsc/tcs-invalidate",
                   test_rpmh_rsc_tcs_invalidate);

    return g_test_run();
}
