// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0-or-later

use std::{
    ffi::{c_void, CStr},
    io::ErrorKind,
    ptr,
};

use bql::prelude::*;
use chardev::prelude::*;
use common::{prelude::*, Opaque};
use hwcore::{bindings::qdev_prop_allow_set_link_before_realize, prelude::*};
use migration::{self, prelude::*};
use qom::{
    bindings::{object_property_add_link, OBJ_PROP_LINK_STRONG},
    prelude::*,
};
use system::{
    bindings::{
        address_space_destroy, address_space_init, address_space_rw, get_system_memory,
        AddressSpace,
    },
    prelude::*,
    MEMTXATTRS_UNSPECIFIED,
};
use util::{self, log::Log, log_mask_ln};

use crate::registers::{self, RegisterOffset, UART_FIFO_DEPTH_WORDS};

// FIFO depth is 256 bytes = 64 words
pub const QUP_FIFO_DEPTH: u32 = UART_FIFO_DEPTH_WORDS;

/// Size of the register block; must match QUP_GENI_UART_MMIO_SIZE in
/// include/hw/char/qup_geni_uart.h.
const QUP_GENI_UART_MMIO_SIZE: u64 = 0x6000;

/// Most bytes moved by one step of a DMA transfer
const DMA_CHUNK_SIZE: usize = 4096;

// FIFOs use 32-bit indices for compatibility with migration stream
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

#[repr(C)]
#[derive(Debug, Default)]
pub struct QupGeniUartRegs {
    // Common GENI registers
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

    // UART specific registers
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

    // FIFOs.  There is no TX FIFO state: a word written to it is sent on
    // the spot, so the TX FIFO is always empty.
    pub rx_fifo: Fifo,
    pub rx_fifo_len: u32,
    /// Valid bytes in the final RX FIFO word; 0 means the word is full.
    /// Reported to the guest via RX_LAST/RX_LAST_BYTE_VALID.
    pub rx_last_byte_cnt: u32,
    /// The guest has been told how many bytes the short final word holds, so
    /// it must not grow until the FIFO has been drained.
    pub rx_tail_closed: bool,
    /// Bytes still owed by the active TX command, from SE_UART_TX_TRANS_LEN.
    pub tx_remaining: u32,

    // DMA registers
    pub dma_tx_ptr_l: u32,
    pub dma_tx_ptr_h: u32,
    pub dma_tx_attr: u32,
    pub dma_tx_len: u32,
    pub dma_tx_sent: u32,
    pub dma_tx_irq_en: u32,
    pub dma_tx_irq_stat: u32,
    pub dma_rx_ptr_l: u32,
    pub dma_rx_ptr_h: u32,
    pub dma_rx_attr: u32,
    pub dma_rx_len: u32,
    pub dma_rx_len_in: u32,
    pub dma_rx_irq_en: u32,
    pub dma_rx_irq_stat: u32,

    // State
    pub tx_enabled: bool,
    pub rx_enabled: bool,
    pub dma_mode_enabled: bool,
    /// A TX DMA length was programmed; it starts once a TX command is active,
    /// whichever of the two the guest does first.
    pub dma_tx_armed: bool,
    pub dma_tx_active: bool,
    pub dma_rx_active: bool,
}

/// The memory the DMA engines address, which the board owns.  It is the
/// target of the "memory" link property.
#[derive(Clone, Copy)]
#[repr(transparent)]
pub struct DmaRegion(*mut system::bindings::MemoryRegion);

// SAFETY: the pointer is only used under the BQL
unsafe impl Send for DmaRegion {}

/// The address space of the DMA engines, initialized when realized.
struct DmaAddressSpace(Opaque<AddressSpace>);

// SAFETY: address_space_rw() may be called from any thread
unsafe impl Sync for DmaAddressSpace {}

