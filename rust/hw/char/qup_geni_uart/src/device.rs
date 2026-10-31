// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0-or-later

use std::{
    ffi::{c_void, CStr},
    io::ErrorKind,
};

use bql::prelude::*;
use chardev::prelude::*;
use common::prelude::*;
use hwcore::prelude::*;
use migration::{self, prelude::*};
use qom::prelude::*;
use system::prelude::*;
use util::{self, log::Log, log_mask_ln};

use crate::registers::{self, RegisterOffset, UART_FIFO_DEPTH_WORDS};

pub const QUP_FIFO_DEPTH: u32 = UART_FIFO_DEPTH_WORDS;

const TX_QUEUE_BYTES: usize = (UART_FIFO_DEPTH_WORDS * registers::BYTES_PER_FIFO_WORD) as usize;

const QUP_GENI_UART_MMIO_SIZE: u64 = 0x6000;

#[repr(transparent)]
#[derive(Debug)]
pub struct Fifo([u32; QUP_FIFO_DEPTH as usize]);

impl Default for Fifo {
    fn default() -> Self {
        Self([0u32; QUP_FIFO_DEPTH as usize])
    }
}
impl_vmstate_forward!(Fifo);

impl std::ops::IndexMut<u32> for Fifo {
    fn index_mut(&mut self, idx: u32) -> &mut Self::Output {
        &mut self.0[idx as usize]
    }
}

impl std::ops::Index<u32> for Fifo {
    type Output = u32;

    fn index(&self, idx: u32) -> &Self::Output {
        &self.0[idx as usize]
    }
}

/// Bytes written to the TX FIFO that the backend has not accepted yet.
#[repr(transparent)]
#[derive(Debug)]
pub struct TxQueue([u8; TX_QUEUE_BYTES]);

impl Default for TxQueue {
    fn default() -> Self {
        Self([0u8; TX_QUEUE_BYTES])
    }
}
impl_vmstate_forward!(TxQueue);

impl std::ops::Deref for TxQueue {
    type Target = [u8];

    fn deref(&self) -> &[u8] {
        &self.0
    }
}

impl std::ops::DerefMut for TxQueue {
    fn deref_mut(&mut self) -> &mut [u8] {
        &mut self.0
    }
}

#[repr(C)]
#[derive(Debug, Default)]
pub struct QupGeniUartRegs {
    pub geni_status: u32,
    pub geni_m_cmd0: u32,
    pub geni_m_irq_status: u32,
    pub geni_m_irq_en: u32,
    pub geni_s_cmd0: u32,
    pub geni_s_irq_status: u32,
    pub geni_s_irq_en: u32,
    pub geni_tx_watermark: u32,
    pub geni_rx_watermark: u32,
    pub geni_rx_rfr_watermark: u32,
    pub geni_m_gp_length: u32,
    pub geni_s_gp_length: u32,
    pub geni_tx_packing_cfg0: u32,
    pub geni_tx_packing_cfg1: u32,
    pub geni_rx_packing_cfg0: u32,
    pub geni_rx_packing_cfg1: u32,

    pub uart_tx_trans_cfg: u32,
    pub uart_rx_trans_cfg: u32,
    pub uart_tx_word_len: u32,
    pub uart_rx_word_len: u32,
    pub uart_tx_stop_bit_len: u32,
    pub uart_rx_stale_cnt: u32,
    pub uart_tx_parity_cfg: u32,
    pub uart_rx_parity_cfg: u32,
    pub uart_loopback_cfg: u32,
    pub uart_io_macro_ctrl: u32,
    pub uart_manual_rfr: u32,
    pub uart_tx_trans_len: u32,

    pub tx_pending: TxQueue,
    pub tx_pending_len: u32,
    pub rx_fifo: Fifo,
    pub rx_fifo_len: u32,
    // Valid bytes in the final RX FIFO word; 0 means the word is full.
    pub rx_last_byte_cnt: u32,
    // Set once the guest observes the size of a short final word.
    pub rx_tail_closed: bool,
    pub tx_remaining: u32,

    pub tx_enabled: bool,
    pub rx_enabled: bool,
}

#[repr(C)]
#[derive(qemu_macros::Object, hwcore::Device)]
pub struct QupGeniUartState {
    pub parent_obj: ParentField<SysBusDevice>,
    pub iomem: MemoryRegion,
    #[property(rename = "chardev")]
    pub char_frontend: CharFrontend,
    pub regs: BqlRefCell<QupGeniUartRegs>,
    pub irq: InterruptSource,
    pub tx_watch: BqlCell<u32>,
}

