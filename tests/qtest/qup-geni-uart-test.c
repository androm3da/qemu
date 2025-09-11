/*
 * QTest test cases for Qualcomm GENI UART
 *
 * Copyright (c) Qualcomm Technologies, Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/module.h"
#include "hw/char/qup_geni_uart.h"

/* Test base address - using UART1 from QCS6490 memory map */
#define UART_BASE 0x00984000

/* Helper functions */
static uint32_t uart_read(QTestState *qts, uint32_t offset)
{
    return qtest_readl(qts, UART_BASE + offset);
}

static void uart_write(QTestState *qts, uint32_t offset, uint32_t value)
{
    qtest_writel(qts, UART_BASE + offset, value);
}



static void uart_wait_for_tx_complete(QTestState *qts)
{
    uint32_t status;
    int retries = 1000;

    /* Wait for TX command to complete */
    do {
        status = uart_read(qts, SE_GENI_STATUS);
        if (!(status & M_GENI_CMD_ACTIVE)) {
            break;
        }
        g_usleep(100);
    } while (--retries > 0);

    g_assert(retries > 0);
}

/* Test firmware revision register */
static void test_fw_revision(void)
{
    QTestState *qts = qtest_init("-M qcs6490");

    uint32_t fw_rev = uart_read(qts, GENI_FW_REVISION_RO);
    g_assert((fw_rev >> FW_REV_PROTOCOL_SHFT) == GENI_SE_UART);

    qtest_quit(qts);
}

/* Test hardware parameters */
static void test_hw_params(void)
{
    QTestState *qts = qtest_init("-M qcs6490");

    uint32_t hw_param0, hw_param1;
    uint32_t tx_fifo_depth, rx_fifo_depth;
    uint32_t tx_fifo_width, rx_fifo_width;

    hw_param0 = uart_read(qts, SE_HW_PARAM_0);
    hw_param1 = uart_read(qts, SE_HW_PARAM_1);

    tx_fifo_width = (hw_param0 & TX_FIFO_WIDTH_MSK) >> TX_FIFO_WIDTH_SHFT;
    tx_fifo_depth = (hw_param0 & TX_FIFO_DEPTH_MSK_256_BYTES) >>
                    TX_FIFO_DEPTH_SHFT;

    rx_fifo_width = (hw_param1 & RX_FIFO_WIDTH_MSK) >> RX_FIFO_WIDTH_SHFT;
    rx_fifo_depth = (hw_param1 & RX_FIFO_DEPTH_MSK_256_BYTES) >>
                    RX_FIFO_DEPTH_SHFT;

    g_assert(tx_fifo_width == UART_FIFO_WIDTH_BITS);
    g_assert(tx_fifo_depth == UART_FIFO_DEPTH_WORDS);
    g_assert(rx_fifo_width == UART_FIFO_WIDTH_BITS);
    g_assert(rx_fifo_depth == UART_FIFO_DEPTH_WORDS);

    qtest_quit(qts);
}

/* Test basic UART initialization sequence from Linux driver */
static void test_uart_init(void)
{
    QTestState *qts = qtest_init("-M qcs6490");


    /* Clear and disable all interrupts first */
    uart_write(qts, SE_GENI_M_IRQ_CLEAR, 0xffffffff);
    uart_write(qts, SE_GENI_S_IRQ_CLEAR, 0xffffffff);
    uart_write(qts, SE_GENI_M_IRQ_EN, 0);
    uart_write(qts, SE_GENI_S_IRQ_EN, 0);

    /* Configure watermarks */
    uart_write(qts, SE_GENI_TX_WATERMARK_REG, DEF_TX_WM);
    uart_write(qts, SE_GENI_RX_WATERMARK_REG, UART_RX_WM);
    uart_write(qts, SE_GENI_RX_RFR_WATERMARK_REG, UART_RX_WM);

    /* Enable common interrupts */
    uint32_t m_irq_en = M_CMD_DONE_EN | M_TX_FIFO_WATERMARK_EN |
                        M_RX_FIFO_WATERMARK_EN | M_RX_FIFO_LAST_EN;
    uart_write(qts, SE_GENI_M_IRQ_EN, m_irq_en);

    /* Verify configuration */
    g_assert(uart_read(qts, SE_GENI_TX_WATERMARK_REG) == DEF_TX_WM);
    g_assert(uart_read(qts, SE_GENI_RX_WATERMARK_REG) == UART_RX_WM);
    g_assert(uart_read(qts, SE_GENI_M_IRQ_EN) == m_irq_en);

    qtest_quit(qts);
}

