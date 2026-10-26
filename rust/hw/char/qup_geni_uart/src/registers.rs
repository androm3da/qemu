// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0-or-later

//! Register definitions for QUP GENI UART
//!
//! This module contains the register layout and bit field definitions
//! for the Qualcomm QUP GENI UART device.

pub const fn genmask(h: u32, l: u32) -> u32 {
    ((!0u32) - (1u32 << l) + 1) & (!0u32 >> (32 - 1 - h))
}

// Register offsets
#[allow(non_camel_case_types)]
#[repr(u64)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, common::TryInto)]
pub enum RegisterOffset {
    // Common SE registers
    GENI_FORCE_DEFAULT = 0x020,
    GENI_OUTPUT_CTRL = 0x024,
    SE_GENI_STATUS = 0x040,
    GENI_SER_M_CLK_CFG = 0x048,
    GENI_SER_S_CLK_CFG = 0x04c,
    GENI_IF_DISABLE_RO = 0x064,
    GENI_FW_REVISION_RO = 0x068,
    SE_GENI_CLK_SEL = 0x07c,
    SE_GENI_CFG_SEQ_START = 0x084,
    SE_GENI_DMA_MODE_EN = 0x258,
    SE_GENI_TX_PACKING_CFG0 = 0x260,
    SE_GENI_TX_PACKING_CFG1 = 0x264,
    SE_GENI_RX_PACKING_CFG0 = 0x284,
    SE_GENI_RX_PACKING_CFG1 = 0x288,
    SE_GENI_M_CMD0 = 0x600,
    SE_GENI_M_CMD_CTRL_REG = 0x604,
    SE_GENI_M_IRQ_STATUS = 0x610,
    SE_GENI_M_IRQ_EN = 0x614,
    SE_GENI_M_IRQ_CLEAR = 0x618,
    SE_GENI_M_IRQ_EN_SET = 0x61c,
    SE_GENI_M_IRQ_EN_CLEAR = 0x620,
    SE_GENI_S_CMD0 = 0x630,
    SE_GENI_S_CMD_CTRL_REG = 0x634,
    SE_GENI_S_IRQ_STATUS = 0x640,
    SE_GENI_S_IRQ_EN = 0x644,
    SE_GENI_S_IRQ_CLEAR = 0x648,
    SE_GENI_S_IRQ_EN_SET = 0x64c,
    SE_GENI_S_IRQ_EN_CLEAR = 0x650,
    SE_GENI_TX_FIFO_N = 0x700,
    SE_GENI_RX_FIFO_N = 0x780,
    SE_GENI_TX_FIFO_STATUS = 0x800,
    SE_GENI_RX_FIFO_STATUS = 0x804,
    SE_GENI_TX_WATERMARK_REG = 0x80c,
    SE_GENI_RX_WATERMARK_REG = 0x810,
    SE_GENI_RX_RFR_WATERMARK_REG = 0x814,
    SE_GENI_IOS = 0x908,
    SE_GENI_M_GP_LENGTH = 0x910,
    SE_GENI_S_GP_LENGTH = 0x914,
    SE_GSI_EVENT_EN = 0xe18,
    SE_IRQ_EN = 0xe1c,
    SE_HW_PARAM0 = 0xe24,
    SE_HW_PARAM1 = 0xe28,

    // DMA registers
    SE_DMA_TX_PTR_L = 0xc30,
    SE_DMA_TX_PTR_H = 0xc34,
    SE_DMA_TX_ATTR = 0xc38,
    SE_DMA_TX_LEN = 0xc3c,
    SE_DMA_TX_IRQ_EN = 0xc48,
    SE_DMA_TX_IRQ_EN_SET = 0xc4c,
    SE_DMA_TX_IRQ_EN_CLR = 0xc50,
    SE_DMA_TX_FSM_RST = 0xc58,
    SE_DMA_TX_IRQ_STAT = 0xc40,
    SE_DMA_TX_IRQ_CLR = 0xc44,
    SE_DMA_RX_PTR_L = 0xd30,
    SE_DMA_RX_PTR_H = 0xd34,
    SE_DMA_RX_ATTR = 0xd38,
    SE_DMA_RX_LEN = 0xd3c,
    SE_DMA_RX_IRQ_EN = 0xd48,
    SE_DMA_RX_IRQ_EN_SET = 0xd4c,
    SE_DMA_RX_IRQ_EN_CLR = 0xd50,
    SE_DMA_RX_LEN_IN = 0xd54,
    SE_DMA_RX_FSM_RST = 0xd58,
    SE_DMA_RX_IRQ_STAT = 0xd40,
    SE_DMA_RX_IRQ_CLR = 0xd44,