qom_isa!(QupGeniUartState : SysBusDevice, DeviceState, Object);

unsafe impl ObjectType for QupGeniUartState {
    type Class = <SysBusDevice as ObjectType>::Class;
    const TYPE_NAME: &'static CStr = crate::TYPE_QUP_GENI_UART;
}

impl ObjectImpl for QupGeniUartState {
    type ParentType = SysBusDevice;

    const INSTANCE_INIT: Option<unsafe fn(ParentInit<Self>)> = Some(Self::init);
    const INSTANCE_POST_INIT: Option<fn(&Self)> = Some(Self::post_init);
    const CLASS_INIT: fn(&mut Self::Class) = Self::Class::class_init::<Self>;
}

impl DeviceImpl for QupGeniUartState {
    const VMSTATE: Option<VMStateDescription<Self>> = Some(VMSTATE_QUP_GENI_UART);
    const REALIZE: Option<fn(&Self) -> util::Result<()>> = Some(Self::realize);
}

impl ResettablePhasesImpl for QupGeniUartState {
    const HOLD: Option<fn(&Self, ResetType)> = Some(Self::reset_hold);
}

impl SysBusDeviceImpl for QupGeniUartState {}

impl QupGeniUartRegs {
    fn tx_pending_words(&self) -> u32 {
        self.tx_pending_len.div_ceil(registers::BYTES_PER_FIFO_WORD)
    }

    // The TX FIFO holds only what the backend has not accepted yet, so the
    // watermark is reached while fewer words than the threshold are queued.
    fn tx_watermark_latch(&mut self) {
        if self.geni_tx_watermark > self.tx_pending_words() {
            self.geni_m_irq_status |= registers::M_TX_FIFO_WATERMARK;
        } else {
            self.geni_m_irq_status &= !registers::M_TX_FIFO_WATERMARK;
        }
    }

    fn tx_queue(&mut self, data: &[u8]) {
        let len = self.tx_pending_len as usize;
        let n = data.len().min(TX_QUEUE_BYTES - len);

        self.tx_pending[len..len + n].copy_from_slice(&data[..n]);
        self.tx_pending_len += n as u32;
        if n < data.len() {
            log_mask_ln!(
                Log::GuestError,
                "QUP GENI UART: TX FIFO overflow, {} bytes dropped",
                data.len() - n
            );
        }
    }

    // A FIFO write cannot wait for a blocked backend while holding the BQL, so
    // keep what the backend refuses until it becomes writable again.
    fn tx_send(&mut self, data: &[u8], char_frontend: &CharFrontend) {
        let mut sent = 0;

        if self.tx_pending_len == 0 {
            match char_frontend.write(data) {
                // Without a backend nothing would ever drain the queue.
                Ok(0) => return,
                Ok(n) => sent = n,
                Err(err) if err.kind() == ErrorKind::WouldBlock => {}
                Err(_) => return,
            }
        }
        self.tx_queue(&data[sent..]);
    }

    fn tx_flush(&mut self, char_frontend: &CharFrontend) {
        while self.tx_pending_len > 0 {
            let len = self.tx_pending_len as usize;

            match char_frontend.write(&self.tx_pending[..len]) {
                Ok(n) if n > 0 => {
                    self.tx_pending.copy_within(n..len, 0);
                    self.tx_pending_len -= n as u32;
                }
                Err(err) if err.kind() == ErrorKind::WouldBlock => break,
                _ => self.tx_pending_len = 0,
            }
        }
        self.tx_watermark_latch();
    }