/* Test UART configuration sequence */
static void test_uart_config(void)
{
    QTestState *qts = qtest_init("-M qcs6490");


    /* Configure word length (8 bits) */
    uart_write(qts, SE_UART_TX_WORD_LEN, 8);
    uart_write(qts, SE_UART_RX_WORD_LEN, 8);

    /* Configure stop bits (1 stop bit) */
    uart_write(qts, SE_UART_TX_STOP_BIT_LEN, TX_STOP_BIT_LEN_1);

    /* Disable parity */
    uart_write(qts, SE_UART_TX_TRANS_CFG, 0);
    uart_write(qts, SE_UART_RX_TRANS_CFG, 0);

    /* Configure stale timeout */
    uart_write(qts, SE_UART_RX_STALE_CNT, STALE_TIMEOUT);

    /* Configure packing for 8-bit words */
    uart_write(qts, SE_GENI_TX_PACKING_CFG0, 0x0);
    uart_write(qts, SE_GENI_TX_PACKING_CFG1, 0x0);
    uart_write(qts, SE_GENI_RX_PACKING_CFG0, 0x0);
    uart_write(qts, SE_GENI_RX_PACKING_CFG1, 0x0);

    /* Verify configuration */
    g_assert(uart_read(qts, SE_UART_TX_WORD_LEN) == 8);
    g_assert(uart_read(qts, SE_UART_RX_WORD_LEN) == 8);
    g_assert(uart_read(qts, SE_UART_TX_STOP_BIT_LEN) ==
             TX_STOP_BIT_LEN_1);
    g_assert(uart_read(qts, SE_UART_RX_STALE_CNT) == STALE_TIMEOUT);

    qtest_quit(qts);
}

/* Test basic TX operation */
static void test_uart_tx(void)
{
    QTestState *qts = qtest_init("-M qcs6490");

    uint32_t tx_data[] = { 0x48454C4C, 0x4F000000 }; /* "HELLO" */
    uint32_t cmd;
    int i;

    /* Initialize UART */
    uart_write(qts, SE_GENI_M_IRQ_CLEAR, 0xffffffff);
    uart_write(qts, SE_GENI_M_IRQ_EN, M_CMD_DONE_EN | M_TX_FIFO_WATERMARK_EN);
    uart_write(qts, SE_GENI_TX_WATERMARK_REG, DEF_TX_WM);
    uart_write(qts, SE_UART_TX_WORD_LEN, 8);
    uart_write(qts, SE_UART_TX_STOP_BIT_LEN, TX_STOP_BIT_LEN_1);

    /* Write data to TX FIFO */
    for (i = 0; i < 2; i++) {
        uart_write(qts, SE_GENI_TX_FIFOn, tx_data[i]);
    }

    /* Start TX command with length = 5 bytes */
    cmd = (UART_START_TX << M_OPCODE_SHFT) | 5;
    uart_write(qts, SE_GENI_M_CMD0, cmd);

    /* Wait for TX to complete */
    uart_wait_for_tx_complete(qts);

    /* Check command done interrupt */
    g_assert(uart_read(qts, SE_GENI_M_IRQ_STATUS) & M_CMD_DONE_EN);

    /* Clear interrupt */
    uart_write(qts, SE_GENI_M_IRQ_CLEAR, M_CMD_DONE_EN);

    qtest_quit(qts);
}

/* Test RX operation */
static void test_uart_rx(void)
{
    QTestState *qts = qtest_init("-M qcs6490");

    uint32_t cmd;

    /* Initialize UART for RX */
    uart_write(qts, SE_GENI_M_IRQ_CLEAR, 0xffffffff);
    uart_write(qts, SE_GENI_S_IRQ_CLEAR, 0xffffffff);
    uart_write(qts, SE_GENI_M_IRQ_EN,
               M_RX_FIFO_WATERMARK_EN | M_RX_FIFO_LAST_EN);
    uart_write(qts, SE_GENI_RX_WATERMARK_REG, UART_RX_WM);
    uart_write(qts, SE_UART_RX_WORD_LEN, 8);

    /* Start RX command */
    cmd = (UART_START_READ << M_OPCODE_SHFT) | UART_PARAM_RFR_OPEN;
    uart_write(qts, SE_GENI_S_CMD0, cmd);

    /* Verify RX is active */
    g_assert(uart_read(qts, SE_GENI_STATUS) & S_GENI_CMD_ACTIVE);

    qtest_quit(qts);
}

