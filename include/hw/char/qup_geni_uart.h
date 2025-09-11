/*
 * Qualcomm GENI UART (QUP UART)
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_CHAR_QCOM_GENI_UART_H
#define HW_CHAR_QCOM_GENI_UART_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "qom/object.h"
#include "qemu/bitops.h"

#define GENMASK(h, l) \
    (((~0UL) - (1UL << (l)) + 1) & (~0UL >> (BITS_PER_LONG - 1 - (h))))

#define TYPE_QUP_GENI_UART "qup-geni-uart"
OBJECT_DECLARE_SIMPLE_TYPE(QupGeniUartState, QUP_GENI_UART)

/* Hardware version for QUP >= 3.10 */
#define QUP_HW_VER_REG_VAL      0x30100000

/* Common SE registers */
#define GENI_FORCE_DEFAULT_REG          0x020
#define GENI_OUTPUT_CTRL                0x024
#define SE_GENI_STATUS                  0x040
#define GENI_SER_M_CLK_CFG             0x048
#define GENI_SER_S_CLK_CFG             0x04c
#define GENI_IF_DISABLE_RO             0x064
#define GENI_FW_REVISION_RO            0x068
#define SE_GENI_CLK_SEL                0x07c
#define SE_GENI_CFG_SEQ_START          0x084
#define SE_GENI_DMA_MODE_EN            0x258
#define SE_GENI_TX_PACKING_CFG0        0x260
#define SE_GENI_TX_PACKING_CFG1        0x264
#define SE_GENI_RX_PACKING_CFG0        0x284
#define SE_GENI_RX_PACKING_CFG1        0x288
#define SE_GENI_M_CMD0                 0x600
#define SE_GENI_M_CMD_CTRL_REG         0x604
#define SE_GENI_M_IRQ_STATUS           0x610
#define SE_GENI_M_IRQ_EN               0x614
#define SE_GENI_M_IRQ_CLEAR            0x618
#define SE_GENI_M_IRQ_EN_SET           0x61c
#define SE_GENI_M_IRQ_EN_CLEAR         0x620
#define SE_GENI_S_CMD0                 0x630
#define SE_GENI_S_CMD_CTRL_REG         0x634
#define SE_GENI_S_IRQ_STATUS           0x640
#define SE_GENI_S_IRQ_EN               0x644
#define SE_GENI_S_IRQ_CLEAR            0x648
#define SE_GENI_S_IRQ_EN_SET           0x64c
#define SE_GENI_S_IRQ_EN_CLEAR         0x650
#define SE_GENI_TX_FIFOn               0x700
#define SE_GENI_RX_FIFOn               0x780
#define SE_GENI_TX_FIFO_STATUS         0x800
#define SE_GENI_RX_FIFO_STATUS         0x804
#define SE_GENI_TX_WATERMARK_REG       0x80c
#define SE_GENI_RX_WATERMARK_REG       0x810
#define SE_GENI_RX_RFR_WATERMARK_REG   0x814
#define SE_GENI_IOS                    0x908
#define SE_GENI_M_GP_LENGTH            0x910
#define SE_GENI_S_GP_LENGTH            0x914
#define SE_HW_PARAM_0                  0xe24
#define SE_HW_PARAM_1                  0xe28

/* UART specific registers */
#define SE_UART_LOOPBACK_CFG           0x22c
#define SE_UART_IO_MACRO_CTRL          0x240
#define SE_UART_TX_TRANS_CFG           0x25c
#define SE_UART_TX_WORD_LEN            0x268
#define SE_UART_TX_STOP_BIT_LEN        0x26c
#define SE_UART_TX_TRANS_LEN           0x270
#define SE_UART_RX_TRANS_CFG           0x280
#define SE_UART_RX_WORD_LEN            0x28c
#define SE_UART_RX_STALE_CNT           0x294
#define SE_UART_TX_PARITY_CFG          0x2a4
#define SE_UART_RX_PARITY_CFG          0x2a8
#define SE_UART_MANUAL_RFR             0x2ac