#[repr(C)]
#[derive(qemu_macros::Object, hwcore::Device)]
/// QUP GENI UART Device Model in QEMU
pub struct QupGeniUartState {
    pub parent_obj: ParentField<SysBusDevice>,
    pub iomem: MemoryRegion,
    #[property(rename = "chardev")]
    pub char_frontend: CharFrontend,
    pub regs: BqlRefCell<QupGeniUartRegs>,
    pub irq: InterruptSource,
    pub tx_watch: BqlCell<u32>,
    /// Memory the DMA engines address, from the "memory" link property; the
    /// system memory if not set
    pub dma_mr: BqlCell<DmaRegion>,
    dma_as: DmaAddressSpace,
    dma_as_ready: BqlCell<bool>,
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
    /// TX watermark is a level condition, not a latched event: it asserts
    /// whenever the FIFO sits at or below the threshold, independent of any
    /// active command. The guest arms a transfer by enabling this interrupt
    /// and waiting for it, so gating it on an active command would deadlock.
    /// A threshold of zero means the guest has disabled the watermark.  The
    /// TX FIFO is always empty, so it is never above the threshold.
    fn m_irq_status_level(&self) -> u32 {
        let mut status = self.geni_m_irq_status;
        if self.geni_tx_watermark > 0 {
            status |= registers::M_TX_FIFO_WATERMARK_EN;
        }
        status
    }