/* Test interrupt handling */
static void test_uart_irq(void)
{
    QTestState *qts = qtest_init("-M qcs6490");

    uint32_t irq_status;

    /* Enable specific interrupts */
    uart_write(qts, SE_GENI_M_IRQ_EN, M_CMD_DONE_EN | M_TX_FIFO_WATERMARK_EN);

    /* Trigger TX watermark by starting empty TX */
    uint32_t cmd = (UART_START_TX << M_OPCODE_SHFT) | 0;
    uart_write(qts, SE_GENI_M_CMD0, cmd);

    /* Check interrupt status */
    irq_status = uart_read(qts, SE_GENI_M_IRQ_STATUS);
    g_assert(irq_status & M_TX_FIFO_WATERMARK_EN);

    /* Clear interrupt */
    uart_write(qts, SE_GENI_M_IRQ_CLEAR, M_TX_FIFO_WATERMARK_EN);
    g_assert((uart_read(qts, SE_GENI_M_IRQ_STATUS) &
              M_TX_FIFO_WATERMARK_EN) == 0);

    qtest_quit(qts);
}

/* Test FIFO status registers */
static void test_fifo_status(void)
{
    QTestState *qts = qtest_init("-M qcs6490");

    uint32_t status;

    /* Initially FIFOs should be empty */
    status = uart_read(qts, SE_GENI_TX_FIFO_STATUS);
    g_assert((status & TX_FIFO_WC) == 0);

    status = uart_read(qts, SE_GENI_RX_FIFO_STATUS);
    g_assert((status & RX_FIFO_WC_MSK) == 0);

    /* Write to TX FIFO and check status */
    uart_write(qts, SE_GENI_TX_FIFOn, 0x12345678);
    status = uart_read(qts, SE_GENI_TX_FIFO_STATUS);
    g_assert((status & TX_FIFO_WC) == 1);

    qtest_quit(qts);
}

/* Test loopback mode (FIFO) */
static void test_uart_loopback_fifo(void)
{
    QTestState *qts = qtest_init("-M qcs6490");

    uint32_t tx_data = 0xDEADBEEF;
    uint32_t rx_data;
    uint32_t cmd;

    /* Ensure FIFO mode (DMA disabled) */
    uart_write(qts, SE_GENI_DMA_MODE_EN, 0);

    /* Configure loopback */
    uart_write(qts, SE_UART_LOOPBACK_CFG, 0x1);

    /* Initialize UART */
    uart_write(qts, SE_GENI_M_IRQ_CLEAR, 0xffffffff);
    uart_write(qts, SE_GENI_M_IRQ_EN, M_CMD_DONE_EN | M_RX_FIFO_WATERMARK_EN);
    uart_write(qts, SE_UART_TX_WORD_LEN, 32);
    uart_write(qts, SE_UART_RX_WORD_LEN, 32);

    /* Start RX */
    cmd = (UART_START_READ << M_OPCODE_SHFT) | UART_PARAM_RFR_OPEN;
    uart_write(qts, SE_GENI_S_CMD0, cmd);

    /* TX data */
    uart_write(qts, SE_GENI_TX_FIFOn, tx_data);
    cmd = (UART_START_TX << M_OPCODE_SHFT) | 4;
    uart_write(qts, SE_GENI_M_CMD0, cmd);

    /* Wait for TX complete */
    uart_wait_for_tx_complete(qts);

    /* Check RX FIFO has data */
    g_assert((uart_read(qts, SE_GENI_RX_FIFO_STATUS) &
              RX_FIFO_WC_MSK) > 0);

    /* Read RX data */
    rx_data = uart_read(qts, SE_GENI_RX_FIFOn);
    g_assert(rx_data == tx_data);

    qtest_quit(qts);
}