/* SE_GENI_STATUS */
#define M_GENI_CMD_ACTIVE              BIT(0)
#define S_GENI_CMD_ACTIVE              BIT(12)

/* GENI_FW_REVISION_RO */
#define FW_REV_PROTOCOL_MSK            GENMASK(15, 8)
#define FW_REV_PROTOCOL_SHFT           8
#define GENI_SE_UART                   2

/* SE_GENI_M_CMD0 */
#define M_OPCODE_MSK                   GENMASK(31, 27)
#define M_OPCODE_SHFT                  27
#define M_PARAMS_MSK                   GENMASK(26, 0)

/* UART M_CMD OP codes */
#define UART_START_TX                  0x1

/* UART S_CMD OP codes */
#define UART_START_READ                0x1
#define UART_PARAM                     0x1
#define UART_PARAM_RFR_OPEN            BIT(7)

/* SE_UART_TX_TRANS_CFG */
#define UART_TX_PAR_EN                 BIT(0)
#define UART_CTS_MASK                  BIT(1)

/* SE_UART_TX_STOP_BIT_LEN */
#define TX_STOP_BIT_LEN_1              0
#define TX_STOP_BIT_LEN_2              2

/* SE_UART_RX_TRANS_CFG */
#define UART_RX_PAR_EN                 BIT(3)

/* SE_UART_RX_WORD_LEN */
#define RX_WORD_LEN_MASK               GENMASK(9, 0)

/* SE_UART_RX_STALE_CNT */
#define RX_STALE_CNT                   GENMASK(23, 0)

/* SE_UART_TX/RX_PARITY_CFG */
#define PAR_CALC_EN                    BIT(0)
#define PAR_EVEN                       0x00
#define PAR_ODD                        0x01
#define PAR_SPACE                      0x10

/* SE_GENI_M_IRQ_EN */
#define M_CMD_DONE_EN                  BIT(0)
#define M_CMD_OVERRUN_EN               BIT(1)
#define M_ILLEGAL_CMD_EN               BIT(2)
#define M_CMD_FAILURE_EN               BIT(3)
#define M_CMD_CANCEL_EN                BIT(4)
#define M_CMD_ABORT_EN                 BIT(5)
#define M_TIMESTAMP_EN                 BIT(6)
#define M_RX_IRQ_EN                    BIT(7)
#define M_GP_SYNC_IRQ_0_EN             BIT(8)
#define M_GP_IRQ_0_EN                  BIT(9)
#define M_GP_IRQ_1_EN                  BIT(10)
#define M_GP_IRQ_2_EN                  BIT(11)
#define M_GP_IRQ_3_EN                  BIT(12)
#define M_GP_IRQ_4_EN                  BIT(13)
#define M_GP_IRQ_5_EN                  BIT(14)
#define M_TX_FIFO_NOT_EMPTY_EN         BIT(21)
#define M_IO_DATA_DEASSERT_EN          BIT(22)
#define M_IO_DATA_ASSERT_EN            BIT(23)
#define M_RX_FIFO_RD_ERR_EN            BIT(24)
#define M_RX_FIFO_WR_ERR_EN            BIT(25)
#define M_RX_FIFO_WATERMARK_EN         BIT(26)
#define M_RX_FIFO_LAST_EN              BIT(27)
#define M_TX_FIFO_RD_ERR_EN            BIT(28)
#define M_TX_FIFO_WR_ERR_EN            BIT(29)
#define M_TX_FIFO_WATERMARK_EN         BIT(30)
#define M_SEC_IRQ_EN                   BIT(31)

/* GENI_TX_FIFO_STATUS */
#define TX_FIFO_WC                     GENMASK(27, 0)