    pub fn read(&mut self, offset: RegisterOffset) -> (bool, u32) {
        use RegisterOffset::*;

        let mut update_irq = false;
        let result = match offset {
            SE_GENI_STATUS => self.geni_status,
            GENI_FW_REVISION_RO => {
                (registers::GENI_SE_UART << registers::FW_REV_PROTOCOL_SHFT) | 0x0001
            }
            SE_GENI_M_IRQ_STATUS => self.geni_m_irq_status,
            SE_GENI_M_IRQ_EN => self.geni_m_irq_en,
            SE_GENI_M_CMD0 => self.geni_m_cmd0,
            SE_GENI_S_CMD0 => self.geni_s_cmd0,
            SE_GENI_TX_WATERMARK_REG => self.geni_tx_watermark,
            SE_GENI_RX_WATERMARK_REG => self.geni_rx_watermark,
            SE_GENI_RX_RFR_WATERMARK_REG => self.geni_rx_rfr_watermark,
            SE_UART_TX_WORD_LEN => self.uart_tx_word_len,
            SE_UART_RX_WORD_LEN => self.uart_rx_word_len,
            SE_UART_TX_STOP_BIT_LEN => self.uart_tx_stop_bit_len,
            SE_UART_RX_STALE_CNT => self.uart_rx_stale_cnt,
            SE_UART_TX_TRANS_CFG => self.uart_tx_trans_cfg,
            SE_UART_RX_TRANS_CFG => self.uart_rx_trans_cfg,
            SE_UART_LOOPBACK_CFG => self.uart_loopback_cfg,
            SE_UART_IO_MACRO_CTRL => self.uart_io_macro_ctrl,
            SE_UART_MANUAL_RFR => self.uart_manual_rfr,
            SE_GENI_TX_PACKING_CFG0 => self.geni_tx_packing_cfg0,
            SE_GENI_TX_PACKING_CFG1 => self.geni_tx_packing_cfg1,
            SE_GENI_RX_PACKING_CFG0 => self.geni_rx_packing_cfg0,
            SE_GENI_RX_PACKING_CFG1 => self.geni_rx_packing_cfg1,
            SE_GENI_TX_FIFO_STATUS => self.tx_pending_words(),
            SE_GENI_RX_FIFO_STATUS => {
                let mut status = self.rx_fifo_len & registers::RX_FIFO_WC_MSK;
                // Without RX_LAST, the guest treats a short word as four bytes.
                if self.rx_fifo_len > 0 && self.rx_last_byte_cnt > 0 {
                    self.rx_tail_closed = true;
                    status |= registers::RX_LAST
                        | (self.rx_last_byte_cnt << registers::RX_LAST_BYTE_VALID_SHFT)
                            & registers::RX_LAST_BYTE_VALID_MSK;
                }
                status
            }
            SE_GENI_TX_FIFO_N => {
                log_mask_ln!(Log::GuestError, "QUP GENI UART: TX FIFO read not supported");
                0
            }
            SE_GENI_RX_FIFO_N => {
                if self.rx_fifo_len > 0 {
                    let data = self.rx_fifo[0];
                    for i in 0..self.rx_fifo_len - 1 {
                        self.rx_fifo[i] = self.rx_fifo[i + 1];
                    }
                    self.rx_fifo_len -= 1;
                    if self.rx_fifo_len < self.geni_rx_watermark {
                        self.geni_s_irq_status &= !registers::S_RX_FIFO_WATERMARK;
                    }
                    if self.rx_fifo_len == 0 {
                        self.geni_s_irq_status &= !registers::S_RX_FIFO_LAST;
                        self.rx_last_byte_cnt = 0;
                        self.rx_tail_closed = false;
                    }
                    update_irq = true;
                    data
                } else {
                    0
                }
            }
            SE_GENI_S_IRQ_STATUS => self.geni_s_irq_status,
            SE_GENI_S_IRQ_EN => self.geni_s_irq_en,
            GENI_SER_M_CLK_CFG | GENI_SER_S_CLK_CFG | SE_GENI_CLK_SEL => 0,
            GENI_OUTPUT_CTRL | GENI_FORCE_DEFAULT => 0,
            GENI_IF_DISABLE_RO => 0,
            SE_GENI_IOS => 0x3, // TX/RX lines idle high
            SE_GENI_M_GP_LENGTH => self.geni_m_gp_length,
            SE_GENI_S_GP_LENGTH => self.geni_s_gp_length,
            SE_UART_TX_TRANS_LEN => self.uart_tx_trans_len,
            SE_UART_TX_PARITY_CFG => self.uart_tx_parity_cfg,
            SE_UART_RX_PARITY_CFG => self.uart_rx_parity_cfg,
            SE_GSI_EVENT_EN | SE_IRQ_EN => 0,
            SE_HW_PARAM0 => {
                (registers::UART_FIFO_WIDTH_BITS << registers::TX_FIFO_WIDTH_SHFT)
                    | (registers::UART_FIFO_DEPTH_WORDS << registers::TX_FIFO_DEPTH_SHFT)
            }
            SE_HW_PARAM1 => {
                (registers::UART_FIFO_WIDTH_BITS << registers::RX_FIFO_WIDTH_SHFT)
                    | (registers::UART_FIFO_DEPTH_WORDS << registers::RX_FIFO_DEPTH_SHFT)
            }
            _ => {
                log_mask_ln!(
                    Log::Unimp,
                    "QUP GENI UART: Unimplemented read from offset {:x}",
                    offset as u32
                );
                0
            }
        };
        (update_irq, result)
    }