/* Test DMA mode */
static void test_uart_dma_mode(void)
{
    QTestState *qts = qtest_init("-M qcs6490");

    uint32_t cmd;
    uint32_t dma_status;

    /* Enable DMA mode */
    uart_write(qts, SE_GENI_DMA_MODE_EN, 1);

    /* Verify DMA mode is enabled */
    dma_status = uart_read(qts, SE_GENI_DMA_MODE_EN);
    g_assert((dma_status & 1) == 1);

    /* Initialize UART for DMA */
    uart_write(qts, SE_GENI_M_IRQ_CLEAR, 0xffffffff);
    uart_write(qts, SE_GENI_M_IRQ_EN, M_CMD_DONE_EN);
    uart_write(qts, SE_UART_TX_WORD_LEN, 8);

    /* Start DMA TX command with 10 bytes */
    cmd = (UART_START_TX << M_OPCODE_SHFT) | 10;
    uart_write(qts, SE_GENI_M_CMD0, cmd);

    /* In DMA mode, command should complete immediately (simulated) */
    uart_wait_for_tx_complete(qts);

    /* Check command done interrupt */
    g_assert(uart_read(qts, SE_GENI_M_IRQ_STATUS) & M_CMD_DONE_EN);

    /* Disable DMA mode */
    uart_write(qts, SE_GENI_DMA_MODE_EN, 0);
    g_assert((uart_read(qts, SE_GENI_DMA_MODE_EN) & 1) == 0);

    qtest_quit(qts);
}

/* Test switching between FIFO and DMA modes */
static void test_uart_mode_switching(void)
{
    QTestState *qts = qtest_init("-M qcs6490");

    uint32_t tx_data = 0x12345678;
    uint32_t cmd;

    /* Start in FIFO mode */
    uart_write(qts, SE_GENI_DMA_MODE_EN, 0);
    uart_write(qts, SE_GENI_M_IRQ_CLEAR, 0xffffffff);
    uart_write(qts, SE_GENI_M_IRQ_EN, M_CMD_DONE_EN | M_TX_FIFO_WATERMARK_EN);

    /* Test FIFO TX */
    uart_write(qts, SE_GENI_TX_FIFOn, tx_data);
    cmd = (UART_START_TX << M_OPCODE_SHFT) | 4;
    uart_write(qts, SE_GENI_M_CMD0, cmd);
    uart_wait_for_tx_complete(qts);

    /* Clear interrupts */
    uart_write(qts, SE_GENI_M_IRQ_CLEAR, 0xffffffff);

    /* Switch to DMA mode */
    uart_write(qts, SE_GENI_DMA_MODE_EN, 1);
    g_assert((uart_read(qts, SE_GENI_DMA_MODE_EN) & 1) == 1);

    /* Test DMA TX */
    cmd = (UART_START_TX << M_OPCODE_SHFT) | 8;
    uart_write(qts, SE_GENI_M_CMD0, cmd);
    uart_wait_for_tx_complete(qts);
    g_assert(uart_read(qts, SE_GENI_M_IRQ_STATUS) & M_CMD_DONE_EN);

    /* Switch back to FIFO mode */
    uart_write(qts, SE_GENI_DMA_MODE_EN, 0);
    g_assert((uart_read(qts, SE_GENI_DMA_MODE_EN) & 1) == 0);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/qup-geni-uart/fw-revision", test_fw_revision);
    qtest_add_func("/qup-geni-uart/hw-params", test_hw_params);
    qtest_add_func("/qup-geni-uart/uart-init", test_uart_init);
    qtest_add_func("/qup-geni-uart/uart-config", test_uart_config);
    qtest_add_func("/qup-geni-uart/uart-tx", test_uart_tx);
    qtest_add_func("/qup-geni-uart/uart-rx", test_uart_rx);
    qtest_add_func("/qup-geni-uart/uart-irq", test_uart_irq);
    qtest_add_func("/qup-geni-uart/fifo-status", test_fifo_status);
    qtest_add_func("/qup-geni-uart/uart-loopback-fifo",
                   test_uart_loopback_fifo);
    qtest_add_func("/qup-geni-uart/uart-dma-mode", test_uart_dma_mode);
    qtest_add_func("/qup-geni-uart/uart-mode-switching",
                   test_uart_mode_switching);

    return g_test_run();
}