/* GENI_RX_FIFO_STATUS */
#define RX_LAST                        BIT(31)
#define RX_LAST_BYTE_VALID_MSK         GENMASK(30, 28)
#define RX_LAST_BYTE_VALID_SHFT        28
#define RX_FIFO_WC_MSK                 GENMASK(24, 0)

/* SE_HW_PARAM_0 */
#define TX_FIFO_WIDTH_MSK              GENMASK(29, 24)
#define TX_FIFO_WIDTH_SHFT             24
#define TX_FIFO_DEPTH_MSK_256_BYTES    GENMASK(23, 16)
#define TX_FIFO_DEPTH_SHFT             16

/* SE_HW_PARAM_1 */
#define RX_FIFO_WIDTH_MSK              GENMASK(29, 24)
#define RX_FIFO_WIDTH_SHFT             24
#define RX_FIFO_DEPTH_MSK_256_BYTES    GENMASK(23, 16)
#define RX_FIFO_DEPTH_SHFT             16

/* Hardware parameters */
#define UART_FIFO_DEPTH_WORDS          64  /* 256 bytes / 4 bytes per word */
#define UART_FIFO_WIDTH_BITS           32
#define UART_OVERSAMPLING              32
#define STALE_TIMEOUT                  16
#define DEFAULT_BITS_PER_CHAR          10
#define DEF_TX_WM                      2
#define UART_RX_WM                     2

/* DMA Register offsets */
#define SE_DMA_TX_PTR_L                0xc30
#define SE_DMA_TX_PTR_H                0xc34
#define SE_DMA_TX_ATTR                 0xc38
#define SE_DMA_TX_LEN                  0xc3c
#define SE_DMA_TX_IRQ_STAT             0xc40
#define SE_DMA_TX_IRQ_CLR              0xc44
#define SE_DMA_TX_IRQ_EN               0xc48
#define SE_DMA_TX_IRQ_EN_SET           0xc4c
#define SE_DMA_TX_IRQ_EN_CLR           0xc50
#define SE_DMA_RX_PTR_L                0xd30
#define SE_DMA_RX_PTR_H                0xd34
#define SE_DMA_RX_ATTR                 0xd38
#define SE_DMA_RX_LEN                  0xd3c
#define SE_DMA_RX_IRQ_STAT             0xd40
#define SE_DMA_RX_IRQ_CLR              0xd44
#define SE_DMA_RX_IRQ_EN               0xd48
#define SE_DMA_RX_IRQ_EN_SET           0xd4c
#define SE_DMA_RX_IRQ_EN_CLR           0xd50

/* DMA Interrupt Enable/Status fields */
#define DMA_DONE_EN                    BIT(0)
#define DMA_EOT_EN                     BIT(1)
#define DMA_AHB_ERR_EN                 BIT(2)

/* DMA Attribute fields */
#define DMA_EOT_BUF                    BIT(0)

/* Non-UART protocol command opcodes (for detection) */
/* SPI command opcodes */
#define SPI_TX_ONLY                    0x1
#define SPI_RX_ONLY                    0x2
#define SPI_TX_RX                      0x7
#define SPI_CS_ASSERT                  0x8
#define SPI_CS_DEASSERT                0x9
#define SPI_SCK_ONLY                   0xa

/* I2C command opcodes */
#define I2C_WRITE                      0x1
#define I2C_READ                       0x2
#define I2C_WRITE_READ                 0x3
#define I2C_ADDR_ONLY                  0x4
#define I2C_BUS_CLEAR                  0x6
#define I2C_STOP_ON_BUS                0x7

/*
 * Note: The actual QupGeniUartState struct is implemented in Rust.
 * This opaque declaration allows C code to work with pointers to the type.
 */
typedef struct QupGeniUartState QupGeniUartState;

/* QUP GENI UART creation function (implemented in Rust) */
DeviceState *qup_geni_uart_create(uint64_t addr, qemu_irq irq, Chardev *chr);

#endif /* HW_CHAR_QCOM_GENI_UART_H */
