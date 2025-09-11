/*
 * QTest test cases for PL011 UART (Rust implementation)
 *
 * Copyright (c) 2024 QEMU Contributors
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/module.h"

/* PL011 Register offsets */
#define UARTDR          0x000  /* Data Register */
#define UARTRSR         0x004  /* Receive Status Register */
#define UARTFR          0x018  /* Flag Register */
#define UARTILPR        0x020  /* IrDA Low-Power Counter Register */
#define UARTIBRD        0x024  /* Integer Baud Rate Register */
#define UARTFBRD        0x028  /* Fractional Baud Rate Register */
#define UARTLCR_H       0x02C  /* Line Control Register */
#define UARTCR          0x030  /* Control Register */
#define UARTIFLS        0x034  /* Interrupt FIFO Level Select Register */
#define UARTIMSC        0x038  /* Interrupt Mask Set/Clear Register */
#define UARTRIS         0x03C  /* Raw Interrupt Status Register */
#define UARTMIS         0x040  /* Masked Interrupt Status Register */
#define UARTICR         0x044  /* Interrupt Clear Register */
#define UARTDMACR       0x048  /* DMA Control Register */

/* Device ID registers */
#define UART_PID0       0xFE0
#define UART_PID1       0xFE4
#define UART_PID2       0xFE8
#define UART_PID3       0xFEC
#define UART_CID0       0xFF0
#define UART_CID1       0xFF4
#define UART_CID2       0xFF8
#define UART_CID3       0xFFC

/* Flag Register bits */
#define FR_TXFE         (1 << 7)  /* Transmit FIFO empty */
#define FR_RXFF         (1 << 6)  /* Receive FIFO full */
#define FR_TXFF         (1 << 5)  /* Transmit FIFO full */
#define FR_RXFE         (1 << 4)  /* Receive FIFO empty */
#define FR_BUSY         (1 << 3)  /* UART busy */

/* Control Register bits */
#define CR_RXE          (1 << 9)  /* Receive enable */
#define CR_TXE          (1 << 8)  /* Transmit enable */
#define CR_LBE          (1 << 7)  /* Loopback enable */
#define CR_UARTEN       (1 << 0)  /* UART enable */

/* Interrupt bits */
#define INT_TX          (1 << 5)  /* Transmit interrupt */
#define INT_RX          (1 << 4)  /* Receive interrupt */

/* Test base address - adjust based on machine */
#define UART_BASE       0x09000000

/* Check if PL011 is available */
static bool pl011_available(void)
{
    /* PL011 should be available on ARM/AArch64 virt machine */
    return true;
}

/* Helper functions */
static uint32_t pl011_read(QTestState *qts, uint32_t offset)
{
    return qtest_readl(qts, UART_BASE + offset);
}

static void pl011_write(QTestState *qts, uint32_t offset, uint32_t value)
{
    qtest_writel(qts, UART_BASE + offset, value);
}

/* Test PL011 device identification */
static void test_device_id(void)
{
    if (!pl011_available()) {
        g_test_skip("PL011 device not available");
        return;
    }
    QTestState *qts = qtest_init("-M virt");
    uint32_t id;

    /* Check Peripheral ID registers */
    id = pl011_read(qts, UART_PID0);
    g_assert_cmpuint(id, ==, 0x11);

    id = pl011_read(qts, UART_PID1);
    g_assert_cmpuint(id, ==, 0x10);

    id = pl011_read(qts, UART_PID2);
    g_assert_cmpuint(id, ==, 0x04);

    /* Check Cell ID registers (PrimeCell ID) */
    id = pl011_read(qts, UART_CID0);
    g_assert_cmpuint(id, ==, 0x0D);

    id = pl011_read(qts, UART_CID1);
    g_assert_cmpuint(id, ==, 0xF0);

    id = pl011_read(qts, UART_CID2);
    g_assert_cmpuint(id, ==, 0x05);

    id = pl011_read(qts, UART_CID3);
    g_assert_cmpuint(id, ==, 0xB1);

    qtest_quit(qts);
}

/* Test PL011 initialization and reset state */
static void test_init_reset(void)
{
    if (!pl011_available()) {
        g_test_skip("PL011 device not available");
        return;
    }
    QTestState *qts = qtest_init("-M virt");
    uint32_t val;

    /* Check reset state of Flag Register */
    val = pl011_read(qts, UARTFR);
    g_assert_cmpuint(val & FR_TXFE, ==, FR_TXFE);  /* TX FIFO should be empty */
    g_assert_cmpuint(val & FR_RXFE, ==, FR_RXFE);  /* RX FIFO should be empty */
    g_assert_cmpuint(val & FR_TXFF, ==, 0);        /* TX FIFO not full */
    g_assert_cmpuint(val & FR_RXFF, ==, 0);        /* RX FIFO not full */

    /* Check Control Register reset state */
    val = pl011_read(qts, UARTCR);
    g_assert_cmpuint(val & (CR_RXE | CR_TXE), ==, CR_RXE | CR_TXE);

    /* Check interrupt mask is clear */
    val = pl011_read(qts, UARTIMSC);
    g_assert_cmpuint(val, ==, 0);

    qtest_quit(qts);
}