    pub fn write(
        &mut self,
        offset: RegisterOffset,
        value: u32,
        char_frontend: &CharFrontend,
    ) -> bool {
        use RegisterOffset::*;

        match offset {
            SE_GENI_M_IRQ_CLEAR => {
                self.geni_m_irq_status &= !value;
                return true;
            }
            SE_GENI_M_IRQ_EN => {
                self.geni_m_irq_en = value;
                return true;
            }
            SE_GENI_TX_WATERMARK_REG => {
                self.geni_tx_watermark = value;
                self.tx_watermark_latch();
                return true;
            }
            SE_GENI_RX_WATERMARK_REG => {
                self.geni_rx_watermark = value;
                if value > 0 && self.rx_fifo_len >= value {
                    self.geni_s_irq_status |= registers::S_RX_FIFO_WATERMARK;
                } else {
                    self.geni_s_irq_status &= !registers::S_RX_FIFO_WATERMARK;
                }
                return true;
            }
            SE_GENI_RX_RFR_WATERMARK_REG => {
                self.geni_rx_rfr_watermark = value;
            }
            SE_UART_TX_WORD_LEN => {
                self.uart_tx_word_len = value;
            }
            SE_UART_RX_WORD_LEN => {
                self.uart_rx_word_len = value;
            }
            SE_UART_TX_STOP_BIT_LEN => {
                self.uart_tx_stop_bit_len = value;
            }
            SE_UART_RX_STALE_CNT => {
                self.uart_rx_stale_cnt = value;
            }
            SE_UART_TX_TRANS_CFG => {
                self.uart_tx_trans_cfg = value;
            }
            SE_UART_RX_TRANS_CFG => {
                self.uart_rx_trans_cfg = value;
            }
            SE_UART_LOOPBACK_CFG => {
                self.uart_loopback_cfg = value;
            }
            SE_UART_IO_MACRO_CTRL => {
                self.uart_io_macro_ctrl = value;
            }
            SE_UART_MANUAL_RFR => {
                self.uart_manual_rfr = value;
            }
            SE_GENI_TX_PACKING_CFG0 => {
                self.geni_tx_packing_cfg0 = value;
            }
            SE_GENI_TX_PACKING_CFG1 => {
                self.geni_tx_packing_cfg1 = value;
            }
            SE_GENI_RX_PACKING_CFG0 => {
                self.geni_rx_packing_cfg0 = value;
            }
            SE_GENI_RX_PACKING_CFG1 => {
                self.geni_rx_packing_cfg1 = value;
            }
            SE_UART_TX_TRANS_LEN => {
                self.uart_tx_trans_len = value;
            }
            GENI_SER_M_CLK_CFG | GENI_SER_S_CLK_CFG | SE_GENI_CLK_SEL | GENI_OUTPUT_CTRL
            | GENI_FORCE_DEFAULT | SE_GSI_EVENT_EN | SE_IRQ_EN => {}
            SE_GENI_M_IRQ_EN_SET => {
                self.geni_m_irq_en |= value;
                return true;
            }
            SE_GENI_M_IRQ_EN_CLEAR => {
                self.geni_m_irq_en &= !value;
                return true;
            }
            SE_GENI_S_IRQ_STATUS => {}
            SE_GENI_S_IRQ_EN => {
                self.geni_s_irq_en = value;
                return true;
            }
            SE_GENI_S_IRQ_CLEAR => {
                self.geni_s_irq_status &= !value;
                return true;
            }
            SE_GENI_S_IRQ_EN_SET => {
                self.geni_s_irq_en |= value;
                return true;
            }
            SE_GENI_S_IRQ_EN_CLEAR => {
                self.geni_s_irq_en &= !value;
                return true;
            }
            SE_GENI_M_GP_LENGTH => {
                self.geni_m_gp_length = value;
            }
            SE_GENI_S_GP_LENGTH => {
                self.geni_s_gp_length = value;
            }
            SE_UART_TX_PARITY_CFG => {
                self.uart_tx_parity_cfg = value;
            }
            SE_UART_RX_PARITY_CFG => {
                self.uart_rx_parity_cfg = value;
            }
            SE_GENI_CFG_SEQ_START => {
                if value & 1 != 0 {
                    log_mask_ln!(
                        Log::Unimp,
                        "QUP GENI UART: Configuration sequence start attempted - protocol \
                         switching not supported"
                    );
                }
            }
            SE_GENI_M_CMD0 => {
                self.geni_m_cmd0 = value;
                let opcode = (value & registers::M_OPCODE_MSK) >> registers::M_OPCODE_SHFT;

                match opcode {
                    registers::UART_START_TX => {
                        self.geni_status |= registers::M_GENI_CMD_ACTIVE;
                        self.tx_enabled = true;
                        self.tx_watermark_latch();
                        self.tx_remaining = self.uart_tx_trans_len;
                        if self.tx_remaining == 0 {
                            self.geni_status &= !registers::M_GENI_CMD_ACTIVE;
                            self.tx_enabled = false;
                            self.geni_m_irq_status |= registers::M_CMD_DONE;
                        }
                        return true;
                    }
                    registers::UART_ABORT => {
                        self.m_cmd_stop(registers::M_CMD_DONE);
                        return true;
                    }
                    _ => {
                        log_mask_ln!(
                            Log::Unimp,
                            "QUP GENI UART: Unsupported command opcode 0x{:x} - device \
                             only supports UART mode",
                            opcode
                        );
                    }
                }
            }
            SE_GENI_M_CMD_CTRL_REG => {
                if value & registers::M_GENI_CMD_ABORT != 0 {
                    self.m_cmd_stop(registers::M_CMD_ABORT);
                    return true;
                }
                if value & registers::M_GENI_CMD_CANCEL != 0 {
                    self.m_cmd_stop(registers::M_CMD_CANCEL);
                    return true;
                }
            }
            SE_GENI_S_CMD_CTRL_REG => {
                if value & registers::S_GENI_CMD_ABORT != 0 {
                    self.s_cmd_stop(registers::S_CMD_ABORT);
                    return true;
                }
                if value & registers::S_GENI_CMD_CANCEL != 0 {
                    self.s_cmd_stop(registers::S_CMD_CANCEL);
                    return true;
                }
            }
            SE_GENI_S_CMD0 => {
                self.geni_s_cmd0 = value;
                let opcode = (value & registers::M_OPCODE_MSK) >> registers::M_OPCODE_SHFT;

                match opcode {
                    registers::UART_START_READ => {
                        self.geni_status |= registers::S_GENI_CMD_ACTIVE;
                        self.rx_enabled = true;
                    }
                    registers::UART_ABORT_READ => {
                        self.s_cmd_stop(registers::S_CMD_DONE);
                        return true;
                    }
                    _ => {
                        log_mask_ln!(
                            Log::Unimp,
                            "QUP GENI UART: Unsupported secondary command opcode 0x{:x} - \
                             device only supports UART mode",
                            opcode
                        );
                    }
                }
            }
            SE_GENI_TX_FIFO_N => {
                if self.tx_enabled {
                    let tx_data = value.to_le_bytes();
                    let len = self.tx_remaining.min(registers::BYTES_PER_FIFO_WORD) as usize;
                    if self.uart_loopback_cfg & 0x1 != 0 {
                        let mut pushed = false;
                        for &byte in &tx_data[..len] {
                            if !self.rx_enabled {
                                break;
                            }
                            if !self.rx_push_byte(byte) {
                                self.geni_s_irq_status |= registers::S_RX_FIFO_WR_ERR;
                                break;
                            }
                            pushed = true;
                        }
                        if pushed {
                            self.rx_data_pushed();
                        }
                    } else {
                        self.tx_send(&tx_data[..len], char_frontend);
                    }
                    self.tx_remaining -= len as u32;
                    self.tx_watermark_latch();
                    if self.tx_remaining == 0 {
                        self.geni_status &= !registers::M_GENI_CMD_ACTIVE;
                        self.tx_enabled = false;
                        self.geni_m_irq_status |= registers::M_CMD_DONE;
                    }
                    return true;
                } else {
                    log_mask_ln!(
                        Log::GuestError,
                        "QUP GENI UART: TX FIFO write with no TX command active"
                    );
                }
            }
            _ => {
                log_mask_ln!(
                    Log::Unimp,
                    "QUP GENI UART: Unimplemented write to offset {:x} value {:x}",
                    offset as u32,
                    value
                );
            }
        }
        false
    }