    pub fn read(&mut self, offset: RegisterOffset) -> (bool, u32) {
        use RegisterOffset::*;

        let mut update_irq = false;
        let result = match offset {
            SE_GENI_STATUS => self.geni_status,
            GENI_FW_REVISION_RO => {
                // Return UART protocol in firmware revision
                (registers::GENI_SE_UART << registers::FW_REV_PROTOCOL_SHFT) | 0x0001
            }
            SE_GENI_M_IRQ_STATUS => self.m_irq_status_level(),
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
            SE_GENI_DMA_MODE_EN => self.dma_mode_enabled as u32,
            // The TX FIFO is always empty
            SE_GENI_TX_FIFO_STATUS => 0,
            SE_GENI_RX_FIFO_STATUS => {
                let mut status = self.rx_fifo_len & registers::RX_FIFO_WC_MSK;
                // A short trailing word must be advertised as RX_LAST, otherwise
                // the guest assumes a full word and consumes padding bytes.
                if self.rx_fifo_len > 0 && self.rx_last_byte_cnt > 0 {
                    // The guest sizes its reads from this, so the word must
                    // not grow behind its back.
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
                    // Shift FIFO contents
                    for i in 0..self.rx_fifo_len - 1 {
                        self.rx_fifo[i] = self.rx_fifo[i + 1];
                    }
                    self.rx_fifo_len -= 1;
                    // Watermark is level-triggered; LAST clears once the short
                    // word it referred to has been consumed.
                    if self.rx_fifo_len < self.geni_rx_watermark {
                        self.geni_s_irq_status &= !registers::S_RX_FIFO_WATERMARK_EN;
                    }
                    if self.rx_fifo_len == 0 {
                        self.geni_s_irq_status &= !registers::S_RX_FIFO_LAST_EN;
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
                // TX FIFO parameters: width=32, depth=64
                (registers::UART_FIFO_WIDTH_BITS << registers::TX_FIFO_WIDTH_SHFT)
                    | (registers::UART_FIFO_DEPTH_WORDS << registers::TX_FIFO_DEPTH_SHFT)
            }
            SE_HW_PARAM1 => {
                // RX FIFO parameters: width=32, depth=64
                (registers::UART_FIFO_WIDTH_BITS << registers::RX_FIFO_WIDTH_SHFT)
                    | (registers::UART_FIFO_DEPTH_WORDS << registers::RX_FIFO_DEPTH_SHFT)
            }
            // DMA register reads
            SE_DMA_TX_PTR_L => self.dma_tx_ptr_l,
            SE_DMA_TX_PTR_H => self.dma_tx_ptr_h,
            SE_DMA_TX_ATTR => self.dma_tx_attr,
            SE_DMA_TX_LEN => self.dma_tx_len,
            SE_DMA_TX_IRQ_EN => self.dma_tx_irq_en,
            SE_DMA_TX_IRQ_STAT => self.dma_tx_irq_stat,
            SE_DMA_RX_PTR_L => self.dma_rx_ptr_l,
            SE_DMA_RX_PTR_H => self.dma_rx_ptr_h,
            SE_DMA_RX_ATTR => self.dma_rx_attr,
            SE_DMA_RX_LEN => self.dma_rx_len,
            SE_DMA_RX_LEN_IN => self.dma_rx_len_in,
            SE_DMA_RX_IRQ_EN => self.dma_rx_irq_en,
            SE_DMA_RX_IRQ_STAT => self.dma_rx_irq_stat,
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
                // Changing the threshold re-evaluates the level condition
                return true;
            }
            SE_GENI_RX_WATERMARK_REG => {
                self.geni_rx_watermark = value;
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
            GENI_SER_M_CLK_CFG | GENI_SER_S_CLK_CFG | SE_GENI_CLK_SEL => {
                // Clock configuration - accept silently
            }
            GENI_OUTPUT_CTRL | GENI_FORCE_DEFAULT => {
                // I/O control - accept silently
            }
            SE_GENI_M_IRQ_EN_SET => {
                self.geni_m_irq_en |= value;
                return true;
            }
            SE_GENI_M_IRQ_EN_CLEAR => {
                self.geni_m_irq_en &= !value;
                return true;
            }
            SE_GENI_S_IRQ_STATUS => {
                // Read-only, ignore writes
            }
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
            SE_GSI_EVENT_EN | SE_IRQ_EN => {
                // Interrupt routing - accept silently
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
            SE_GENI_DMA_MODE_EN => {
                self.dma_mode_enabled = (value & 1) != 0;
            }
            SE_GENI_M_CMD0 => {
                self.geni_m_cmd0 = value;
                let opcode = (value & registers::M_OPCODE_MSK) >> registers::M_OPCODE_SHFT;

                match opcode {
                    registers::UART_START_TX => {
                        self.geni_status |= registers::M_GENI_CMD_ACTIVE;
                        self.tx_enabled = true;
                        // The command transfers exactly TX_TRANS_LEN bytes
                        self.tx_remaining = self.uart_tx_trans_len;
                        if self.tx_remaining == 0 {
                            self.geni_status &= !registers::M_GENI_CMD_ACTIVE;
                            self.tx_enabled = false;
                            self.geni_m_irq_status |= registers::M_CMD_DONE_EN;
                        } else if self.dma_tx_armed {
                            self.dma_tx_active = true;
                        }
                        return true;
                    }
                    registers::UART_ABORT => {
                        self.m_cmd_stop(registers::M_CMD_DONE_EN);
                        return true;
                    }
                    0x3..=0x1F => {
                        log_mask_ln!(
                            Log::Unimp,
                            "QUP GENI UART: Unknown/unsupported command opcode 0x{:x} - device \
                             only supports UART mode",
                            opcode
                        );
                    }
                    _ => {
                        log_mask_ln!(
                            Log::GuestError,
                            "QUP GENI UART: Invalid command opcode 0x{:x}",
                            opcode
                        );
                    }
                }
            }
            SE_GENI_M_CMD_CTRL_REG => {
                if value & registers::M_GENI_CMD_ABORT != 0 {
                    self.m_cmd_stop(registers::M_CMD_ABORT_EN);
                    return true;
                }
                if value & registers::M_GENI_CMD_CANCEL != 0 {
                    self.m_cmd_stop(registers::M_CMD_CANCEL_EN);
                    return true;
                }
            }
            SE_GENI_S_CMD_CTRL_REG => {
                if value & registers::S_GENI_CMD_ABORT != 0 {
                    self.s_cmd_stop(registers::S_CMD_ABORT_EN);
                    return true;
                }
                if value & registers::S_GENI_CMD_CANCEL != 0 {
                    self.s_cmd_stop(registers::S_CMD_CANCEL_EN);
                    if self.dma_mode_enabled {
                        self.dma_rx_irq_stat |= registers::DMA_EOT_EN;
                    }
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
                        self.s_cmd_stop(registers::S_CMD_DONE_EN);
                        return true;
                    }
                    0x3..=0x1F => {
                        log_mask_ln!(
                            Log::Unimp,
                            "QUP GENI UART: Non-UART secondary command opcode 0x{:x} attempted - \
                             device only supports UART mode",
                            opcode
                        );
                    }
                    _ => {
                        log_mask_ln!(
                            Log::GuestError,
                            "QUP GENI UART: Invalid secondary command opcode 0x{:x}",
                            opcode
                        );
                    }
                }
            }
            SE_GENI_TX_FIFO_N => {
                if self.tx_enabled {
                    // The active command declares its exact byte count, so the
                    // final word is partial rather than NUL-padded.
                    let tx_data = value.to_le_bytes();
                    let len = self.tx_remaining.min(registers::BYTES_PER_FIFO_WORD) as usize;
                    if self.uart_loopback_cfg & 0x1 != 0 {
                        // The looped-back bytes are lost unless the receiver
                        // is running, as on the wire.
                        let mut pushed = false;
                        for &byte in &tx_data[..len] {
                            if !self.rx_enabled {
                                break;
                            }
                            if !self.rx_push_byte(byte) {
                                self.geni_s_irq_status |= registers::S_RX_FIFO_WR_ERR_EN;
                                break;
                            }
                            pushed = true;
                        }
                        if pushed {
                            self.rx_data_pushed();
                        }
                    } else {
                        // Unlike the DMA path this waits for the backend, with
                        // the BQL held, if it is full: a FIFO word has nowhere
                        // to be held back.  The pl011 does the same.
                        let _ = char_frontend.write_all(&tx_data[..len]);
                    }
                    self.tx_remaining -= len as u32;
                    if self.tx_remaining == 0 {
                        self.geni_status &= !registers::M_GENI_CMD_ACTIVE;
                        self.tx_enabled = false;
                        self.geni_m_irq_status |= registers::M_CMD_DONE_EN;
                    }
                    return true;
                } else {
                    // Nothing consumes the FIFO until a command is started
                    log_mask_ln!(
                        Log::GuestError,
                        "QUP GENI UART: TX FIFO write with no TX command active"
                    );
                }
            }
            // DMA register writes
            SE_DMA_TX_PTR_L => {
                self.dma_tx_ptr_l = value;
            }
            SE_DMA_TX_PTR_H => {
                self.dma_tx_ptr_h = value;
            }
            SE_DMA_TX_ATTR => {
                self.dma_tx_attr = value;
            }
            SE_DMA_TX_LEN => {
                self.dma_tx_len = value;
                self.dma_tx_sent = 0;
                self.dma_tx_armed = self.dma_mode_enabled && value > 0;
                self.dma_tx_active = self.dma_tx_armed && self.tx_enabled;
            }
            SE_DMA_TX_IRQ_EN => {
                self.dma_tx_irq_en = value;
                return true;
            }
            SE_DMA_TX_IRQ_EN_SET => {
                self.dma_tx_irq_en |= value;
                return true;
            }
            SE_DMA_TX_IRQ_EN_CLR => {
                self.dma_tx_irq_en &= !value;
                return true;
            }
            SE_DMA_TX_IRQ_CLR => {
                self.dma_tx_irq_stat &= !value;
                return true;
            }
            SE_DMA_TX_FSM_RST => {
                if value & 1 != 0 {
                    self.dma_tx_stop();
                    self.dma_tx_irq_stat |= registers::DMA_RESET_DONE_EN;
                    return true;
                }
            }
            SE_DMA_RX_PTR_L => {
                self.dma_rx_ptr_l = value;
            }
            SE_DMA_RX_PTR_H => {
                self.dma_rx_ptr_h = value;
            }
            SE_DMA_RX_ATTR => {
                self.dma_rx_attr = value;
            }
            SE_DMA_RX_LEN => {
                self.dma_rx_len = value;
                self.dma_rx_len_in = 0;
                if self.dma_mode_enabled && value > 0 {
                    self.dma_rx_active = true;
                }
            }
            SE_DMA_RX_FSM_RST => {
                if value & 1 != 0 {
                    self.dma_rx_active = false;
                    self.dma_rx_irq_stat |= registers::DMA_RESET_DONE_EN;
                    return true;
                }
            }
            SE_DMA_RX_IRQ_EN => {
                self.dma_rx_irq_en = value;
                return true;
            }
            SE_DMA_RX_IRQ_EN_SET => {
                self.dma_rx_irq_en |= value;
                return true;
            }
            SE_DMA_RX_IRQ_EN_CLR => {
                self.dma_rx_irq_en &= !value;
                return true;
            }
            SE_DMA_RX_IRQ_CLR => {
                self.dma_rx_irq_stat &= !value;
                return true;
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

    /// Plan the next chunk of the active TX DMA: the guest address to fetch
    /// it from and its size.  Retires the transfer once it is complete, and
    /// returns `None` when there is nothing to fetch right now.
    ///
    /// The guest memory access itself is left to the caller, which must make
    /// it with the registers unborrowed: the buffer may be this device's own
    /// MMIO.
    fn dma_tx_plan(&mut self) -> Option<(u64, usize)> {
        let len = self.dma_tx_len.min(self.uart_tx_trans_len);

        if !self.dma_tx_active {
            return None;
        }
        if self.dma_tx_sent >= len {
            self.dma_tx_stop();
            self.dma_tx_irq_stat |= registers::DMA_DONE_EN;
            if self.tx_remaining == 0 {
                self.tx_enabled = false;
                self.geni_status &= !registers::M_GENI_CMD_ACTIVE;
                self.geni_m_irq_status |= registers::M_CMD_DONE_EN;
            }
            return None;
        }

        let mut count = (len - self.dma_tx_sent).min(DMA_CHUNK_SIZE as u32) as usize;
        if self.uart_loopback_cfg & 1 != 0 && self.rx_enabled {
            // Wait for the receiver to be given a buffer
            if !self.dma_rx_active {
                return None;
            }
            count = count.min(self.dma_rx_len as usize);
        }
        let base = (u64::from(self.dma_tx_ptr_h) << 32) | u64::from(self.dma_tx_ptr_l);
        match base.checked_add(u64::from(self.dma_tx_sent)) {
            Some(addr) if count > 0 => Some((addr, count)),
            Some(_) => None,
            None => {
                self.dma_tx_irq_stat |= registers::DMA_AHB_ERR_EN;
                self.dma_tx_stop();
                None
            }
        }
    }

    /// End the TX DMA, however it ended.
    fn dma_tx_stop(&mut self) {
        self.dma_tx_armed = false;
        self.dma_tx_active = false;
    }

    /// Where received DMA data goes.
    fn dma_rx_addr(&self) -> u64 {
        (u64::from(self.dma_rx_ptr_h) << 32) | u64::from(self.dma_rx_ptr_l)
    }

    /// Complete the RX DMA after `len` bytes were (or failed to be) stored.
    fn dma_rx_finish(&mut self, len: usize, ok: bool) {
        self.dma_rx_active = false;
        if ok {
            self.dma_rx_len_in = len as u32;
            self.dma_rx_irq_stat |= registers::DMA_DONE_EN;
        } else {
            self.dma_rx_irq_stat |= registers::DMA_AHB_ERR_EN;
        }
    }

    /// Retire the main sequencer's command, flushing its TX state.
    fn m_cmd_stop(&mut self, irq: u32) {
        self.geni_status &= !registers::M_GENI_CMD_ACTIVE;
        self.tx_enabled = false;
        self.dma_tx_stop();
        self.tx_remaining = 0;
        self.geni_m_irq_status |= irq;
    }

    /// Retire the secondary sequencer's command, flushing its RX state.
    fn s_cmd_stop(&mut self, irq: u32) {
        self.geni_status &= !registers::S_GENI_CMD_ACTIVE;
        self.rx_enabled = false;
        self.dma_rx_active = false;
        self.rx_fifo_len = 0;
        self.rx_last_byte_cnt = 0;
        self.rx_tail_closed = false;
        self.geni_s_irq_status &=
            !(registers::S_RX_FIFO_WATERMARK_EN | registers::S_RX_FIFO_LAST_EN);
        self.geni_s_irq_status |= irq;
    }

    /// Append a byte to the RX FIFO.  Bytes are packed into the partially
    /// filled final word if there is one, so that only the last word of the
    /// FIFO can be short, which is all RX_LAST_BYTE_VALID can describe.
    /// Returns false if the FIFO is full, which includes a short final word
    /// that the guest has already been told the size of.
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

    /// Raise the RX events for data just added with `rx_push_byte`.
    ///
    /// A short trailing word ends the burst, which real hardware reports as
    /// a stale/LAST condition.  Without it the guest sits below the watermark
    /// and never drains the tail of the input.
    fn rx_data_pushed(&mut self) {
        // A threshold of zero means the guest has disabled the watermark
        if self.geni_rx_watermark > 0 && self.rx_fifo_len >= self.geni_rx_watermark {
            self.geni_s_irq_status |= registers::S_RX_FIFO_WATERMARK_EN;
        }
        if self.rx_last_byte_cnt > 0 {
            self.geni_s_irq_status |= registers::S_RX_FIFO_LAST_EN;
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
        self.tx_enabled = false;
        self.rx_enabled = false;
        self.dma_mode_enabled = false;

        self.dma_tx_ptr_l = 0;
        self.dma_tx_ptr_h = 0;
        self.dma_tx_attr = 0;
        self.dma_tx_len = 0;
        self.dma_tx_sent = 0;
        self.dma_tx_irq_en = 0;
        self.dma_tx_irq_stat = 0;
        self.dma_rx_ptr_l = 0;
        self.dma_rx_ptr_h = 0;
        self.dma_rx_attr = 0;
        self.dma_rx_len = 0;
        self.dma_rx_len_in = 0;
        self.dma_rx_irq_en = 0;
        self.dma_rx_irq_stat = 0;
        self.dma_tx_armed = false;
        self.dma_tx_active = false;
        self.dma_rx_active = false;
    }
}

impl QupGeniUartState {
    /// # Safety
    ///
    /// `self` must point to a correctly sized and aligned location for the
    /// `QupGeniUartState` type. It must not be called more than once on the
    /// same location/instance. All its fields are expected to hold
    /// uninitialized values with the sole exception of `parent_obj`.
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
        uninit_field_mut!(*this, dma_mr).write(BqlCell::new(DmaRegion(ptr::null_mut())));
        // SAFETY: address_space_init() fills it in when the device is realized
        uninit_field_mut!(*this, dma_as).write(unsafe { DmaAddressSpace(Opaque::zeroed()) });
        uninit_field_mut!(*this, dma_as_ready).write(Default::default());
    }

    fn post_init(&self) {
        self.init_mmio(&self.iomem);
        self.init_irq(&self.irq);

        // SAFETY: dma_mr lives as long as the object, and DmaRegion is a
        // transparent wrapper of the pointer that the link property stores.
        // The property keeps a reference to what it is set to.
        unsafe {
            object_property_add_link(
                self.as_object_mut_ptr(),
                c"memory".as_ptr(),
                c"memory-region".as_ptr(),
                self.dma_mr.as_ptr().cast(),
                Some(qdev_prop_allow_set_link_before_realize),
                OBJ_PROP_LINK_STRONG,
            );
        }
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
            if matches!(
                reg,
                RegisterOffset::SE_GENI_M_CMD0
                    | RegisterOffset::SE_DMA_TX_LEN
                    | RegisterOffset::SE_DMA_RX_LEN
            ) {
                self.run_dma_tx();
                self.update_irq();
            }
            self.update_tx_watch();
            if matches!(
                reg,
                RegisterOffset::SE_GENI_S_CMD0 | RegisterOffset::SE_DMA_RX_LEN
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

    /// Read guest memory through the DMA address space.
    fn dma_read(&self, addr: u64, buf: &mut [u8]) -> bool {
        // SAFETY: buf is writable for its length and dma_as was initialized
        // by realize, before any register could be accessed.
        let result = unsafe {
            address_space_rw(
                self.dma_as.0.as_ptr(),
                addr,
                MEMTXATTRS_UNSPECIFIED,
                buf.as_mut_ptr().cast(),
                buf.len() as u64,
                false,
            )
        };
        result == 0
    }

    /// Write guest memory through the DMA address space.
    fn dma_write(&self, addr: u64, data: &[u8]) -> bool {
        // SAFETY: data is readable for its length and dma_as was initialized
        // by realize, before any register could be accessed.
        let result = unsafe {
            address_space_rw(
                self.dma_as.0.as_ptr(),
                addr,
                MEMTXATTRS_UNSPECIFIED,
                data.as_ptr().cast_mut().cast(),
                data.len() as u64,
                true,
            )
        };
        result == 0
    }

    /// Store received data in the guest's RX DMA buffer.
    ///
    /// The registers are not borrowed during the access: the buffer could be
    /// this device's own MMIO, which a borrow would turn into a panic.
    fn dma_rx_deliver(&self, data: &[u8]) {
        let addr = self.regs.borrow().dma_rx_addr();
        let ok = self.dma_write(addr, data);
        self.regs.borrow_mut().dma_rx_finish(data.len(), ok);
    }

    /// Move as much of the active TX DMA as the backend will take.
    ///
    /// As in `dma_rx_deliver`, guest memory is accessed unborrowed, so the
    /// state is checked again after each access.
    fn run_dma_tx(&self) {
        let mut buf = [0u8; DMA_CHUNK_SIZE];

        loop {
            let plan = self.regs.borrow_mut().dma_tx_plan();
            let Some((addr, count)) = plan else {
                break;
            };
            let ok = self.dma_read(addr, &mut buf[..count]);

            let mut regs = self.regs.borrow_mut();
            if !regs.dma_tx_active {
                break;
            }
            if !ok {
                regs.dma_tx_irq_stat |= registers::DMA_AHB_ERR_EN;
                regs.dma_tx_stop();
                break;
            }
            let sent = if regs.uart_loopback_cfg & 1 != 0 {
                let rx_enabled = regs.rx_enabled;
                drop(regs);
                if rx_enabled {
                    self.dma_rx_deliver(&buf[..count]);
                }
                regs = self.regs.borrow_mut();
                count
            } else {
                match self.char_frontend.write(&buf[..count]) {
                    Ok(0) => count,
                    Ok(sent) => sent,
                    Err(err) if err.kind() == ErrorKind::WouldBlock => break,
                    Err(_) => count,
                }
            };
            regs.dma_tx_sent += sent as u32;
            regs.tx_remaining = regs.tx_remaining.saturating_sub(sent as u32);
        }
    }

    fn can_receive(&self) -> u32 {
        let regs = self.regs.borrow();
        if regs.uart_loopback_cfg & 1 != 0 {
            0
        } else if regs.rx_enabled && regs.dma_mode_enabled {
            if regs.dma_rx_active {
                regs.dma_rx_len.min(DMA_CHUNK_SIZE as u32)
            } else {
                0
            }
        } else if regs.rx_enabled {
            // Hold input back while the guest is draining a short final word
            if regs.rx_tail_closed {
                0
            } else {
                QUP_FIFO_DEPTH - regs.rx_fifo_len
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

        if regs.dma_mode_enabled {
            let dma_rx_active = regs.dma_rx_active;
            drop(regs);
            if dma_rx_active {
                self.dma_rx_deliver(buf);
            }
            self.update_irq();
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
            regs.geni_s_irq_status |= registers::S_RX_FIFO_WR_ERR_EN;
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
        let mut dma_mr = self.dma_mr.get().0;
        if dma_mr.is_null() {
            // SAFETY: the system memory exists for the life of the machine
            dma_mr = unsafe { get_system_memory() };
            self.dma_mr.set(DmaRegion(dma_mr));
        }
        // SAFETY: dma_as is not initialized yet and dma_mr is a valid region
        unsafe {
            address_space_init(
                self.dma_as.0.as_mut_ptr(),
                dma_mr,
                c"qup-geni-uart-dma".as_ptr(),
            );
        }
        self.dma_as_ready.set(true);

        self.char_frontend
            .enable_handlers(self, Self::can_receive, Self::receive, Self::event);
        Ok(())
    }

    fn reset_hold(&self, _type: ResetType) {
        self.clear_tx_watch();
        self.regs.borrow_mut().reset();
        self.update_irq();
    }

    fn clear_tx_watch(&self) {
        let tag = self.tx_watch.replace(0);
        self.char_frontend.remove_watch(tag);
    }

    fn update_tx_watch(&self) {
        if !self.regs.borrow().dma_tx_active || self.regs.borrow().uart_loopback_cfg & 1 != 0 {
            self.clear_tx_watch();
        } else if self.tx_watch.get() == 0 {
            // SAFETY: the source is removed on reset and before object drop.
            let tag = unsafe {
                self.char_frontend.add_write_watch(
                    Some(Self::tx_ready),
                    (self as *const Self).cast_mut().cast(),
                )
            };
            self.tx_watch.set(tag);
            if tag == 0 {
                let mut regs = self.regs.borrow_mut();
                let pending = regs
                    .dma_tx_len
                    .min(regs.uart_tx_trans_len)
                    .saturating_sub(regs.dma_tx_sent);
                regs.tx_remaining = regs.tx_remaining.saturating_sub(pending);
                regs.dma_tx_stop();
                regs.dma_tx_irq_stat |= registers::DMA_DONE_EN;
                if regs.tx_remaining == 0 {
                    regs.tx_enabled = false;
                    regs.geni_status &= !registers::M_GENI_CMD_ACTIVE;
                    regs.geni_m_irq_status |= registers::M_CMD_DONE_EN;
                }
                drop(regs);
                self.update_irq();
            }
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
        this.run_dma_tx();
        this.update_irq();
        this.update_tx_watch();
        0
    }

    fn post_load(&self, _version: u8) -> Result<(), migration::InvalidError> {
        {
            // These index and shift in the register accessors, so a stream
            // that gets them wrong must not be accepted.
            let regs = self.regs.borrow();
            if regs.rx_fifo_len > QUP_FIFO_DEPTH
                || regs.rx_last_byte_cnt >= registers::BYTES_PER_FIFO_WORD
                || (regs.rx_last_byte_cnt > 0 && regs.rx_fifo_len == 0)
                || (regs.rx_tail_closed && regs.rx_last_byte_cnt == 0)
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
        let irq_active = (regs.m_irq_status_level() & regs.geni_m_irq_en) != 0
            || (regs.geni_s_irq_status & regs.geni_s_irq_en) != 0
            || (regs.dma_tx_irq_stat & regs.dma_tx_irq_en) != 0
            || (regs.dma_rx_irq_stat & regs.dma_rx_irq_en) != 0;
        self.irq.set(irq_active);
    }
}

impl Drop for QupGeniUartState {
    fn drop(&mut self) {
        self.clear_tx_watch();
        if self.dma_as_ready.get() {
            // SAFETY: the address space was initialized by realize
            unsafe { address_space_destroy(self.dma_as.0.as_mut_ptr()) };
        }
    }
}

// VMState implementation for QupGeniUartRegs
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
        vmstate_of!(QupGeniUartRegs, rx_fifo),
        vmstate_of!(QupGeniUartRegs, rx_fifo_len),
        vmstate_of!(QupGeniUartRegs, rx_last_byte_cnt),
        vmstate_of!(QupGeniUartRegs, rx_tail_closed),
        vmstate_of!(QupGeniUartRegs, tx_remaining),
        vmstate_of!(QupGeniUartRegs, tx_enabled),
        vmstate_of!(QupGeniUartRegs, rx_enabled),
        vmstate_of!(QupGeniUartRegs, dma_mode_enabled),
        vmstate_of!(QupGeniUartRegs, dma_tx_ptr_l),
        vmstate_of!(QupGeniUartRegs, dma_tx_ptr_h),
        vmstate_of!(QupGeniUartRegs, dma_tx_attr),
        vmstate_of!(QupGeniUartRegs, dma_tx_len),
        vmstate_of!(QupGeniUartRegs, dma_tx_sent),
        vmstate_of!(QupGeniUartRegs, dma_tx_irq_en),
        vmstate_of!(QupGeniUartRegs, dma_tx_irq_stat),
        vmstate_of!(QupGeniUartRegs, dma_rx_ptr_l),
        vmstate_of!(QupGeniUartRegs, dma_rx_ptr_h),
        vmstate_of!(QupGeniUartRegs, dma_rx_attr),
        vmstate_of!(QupGeniUartRegs, dma_rx_len),
        vmstate_of!(QupGeniUartRegs, dma_rx_len_in),
        vmstate_of!(QupGeniUartRegs, dma_rx_irq_en),
        vmstate_of!(QupGeniUartRegs, dma_rx_irq_stat),
        vmstate_of!(QupGeniUartRegs, dma_tx_armed),
        vmstate_of!(QupGeniUartRegs, dma_tx_active),
        vmstate_of!(QupGeniUartRegs, dma_rx_active),
        })
        .build()
);

// VMState implementation for QupGeniUartState
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