    // UART specific registers
    SE_UART_LOOPBACK_CFG = 0x22c,
    SE_UART_IO_MACRO_CTRL = 0x240,
    SE_UART_TX_TRANS_CFG = 0x25c,
    SE_UART_TX_WORD_LEN = 0x268,
    SE_UART_TX_STOP_BIT_LEN = 0x26c,
    SE_UART_TX_TRANS_LEN = 0x270,
    SE_UART_RX_TRANS_CFG = 0x280,
    SE_UART_RX_WORD_LEN = 0x28c,
    SE_UART_RX_STALE_CNT = 0x294,
    SE_UART_TX_PARITY_CFG = 0x2a4,
    SE_UART_RX_PARITY_CFG = 0x2a8,
    SE_UART_MANUAL_RFR = 0x2ac,
}

// Status register fields
pub const M_GENI_CMD_ACTIVE: u32 = 1 << 0;
pub const S_GENI_CMD_ACTIVE: u32 = 1 << 12;

// GENI_M_CMD_CTRL_REG fields
pub const M_GENI_CMD_ABORT: u32 = 1 << 1;
pub const M_GENI_CMD_CANCEL: u32 = 1 << 2;

// GENI_S_CMD_CTRL_REG fields
pub const S_GENI_CMD_ABORT: u32 = 1 << 1;
pub const S_GENI_CMD_CANCEL: u32 = 1 << 2;

// FW Revision fields
pub const FW_REV_PROTOCOL_SHFT: u32 = 8;

// Protocol constants (from Linux kernel enum geni_se_protocol_type)
pub const GENI_SE_UART: u32 = 2;

// M_CMD0 fields
pub const M_OPCODE_MSK: u32 = genmask(31, 27);
pub const M_OPCODE_SHFT: u32 = 27;

// UART M_CMD OP codes
pub const UART_START_TX: u32 = 0x1;
pub const UART_ABORT: u32 = 0x2;

// UART S_CMD OP codes
pub const UART_START_READ: u32 = 0x1;
pub const UART_ABORT_READ: u32 = 0x2;

// M interrupt enable fields
pub const M_CMD_DONE_EN: u32 = 1 << 0;
pub const S_CMD_DONE_EN: u32 = 1 << 0;
pub const S_CMD_CANCEL_EN: u32 = 1 << 4;
pub const S_CMD_ABORT_EN: u32 = 1 << 5;
pub const M_CMD_CANCEL_EN: u32 = 1 << 4;
pub const M_CMD_ABORT_EN: u32 = 1 << 5;
pub const M_TX_FIFO_WATERMARK_EN: u32 = 1 << 30;

// S interrupt enable/status fields
pub const S_RX_FIFO_WR_ERR_EN: u32 = 1 << 25;
pub const S_RX_FIFO_WATERMARK_EN: u32 = 1 << 26;
pub const S_RX_FIFO_LAST_EN: u32 = 1 << 27;

// RX FIFO status fields
pub const RX_LAST: u32 = 1 << 31;
pub const RX_LAST_BYTE_VALID_MSK: u32 = genmask(30, 28);
pub const RX_LAST_BYTE_VALID_SHFT: u32 = 28;
pub const RX_FIFO_WC_MSK: u32 = genmask(24, 0);

// HW parameter fields
pub const TX_FIFO_WIDTH_SHFT: u32 = 24;
pub const TX_FIFO_DEPTH_SHFT: u32 = 16;
pub const RX_FIFO_WIDTH_SHFT: u32 = 24;
pub const RX_FIFO_DEPTH_SHFT: u32 = 16;

// Hardware parameters
pub const BYTES_PER_FIFO_WORD: u32 = 4;
pub const UART_FIFO_DEPTH_WORDS: u32 = 64;
pub const UART_FIFO_WIDTH_BITS: u32 = 32;
pub const STALE_TIMEOUT: u32 = 160;
pub const DEF_TX_WM: u32 = 2;
pub const UART_RX_WM: u32 = 2;

// DMA interrupt enable/status fields
pub const DMA_DONE_EN: u32 = 1 << 0;
pub const DMA_EOT_EN: u32 = 1 << 1;
pub const DMA_AHB_ERR_EN: u32 = 1 << 2;
pub const DMA_RESET_DONE_EN: u32 = 1 << 3;