    fn m_cmd_stop(&mut self, irq: u32) {
        self.geni_status &= !registers::M_GENI_CMD_ACTIVE;
        self.tx_enabled = false;
        self.tx_remaining = 0;
        self.geni_m_irq_status |= irq;
    }

    fn s_cmd_stop(&mut self, irq: u32) {
        self.geni_status &= !registers::S_GENI_CMD_ACTIVE;
        self.rx_enabled = false;
        self.rx_fifo_len = 0;
        self.rx_last_byte_cnt = 0;
        self.rx_tail_closed = false;
        self.geni_s_irq_status &= !(registers::S_RX_FIFO_WATERMARK | registers::S_RX_FIFO_LAST);
        self.geni_s_irq_status |= irq;
    }

    // RX_LAST_BYTE_VALID can describe only the last FIFO word, so fill a
    // partial word before appending another one.
    fn rx_push_byte(&mut self, byte: u8) -> bool {
        if self.rx_last_byte_cnt > 0 && self.rx_fifo_len > 0 {
            if self.rx_tail_closed {
                return false;
            }
            let idx = self.rx_fifo_len - 1;
            self.rx_fifo[idx] |= u32::from(byte) << (self.rx_last_byte_cnt * 8);
            self.rx_last_byte_cnt = (self.rx_last_byte_cnt + 1) % registers::BYTES_PER_FIFO_WORD;
            return true;
        }
        if self.rx_fifo_len >= QUP_FIFO_DEPTH {
            return false;
        }
        let idx = self.rx_fifo_len;
        self.rx_fifo[idx] = u32::from(byte);
        self.rx_fifo_len += 1;
        self.rx_last_byte_cnt = 1;
        true
    }

