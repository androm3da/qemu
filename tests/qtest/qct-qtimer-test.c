/*
 * QTest testcase for the QCT QtTimer device
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"
#include "hw/timer/qct-qtimer.h"

/* Test constants */
#define QTIMER_DEFAULT_FREQ_HZ 19200000ULL

/* Base addresses for testing - only frame/view region is mapped */
#define QTIMER_VIEW_BASE 0xfc921000  /* From hexagon virt board qtmr_region */

/* Helper functions for register access */
static uint32_t qtimer_read32(uint64_t base, uint32_t offset)
{
    return readl(base + offset);
}

__attribute__((unused))
static void qtimer_write32(uint64_t base, uint32_t offset, uint32_t value)
{
    writel(base + offset, value);
}

static uint64_t qtimer_read64(uint64_t base, uint32_t offset)
{
    uint32_t lo = qtimer_read32(base, offset);
    uint32_t hi = qtimer_read32(base, offset + 4);
    return ((uint64_t)hi << 32) | lo;
}

/* Test basic device presence and register access */
static void test_qtimer_basic_access(void)
{
    uint32_t val;

    /* Test frequency register - should be default value */
    val = qtimer_read32(QTIMER_VIEW_BASE, QCT_QTIMER_CNT_FREQ);
    g_assert_cmpuint(val, ==, QTIMER_DEFAULT_FREQ_HZ);
}

/* Test multiple timer frames */
static void test_qtimer_multiple_frames(void)
{
    uint32_t val;
    uint64_t frame0_base = QTIMER_VIEW_BASE;
    uint64_t frame1_base = QTIMER_VIEW_BASE + 0x1000;  /* Next frame */

    /* Test that both frames have the same frequency */
    val = qtimer_read32(frame0_base, QCT_QTIMER_CNT_FREQ);
    g_assert_cmpuint(val, ==, QTIMER_DEFAULT_FREQ_HZ);

    val = qtimer_read32(frame1_base, QCT_QTIMER_CNT_FREQ);
    g_assert_cmpuint(val, ==, QTIMER_DEFAULT_FREQ_HZ);
}

/* Test that registers exist and can be accessed */
static void test_qtimer_register_reads(void)
{
    qtimer_read32(QTIMER_VIEW_BASE, QCT_QTIMER_CNT_FREQ);
    qtimer_read64(QTIMER_VIEW_BASE, QCT_QTIMER_CNTPCT_LO);
    qtimer_read64(QTIMER_VIEW_BASE, QCT_QTIMER_CNTP_CVAL_LO);
    qtimer_read32(QTIMER_VIEW_BASE, QCT_QTIMER_CNTP_CTL);
    qtimer_read32(QTIMER_VIEW_BASE, QCT_QTIMER_CNTP_TVAL);
}

int main(int argc, char **argv)
{
    int r;

    g_test_init(&argc, &argv, NULL);

    /* Start QEMU with hexagon virt machine which includes the qtimer */
    qtest_start("-machine V66G_1024");

    qtest_add_func("/qct-qtimer/basic-access", test_qtimer_basic_access);
    qtest_add_func("/qct-qtimer/multiple-frames", test_qtimer_multiple_frames);
    qtest_add_func("/qct-qtimer/register-reads", test_qtimer_register_reads);

    r = g_test_run();

    qtest_end();

    return r;
}
