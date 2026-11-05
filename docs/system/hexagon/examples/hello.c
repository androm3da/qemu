/* SPDX-License-Identifier: GPL-2.0-or-later */

#define UART_BASE 0x10000000u
#define UART_DR   0x00
#define UART_FR   0x18
#define UART_IBRD 0x24
#define UART_FBRD 0x28
#define UART_LCRH 0x2c
#define UART_CR   0x30
#define UART_IMSC 0x38
#define UART_ICR  0x44
#define UART_TXFF (1u << 5)

/* MMIO reads and writes must reach the device on every access. */
typedef volatile unsigned int UartRegister;

static UartRegister *uart_reg(unsigned int offset)
{
    return (UartRegister *)(UART_BASE + offset);
}

static void uart_init(void)
{
    *uart_reg(UART_CR) = 0;
    *uart_reg(UART_IMSC) = 0;
    *uart_reg(UART_ICR) = 0x7ff;
    /* 24 MHz UART clock, 115200 baud, eight data bits, FIFO enabled. */
    *uart_reg(UART_IBRD) = 13;
    *uart_reg(UART_FBRD) = 1;
    *uart_reg(UART_LCRH) = 0x70;
    *uart_reg(UART_CR) = 0x301;
}

static void uart_putc(char c)
{
    while (*uart_reg(UART_FR) & UART_TXFF) {
        /* Wait for room in the transmit FIFO. */
    }
    *uart_reg(UART_DR) = (unsigned char)c;
}

/* Volatile prevents folding this startup check into compile-time constants. */
static volatile unsigned int initialized = 42;
/* Keep a runtime read of zero-initialized storage for the startup check. */
static volatile unsigned int zero;

int main(void)
{
    const char *message = "Hello from Hexagon!\r\n";

    uart_init();
    if (initialized != 42 || zero != 0) {
        message = "Startup failed\r\n";
    }
    while (*message) {
        uart_putc(*message++);
    }
    return 0;
}