    fn rx_data_pushed(&mut self) {
        if self.geni_rx_watermark > 0 && self.rx_fifo_len >= self.geni_rx_watermark {
            self.geni_s_irq_status |= registers::S_RX_FIFO_WATERMARK;
        }
        if self.rx_last_byte_cnt > 0 {
            self.geni_s_irq_status |= registers::S_RX_FIFO_LAST;
        }
    }

    pub fn reset(&mut self) {
        self.geni_status = 0;
        self.geni_m_cmd0 = 0;
        self.geni_m_irq_status = 0;
        self.geni_m_irq_en = 0;
        self.geni_s_cmd0 = 0;
        self.geni_s_irq_status = 0;
        self.geni_s_irq_en = 0;
        self.geni_tx_watermark = registers::DEF_TX_WM;
        self.geni_rx_watermark = registers::UART_RX_WM;
        self.geni_rx_rfr_watermark = registers::UART_RX_WM;
        self.tx_watermark_latch();
        self.geni_m_gp_length = 0;
        self.geni_s_gp_length = 0;
        self.geni_tx_packing_cfg0 = 0;
        self.geni_tx_packing_cfg1 = 0;
        self.geni_rx_packing_cfg0 = 0;
        self.geni_rx_packing_cfg1 = 0;

        self.uart_tx_trans_cfg = 0;
        self.uart_rx_trans_cfg = 0;
        self.uart_tx_word_len = 0;
        self.uart_rx_word_len = 0;
        self.uart_tx_stop_bit_len = 0;
        self.uart_rx_stale_cnt = registers::STALE_TIMEOUT;
        self.uart_tx_parity_cfg = 0;
        self.uart_rx_parity_cfg = 0;
        self.uart_loopback_cfg = 0;
        self.uart_io_macro_ctrl = 0;
        self.uart_manual_rfr = 0;
        self.uart_tx_trans_len = 0;

        self.rx_fifo_len = 0;
        self.rx_last_byte_cnt = 0;
        self.rx_tail_closed = false;
        self.tx_remaining = 0;
        self.tx_pending_len = 0;
        self.tx_enabled = false;
        self.rx_enabled = false;
    }
}

impl QupGeniUartState {
    /// # Safety
    ///
    /// `this` must refer to a new, correctly sized and aligned instance whose
    /// fields other than `parent_obj` are uninitialized.
    unsafe fn init(mut this: ParentInit<Self>) {
        static QUP_GENI_UART_OPS: MemoryRegionOps<QupGeniUartState> =
            MemoryRegionOpsBuilder::<QupGeniUartState>::new()
                .read(&QupGeniUartState::read)
                .write(&QupGeniUartState::write)
                .little_endian()
                .impl_sizes(4, 4)
                .build();

        // SAFETY: this and this.iomem are guaranteed to be valid at this point
        MemoryRegion::init_io(
            &mut uninit_field_mut!(*this, iomem),
            &QUP_GENI_UART_OPS,
            "qup-geni-uart",
            QUP_GENI_UART_MMIO_SIZE,
        );

        uninit_field_mut!(*this, regs).write(Default::default());
        uninit_field_mut!(*this, tx_watch).write(Default::default());
    }

    fn post_init(&self) {
        self.init_mmio(&self.iomem);
        self.init_irq(&self.irq);
    }

    fn read(&self, offset: hwaddr, _size: u32) -> u64 {
        match RegisterOffset::try_from(offset) {
            Ok(reg) => {
                let (update_irq, result) = self.regs.borrow_mut().read(reg);
                if update_irq {
                    self.update_irq();
                    self.char_frontend.accept_input();
                }
                result.into()
            }
            Err(_) => {
                log_mask_ln!(
                    Log::GuestError,
                    "QupGeniUartState::read: Bad offset {:x}",
                    offset
                );
                0
            }
        }
    }

