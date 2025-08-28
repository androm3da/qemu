/*
 * QTest testcase for the L2VIC Interrupt Controller
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"
#include "hw/intc/l2vic.h"

/* Base addresses for L2VIC */
#define L2VIC_BASE 0xfc910000
#define L2VIC_FAST_BASE 0xfc920000

/* Helper functions */
static uint32_t l2vic_read32(uint64_t base, uint32_t offset)
{
    return readl(base + offset);
}

static void l2vic_write32(uint64_t base, uint32_t offset, uint32_t value)
{
    writel(base + offset, value);
}

/* Test basic register access */
static void test_l2vic_register_access(void)
{
    uint32_t val;

    /* Test that basic registers can be read/written */
    l2vic_write32(L2VIC_BASE, L2VIC_INT_ENABLE_SETn, 0x1);
    val = l2vic_read32(L2VIC_BASE, L2VIC_INT_ENABLEn);
    /* Should have enabled interrupt 0 */
    g_assert_cmpuint(val & 0x1, ==, 0x1);

    /* Clear it */
    l2vic_write32(L2VIC_BASE, L2VIC_INT_ENABLE_CLEARn, 0x1);
    val = l2vic_read32(L2VIC_BASE, L2VIC_INT_ENABLEn);
    /* Should be cleared now */
    g_assert_cmpuint(val & 0x1, ==, 0x0);
}

/* Test interrupt enable/disable */
static void test_l2vic_interrupt_enable(void)
{
    uint32_t val;

    /* Initially all interrupts should be disabled */
    val = l2vic_read32(L2VIC_BASE, L2VIC_INT_ENABLEn);
    g_assert_cmpuint(val, ==, 0);

    /* Enable some interrupts */
    l2vic_write32(L2VIC_BASE, L2VIC_INT_ENABLE_SETn, 0x5);  /* Enable IRQ 0 and 2 */
    val = l2vic_read32(L2VIC_BASE, L2VIC_INT_ENABLEn);
    g_assert_cmpuint(val & 0x5, ==, 0x5);

    /* Disable one */
    l2vic_write32(L2VIC_BASE, L2VIC_INT_ENABLE_CLEARn, 0x1);  /* Disable IRQ 0 */
    val = l2vic_read32(L2VIC_BASE, L2VIC_INT_ENABLEn);
    g_assert_cmpuint(val & 0x1, ==, 0x0);  /* IRQ 0 should be disabled */
    g_assert_cmpuint(val & 0x4, ==, 0x4);  /* IRQ 2 should still be enabled */
}

/* Test register read/write without triggering interrupts */
static void test_l2vic_basic_functionality(void)
{
    l2vic_read32(L2VIC_BASE, L2VIC_INT_ENABLEn);
    l2vic_read32(L2VIC_BASE, L2VIC_INT_PENDINGn);
    l2vic_read32(L2VIC_BASE, L2VIC_INT_STATUSn);
    l2vic_read32(L2VIC_BASE, L2VIC_INT_TYPEn);

    /* Exercise write-only registers */
    l2vic_write32(L2VIC_BASE, L2VIC_INT_ENABLE_SETn, 0);
    l2vic_write32(L2VIC_BASE, L2VIC_INT_ENABLE_CLEARn, 0);
}

int main(int argc, char **argv)
{
    int r;

    g_test_init(&argc, &argv, NULL);

    /* Start QEMU with hexagon V66G_1024 machine which includes the L2VIC */
    qtest_start("-machine V66G_1024");

    qtest_add_func("/l2vic/register-access", test_l2vic_register_access);
    qtest_add_func("/l2vic/interrupt-enable", test_l2vic_interrupt_enable);
    qtest_add_func("/l2vic/basic-functionality", test_l2vic_basic_functionality);

    r = g_test_run();

    qtest_end();

    return r;
}