/* Test basic TX operation */
static void test_tx_basic(void)
{
    if (!pl011_available()) {
        g_test_skip("PL011 device not available");
        return;
    }
    QTestState *qts = qtest_init("-M virt");
    uint32_t flags;

    /* Enable UART */
    pl011_write(qts, UARTCR, CR_UARTEN | CR_TXE | CR_RXE);

    /* Check TX FIFO is empty */
    flags = pl011_read(qts, UARTFR);
    g_assert_cmpuint(flags & FR_TXFE, ==, FR_TXFE);

    /* Write a character */
    pl011_write(qts, UARTDR, 'A');

    /* TX FIFO should no longer be empty */
    flags = pl011_read(qts, UARTFR);
    g_assert_cmpuint(flags & FR_TXFE, ==, 0);

    qtest_quit(qts);
}

/* Test loopback mode */
static void test_loopback(void)
{
    if (!pl011_available()) {
        g_test_skip("PL011 device not available");
        return;
    }
    QTestState *qts = qtest_init("-M virt");
    uint32_t data;

    /* Enable UART with loopback */
    pl011_write(qts, UARTCR, CR_UARTEN | CR_TXE | CR_RXE | CR_LBE);

    /* Send data */
    pl011_write(qts, UARTDR, 0x55);

    /* Data should appear in RX FIFO */
    data = pl011_read(qts, UARTDR);
    g_assert_cmpuint(data & 0xFF, ==, 0x55);

    /* Send another byte */
    pl011_write(qts, UARTDR, 0xAA);
    data = pl011_read(qts, UARTDR);
    g_assert_cmpuint(data & 0xFF, ==, 0xAA);

    qtest_quit(qts);
}

/* Test interrupt generation */
static void test_interrupts(void)
{
    if (!pl011_available()) {
        g_test_skip("PL011 device not available");
        return;
    }
    QTestState *qts = qtest_init("-M virt");
    uint32_t mis;

    /* Enable UART and TX interrupt */
    pl011_write(qts, UARTCR, CR_UARTEN | CR_TXE | CR_RXE);
    pl011_write(qts, UARTIMSC, INT_TX);

    /* TX interrupt should be active (FIFO empty) */
    mis = pl011_read(qts, UARTMIS);
    g_assert_cmpuint(mis & INT_TX, ==, INT_TX);

    /* Clear interrupt */
    pl011_write(qts, UARTICR, INT_TX);
    mis = pl011_read(qts, UARTMIS);
    g_assert_cmpuint(mis & INT_TX, ==, 0);

    /* Test RX interrupt in loopback */
    pl011_write(qts, UARTCR, CR_UARTEN | CR_TXE | CR_RXE | CR_LBE);
    pl011_write(qts, UARTIMSC, INT_RX);

    /* Send data */
    pl011_write(qts, UARTDR, 0x42);

    /* RX interrupt should be active */
    mis = pl011_read(qts, UARTMIS);
    g_assert_cmpuint(mis & INT_RX, ==, INT_RX);

    /* Read data to clear condition */
    pl011_read(qts, UARTDR);

    qtest_quit(qts);
}

/* Test baud rate configuration */
static void test_baud_rate(void)
{
    if (!pl011_available()) {
        g_test_skip("PL011 device not available");
        return;
    }
    QTestState *qts = qtest_init("-M virt");
    uint32_t ibrd, fbrd;

    /* Set integer baud rate divisor */
    pl011_write(qts, UARTIBRD, 0x1234);
    ibrd = pl011_read(qts, UARTIBRD);
    g_assert_cmpuint(ibrd, ==, 0x1234);

    /* Set fractional baud rate divisor */
    pl011_write(qts, UARTFBRD, 0x35);
    fbrd = pl011_read(qts, UARTFBRD);
    g_assert_cmpuint(fbrd, ==, 0x35);

    qtest_quit(qts);
}

/* Test line control configuration */
static void test_line_control(void)
{
    if (!pl011_available()) {
        g_test_skip("PL011 device not available");
        return;
    }
    QTestState *qts = qtest_init("-M virt");
    uint32_t lcr;

    /* Set 8 bits, no parity, 2 stop bits */
    pl011_write(qts, UARTLCR_H, 0x70);  /* WLEN=11 (8 bits), STP2 */
    lcr = pl011_read(qts, UARTLCR_H);
    g_assert_cmpuint(lcr, ==, 0x70);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/pl011/device-id", test_device_id);
    qtest_add_func("/pl011/init-reset", test_init_reset);
    qtest_add_func("/pl011/tx-basic", test_tx_basic);
    qtest_add_func("/pl011/loopback", test_loopback);
    qtest_add_func("/pl011/interrupts", test_interrupts);
    qtest_add_func("/pl011/baud-rate", test_baud_rate);
    qtest_add_func("/pl011/line-control", test_line_control);

    return g_test_run();
}