    fn write(&self, offset: hwaddr, value: u64, _size: u32) {
        if let Ok(reg) = RegisterOffset::try_from(offset) {
            let update_irq = self
                .regs
                .borrow_mut()
                .write(reg, value as u32, &self.char_frontend);
            if update_irq {
                self.update_irq();
            }
            self.update_tx_watch();
            if matches!(
                reg,
                RegisterOffset::SE_GENI_S_CMD0 | RegisterOffset::SE_GENI_S_CMD_CTRL_REG
            ) {
                self.char_frontend.accept_input();
            }
        } else {
            log_mask_ln!(
                Log::GuestError,
                "QupGeniUartState::write: Bad offset {:x} value {:x}",
                offset,
                value
            );
        }
    }

    fn can_receive(&self) -> u32 {
        let regs = self.regs.borrow();
        if regs.uart_loopback_cfg & 1 != 0 {
            0
        } else if regs.rx_enabled {
            if regs.rx_tail_closed {
                0
            } else {
                let words = QUP_FIFO_DEPTH - regs.rx_fifo_len;
                let tail = if regs.rx_last_byte_cnt > 0 {
                    registers::BYTES_PER_FIFO_WORD - regs.rx_last_byte_cnt
                } else {
                    0
                };
                words * registers::BYTES_PER_FIFO_WORD + tail
            }
        } else {
            0
        }
    }

    fn receive(&self, buf: &[u8]) {
        let mut regs = self.regs.borrow_mut();
        if !regs.rx_enabled || (regs.uart_loopback_cfg & 0x1 != 0) {
            return;
        }
        let mut pushed = false;
        let mut overflow = false;
        for &byte in buf {
            if !regs.rx_push_byte(byte) {
                overflow = true;
                break;
            }
            pushed = true;
        }
        if overflow {
            regs.geni_s_irq_status |= registers::S_RX_FIFO_WR_ERR;
        }
        if pushed {
            regs.rx_data_pushed();
        }

        drop(regs);
        if pushed || overflow {
            self.update_irq();
        }
    }

    fn event(&self, _event: Event) {}

    fn realize(&self) -> util::Result<()> {
        self.char_frontend
            .enable_handlers(self, Self::can_receive, Self::receive, Self::event);
        Ok(())
    }

    fn reset_hold(&self, _type: ResetType) {
        self.clear_tx_watch();
        self.regs.borrow_mut().reset();
        self.update_irq();
        self.char_frontend.accept_input();
    }

    fn clear_tx_watch(&self) {
        let tag = self.tx_watch.replace(0);
        self.char_frontend.remove_watch(tag);
    }

    fn arm_tx_watch(&self) -> bool {
        if self.tx_watch.get() == 0 {
            // SAFETY: the source is removed on reset and before object drop.
            let tag = unsafe {
                self.char_frontend.add_write_watch(
                    Some(Self::tx_ready),
                    (self as *const Self).cast_mut().cast(),
                )
            };
            self.tx_watch.set(tag);
        }
        self.tx_watch.get() != 0
    }

    fn update_tx_watch(&self) {
        let wanted = {
            let regs = self.regs.borrow();
            regs.tx_pending_len > 0
        };

        if !wanted {
            self.clear_tx_watch();
            return;
        }
        if self.arm_tx_watch() {
            return;
        }

        // Such a backend, for instance UDP, is lossy anyway.
        let mut regs = self.regs.borrow_mut();
        if regs.tx_pending_len > 0 {
            log_mask_ln!(
                Log::Unimp,
                "QUP GENI UART: backend cannot report writability, \
                 dropping {} queued TX bytes",
                regs.tx_pending_len
            );
            regs.tx_pending_len = 0;
            regs.tx_watermark_latch();
            drop(regs);
            self.update_irq();
        }
    }

    unsafe extern "C" fn tx_ready(
        _unused: *mut c_void,
        _condition: u32,
        opaque: *mut c_void,
    ) -> i32 {
        // SAFETY: the watch holds a pointer to this live QOM object.
        let this = unsafe { &*opaque.cast::<Self>() };
        this.tx_watch.set(0);
        this.regs.borrow_mut().tx_flush(&this.char_frontend);
        this.update_irq();
        this.update_irq();
        this.update_tx_watch();
        0
    }

    fn post_load(&self, _version: u8) -> Result<(), migration::InvalidError> {
        {
            // Reject invalid array indices and shift counts before use.
            let regs = self.regs.borrow();
            if regs.rx_fifo_len > QUP_FIFO_DEPTH
                || regs.rx_last_byte_cnt >= registers::BYTES_PER_FIFO_WORD
                || (regs.rx_last_byte_cnt > 0 && regs.rx_fifo_len == 0)
                || (regs.rx_tail_closed && regs.rx_last_byte_cnt == 0)
                || regs.tx_pending_len as usize > TX_QUEUE_BYTES
            {
                return Err(migration::InvalidError);
            }
        }
        self.update_irq();
        self.update_tx_watch();
        Ok(())
    }

    fn update_irq(&self) {
        let regs = self.regs.borrow();
        let irq_active = (regs.geni_m_irq_status & regs.geni_m_irq_en) != 0
            || (regs.geni_s_irq_status & regs.geni_s_irq_en) != 0;
        self.irq.set(irq_active);
    }
}

impl Drop for QupGeniUartState {
    fn drop(&mut self) {
        self.clear_tx_watch();
    }
}

impl_vmstate_struct!(
    QupGeniUartRegs,
    VMStateDescriptionBuilder::<QupGeniUartRegs>::new()
        .name(c"qup_geni_uart/regs")
        .version_id(1)
        .minimum_version_id(1)
        .fields(vmstate_fields! {
        vmstate_of!(QupGeniUartRegs, geni_status),
        vmstate_of!(QupGeniUartRegs, geni_m_cmd0),
        vmstate_of!(QupGeniUartRegs, geni_m_irq_status),
        vmstate_of!(QupGeniUartRegs, geni_m_irq_en),
        vmstate_of!(QupGeniUartRegs, geni_s_cmd0),
        vmstate_of!(QupGeniUartRegs, geni_s_irq_status),
        vmstate_of!(QupGeniUartRegs, geni_s_irq_en),
        vmstate_of!(QupGeniUartRegs, geni_tx_watermark),
        vmstate_of!(QupGeniUartRegs, geni_rx_watermark),
        vmstate_of!(QupGeniUartRegs, geni_rx_rfr_watermark),
        vmstate_of!(QupGeniUartRegs, geni_m_gp_length),
        vmstate_of!(QupGeniUartRegs, geni_s_gp_length),
        vmstate_of!(QupGeniUartRegs, geni_tx_packing_cfg0),
        vmstate_of!(QupGeniUartRegs, geni_tx_packing_cfg1),
        vmstate_of!(QupGeniUartRegs, geni_rx_packing_cfg0),
        vmstate_of!(QupGeniUartRegs, geni_rx_packing_cfg1),
        vmstate_of!(QupGeniUartRegs, uart_tx_trans_cfg),
        vmstate_of!(QupGeniUartRegs, uart_rx_trans_cfg),
        vmstate_of!(QupGeniUartRegs, uart_tx_word_len),
        vmstate_of!(QupGeniUartRegs, uart_rx_word_len),
        vmstate_of!(QupGeniUartRegs, uart_tx_stop_bit_len),
        vmstate_of!(QupGeniUartRegs, uart_rx_stale_cnt),
        vmstate_of!(QupGeniUartRegs, uart_tx_parity_cfg),
        vmstate_of!(QupGeniUartRegs, uart_rx_parity_cfg),
        vmstate_of!(QupGeniUartRegs, uart_loopback_cfg),
        vmstate_of!(QupGeniUartRegs, uart_io_macro_ctrl),
        vmstate_of!(QupGeniUartRegs, uart_manual_rfr),
        vmstate_of!(QupGeniUartRegs, uart_tx_trans_len),
        vmstate_of!(QupGeniUartRegs, tx_pending),
        vmstate_of!(QupGeniUartRegs, tx_pending_len),
        vmstate_of!(QupGeniUartRegs, rx_fifo),
        vmstate_of!(QupGeniUartRegs, rx_fifo_len),
        vmstate_of!(QupGeniUartRegs, rx_last_byte_cnt),
        vmstate_of!(QupGeniUartRegs, rx_tail_closed),
        vmstate_of!(QupGeniUartRegs, tx_remaining),
        vmstate_of!(QupGeniUartRegs, tx_enabled),
        vmstate_of!(QupGeniUartRegs, rx_enabled),
        })
        .build()
);

pub const VMSTATE_QUP_GENI_UART: VMStateDescription<QupGeniUartState> =
    VMStateDescriptionBuilder::<QupGeniUartState>::new()
        .name(c"qup_geni_uart")
        .post_load(&QupGeniUartState::post_load)
        .version_id(1)
        .minimum_version_id(1)
        .fields(vmstate_fields! {
            vmstate_of!(QupGeniUartState, regs),
        })
        .build();
