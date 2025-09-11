// Copyright (c) Qualcomm Technologies, Inc.
// SPDX-License-Identifier: GPL-2.0-or-later

use std::ffi::CStr;
use std::ffi::c_void;

use bql::BqlRefCell;
use chardev::{CharBackend, Chardev, Event};
use common::uninit_field_mut;
use hwcore::{
    DeviceImpl, DeviceMethods, DeviceState, IRQState, InterruptSource, ResetType,
    ResettablePhasesImpl, SysBusDevice, SysBusDeviceImpl, SysBusDeviceMethods,
};
use migration::{
    impl_vmstate_forward, impl_vmstate_struct, VMStateDescription, VMStateDescriptionBuilder,
    vmstate_fields, vmstate_of,
};
use qom::{prelude::*, ObjectImpl, Owned, ParentField, ParentInit};
use system::{
    bindings::{
        address_space_memory, address_space_rw, AddressSpace, MEMTX_DECODE_ERROR,
        MEMTX_ERROR, MEMTX_OK,
    },
    hwaddr, MemoryRegion, MemoryRegionOps, MemoryRegionOpsBuilder, MEMTXATTRS_UNSPECIFIED,
};
use util::{self, log::Log, log_mask_ln};

use crate::registers::{self, RegisterOffset, UART_FIFO_DEPTH_WORDS};

// FIFO depth is 256 bytes = 64 words
pub const QUP_FIFO_DEPTH: u32 = UART_FIFO_DEPTH_WORDS;

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

#[allow(dead_code)]
impl Fifo {
    const fn len(&self) -> u32 {
        self.0.len() as u32
    }
}

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
    pub geni_m_cmd_ctrl: u32,
    pub geni_m_irq_status: u32,
    pub geni_m_irq_en: u32,
    pub geni_s_cmd0: u32,
    pub geni_s_cmd_ctrl: u32,
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

    // FIFOs
    pub tx_fifo: Fifo,
    pub rx_fifo: Fifo,
    pub tx_fifo_len: u32,
    pub rx_fifo_len: u32,

    // DMA registers
    pub dma_tx_ptr_l: u32,
    pub dma_tx_ptr_h: u32,
    pub dma_tx_attr: u32,
    pub dma_tx_len: u32,
    pub dma_tx_irq_en: u32,
    pub dma_tx_irq_stat: u32,
    pub dma_rx_ptr_l: u32,
    pub dma_rx_ptr_h: u32,
    pub dma_rx_attr: u32,
    pub dma_rx_len: u32,
    pub dma_rx_irq_en: u32,
    pub dma_rx_irq_stat: u32,

    // State
    pub tx_enabled: bool,
    pub rx_enabled: bool,
    pub clk_rate: u32,
    pub dma_mode_enabled: bool,
    pub dma_tx_active: bool,

    // Stale timeout support
    pub rx_stale_timeout_active: bool,
    pub dma_rx_active: bool,
}

#[repr(C)]
#[derive(qemu_macros::Object, hwcore::Device)]
/// QUP GENI UART Device Model in QEMU
pub struct QupGeniUartState {
    pub parent_obj: ParentField<SysBusDevice>,
    pub iomem: MemoryRegion,
    #[property(rename = "chardev")]
    pub char_backend: CharBackend,
    pub regs: BqlRefCell<QupGeniUartRegs>,
    pub irq: InterruptSource,
}

// Size constraint check (if there's a C version to compare against)
// static_assert!(size_of::<QupGeniUartState>() <= size_of::<system::bindings::QupGeniUartState>());

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
    pub fn read(&mut self, offset: RegisterOffset) -> (bool, u32) {
        use RegisterOffset::*;

        let mut update_irq = false;
        let result = match offset {
            SeGeniStatus => self.geni_status,
            GeniFwRevisionRo => {
                // Return UART protocol in firmware revision
                (registers::GENI_SE_UART << registers::FW_REV_PROTOCOL_SHFT) | 0x0001
            }
            SeGeniMIrqStatus => self.geni_m_irq_status,
            SeGeniMIrqEn => self.geni_m_irq_en,
            SeGeniMCmd0 => self.geni_m_cmd0,
            SeGeniSCmd0 => self.geni_s_cmd0,
            SeGeniTxWatermarkReg => self.geni_tx_watermark,
            SeGeniRxWatermarkReg => self.geni_rx_watermark,
            SeGeniRxRfrWatermarkReg => self.geni_rx_rfr_watermark,
            SeUartTxWordLen => self.uart_tx_word_len,
            SeUartRxWordLen => self.uart_rx_word_len,
            SeUartTxStopBitLen => self.uart_tx_stop_bit_len,
            SeUartRxStaleCnt => self.uart_rx_stale_cnt,
            SeUartTxTransCfg => self.uart_tx_trans_cfg,
            SeUartRxTransCfg => self.uart_rx_trans_cfg,
            SeUartLoopbackCfg => self.uart_loopback_cfg,
            SeUartIoMacroCtrl => self.uart_io_macro_ctrl,
            SeUartManualRfr => self.uart_manual_rfr,
            SeGeniTxPackingCfg0 => self.geni_tx_packing_cfg0,
            SeGeniTxPackingCfg1 => self.geni_tx_packing_cfg1,
            SeGeniRxPackingCfg0 => self.geni_rx_packing_cfg0,
            SeGeniRxPackingCfg1 => self.geni_rx_packing_cfg1,
            SeGeniDmaModeEn => self.dma_mode_enabled as u32,
            SeGeniTxFifoStatus => {
                // Return TX FIFO word count
                self.tx_fifo_len & registers::TX_FIFO_WC
            }
            SeGeniRxFifoStatus => {
                // Return RX FIFO word count
                self.rx_fifo_len & registers::RX_FIFO_WC_MSK
            }
            SeGeniTxFifoN => {
                log_mask_ln!(Log::GuestError, "QUP GENI UART: TX FIFO read not supported");
                0
            }
            SeGeniRxFifoN => {
                if self.rx_fifo_len > 0 {
                    let data = self.rx_fifo[0];
                    // Shift FIFO contents
                    for i in 0..self.rx_fifo_len - 1 {
                        self.rx_fifo[i] = self.rx_fifo[i + 1];
                    }
                    self.rx_fifo_len -= 1;
                    update_irq = true;
                    data
                } else {
                    0
                }
            }
            SeHwParam0 => {
                // TX FIFO parameters: width=32, depth=64
                (registers::UART_FIFO_WIDTH_BITS << registers::TX_FIFO_WIDTH_SHFT)
                    | (registers::UART_FIFO_DEPTH_WORDS << registers::TX_FIFO_DEPTH_SHFT)
            }
            SeHwParam1 => {
                // RX FIFO parameters: width=32, depth=64
                (registers::UART_FIFO_WIDTH_BITS << registers::RX_FIFO_WIDTH_SHFT)
                    | (registers::UART_FIFO_DEPTH_WORDS << registers::RX_FIFO_DEPTH_SHFT)
            }
            // DMA register reads
            SeDmaTxPtrL => self.dma_tx_ptr_l,
            SeDmaTxPtrH => self.dma_tx_ptr_h,
            SeDmaTxAttr => self.dma_tx_attr,
            SeDmaTxLen => self.dma_tx_len,
            SeDmaTxIrqEn => self.dma_tx_irq_en,
            SeDmaTxIrqStat => self.dma_tx_irq_stat,
            SeDmaRxPtrL => self.dma_rx_ptr_l,
            SeDmaRxPtrH => self.dma_rx_ptr_h,
            SeDmaRxAttr => self.dma_rx_attr,
            SeDmaRxLen => self.dma_rx_len,
            SeDmaRxIrqEn => self.dma_rx_irq_en,
            SeDmaRxIrqStat => self.dma_rx_irq_stat,
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
        char_backend: &CharBackend,
    ) -> bool {
        use RegisterOffset::*;

        match offset {
            SeGeniMIrqClear => {
                self.geni_m_irq_status &= !value;
                return true;
            }
            SeGeniMIrqEn => {
                self.geni_m_irq_en = value;
                return true;
            }
            SeGeniTxWatermarkReg => {
                self.geni_tx_watermark = value;
            }
            SeGeniRxWatermarkReg => {
                self.geni_rx_watermark = value;
            }
            SeGeniRxRfrWatermarkReg => {
                self.geni_rx_rfr_watermark = value;
            }
            SeUartTxWordLen => {
                self.uart_tx_word_len = value;
            }
            SeUartRxWordLen => {
                self.uart_rx_word_len = value;
            }
            SeUartTxStopBitLen => {
                self.uart_tx_stop_bit_len = value;
            }
            SeUartRxStaleCnt => {
                self.uart_rx_stale_cnt = value;
                // Linux sets this to 160 (DEFAULT_BITS_PER_CHAR * STALE_TIMEOUT)
                // When RX data is present but incomplete, start stale timeout
                if self.rx_fifo_len > 0 && value > 0 {
                    self.start_rx_stale_timeout();
                }
            }
            SeUartTxTransCfg => {
                self.uart_tx_trans_cfg = value;
            }
            SeUartRxTransCfg => {
                self.uart_rx_trans_cfg = value;
            }
            SeUartLoopbackCfg => {
                self.uart_loopback_cfg = value;
            }
            SeUartIoMacroCtrl => {
                self.uart_io_macro_ctrl = value;
                // Handle pin swapping configuration that Linux drivers expect
                // Log pin configuration for debugging (no Log::Debug available)
            }
            SeUartManualRfr => {
                self.uart_manual_rfr = value;
            }
            SeGeniTxPackingCfg0 => {
                self.geni_tx_packing_cfg0 = value;
            }
            SeGeniTxPackingCfg1 => {
                self.geni_tx_packing_cfg1 = value;
            }
            SeGeniRxPackingCfg0 => {
                self.geni_rx_packing_cfg0 = value;
            }
            SeGeniRxPackingCfg1 => {
                self.geni_rx_packing_cfg1 = value;
            }
            SeGeniCfgSeqStart => {
                // Configuration sequence start - could indicate protocol reconfiguration attempt
                if value & 1 != 0 {
                    log_mask_ln!(Log::Unimp,
                        "QUP GENI UART: Configuration sequence start attempted - protocol switching not supported");
                }
            }
            SeGeniDmaModeEn => {
                self.dma_mode_enabled = (value & 1) != 0;
                log_mask_ln!(
                    Log::Unimp,
                    "QUP GENI UART: DMA mode {} (basic support)",
                    if self.dma_mode_enabled {
                        "enabled"
                    } else {
                        "disabled"
                    }
                );
            }
            SeGeniMCmd0 => {
                self.geni_m_cmd0 = value;
                let opcode = (value & registers::M_OPCODE_MSK) >> registers::M_OPCODE_SHFT;
                let params = value & registers::M_PARAMS_MSK;

                match opcode {
                    registers::UART_START_TX => {
                        self.geni_status |= registers::M_GENI_CMD_ACTIVE;
                        self.handle_tx_command(params, char_backend);
                        // Command completes immediately in this simplified implementation
                        self.geni_status &= !registers::M_GENI_CMD_ACTIVE;
                        self.geni_m_irq_status |= registers::M_CMD_DONE_EN;
                        return true;
                    }
                    registers::UART_ABORT => {
                        // Abort TX operation - clear active flags and reset state
                        self.geni_status &= !registers::M_GENI_CMD_ACTIVE;
                        self.tx_enabled = false;
                        self.dma_tx_active = false;
                        self.tx_fifo_len = 0; // Clear pending TX data
                        self.geni_m_irq_status |= registers::M_CMD_DONE_EN;
                        // TX operation aborted (no debug logging available)
                        return true;
                    }
                    // Note: SPI_RX_ONLY = 0x2, but UART_ABORT is also 0x2, so this pattern is unreachable
                    // Detect SPI command opcodes (skipping SPI_RX_ONLY since it conflicts with UART_ABORT)
                    registers::SPI_CS_ASSERT
                    | registers::SPI_CS_DEASSERT
                    | registers::SPI_SCK_ONLY => {
                        log_mask_ln!(Log::Unimp,
                            "QUP GENI UART: SPI command opcode 0x{:x} attempted - device only supports UART mode",
                            opcode);
                    }
                    // Detect I2C command opcodes
                    registers::I2C_WRITE_READ
                    | registers::I2C_ADDR_ONLY
                    | registers::I2C_BUS_CLEAR => {
                        log_mask_ln!(Log::Unimp,
                            "QUP GENI UART: I2C command opcode 0x{:x} attempted - device only supports UART mode",
                            opcode);
                    }
                    // Handle overlap: SPI_TX_RX and I2C_STOP_ON_BUS both have value 0x7
                    0x7 => {
                        log_mask_ln!(Log::Unimp,
                            "QUP GENI UART: SPI/I2C command opcode 0x{:x} attempted - device only supports UART mode",
                            opcode);
                    }
                    // Note: SPI_TX_ONLY, I2C_WRITE, I2C_READ have same opcode as UART_START_TX (0x1)
                    // so we can't distinguish them - this is expected hardware behavior
                    0x2..=0x6 | 0x8..=0x1F => {
                        log_mask_ln!(Log::Unimp,
                            "QUP GENI UART: Unknown/unsupported command opcode 0x{:x} - device only supports UART mode",
                            opcode);
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
            SeGeniSCmd0 => {
                self.geni_s_cmd0 = value;
                let opcode = (value & registers::M_OPCODE_MSK) >> registers::M_OPCODE_SHFT;

                match opcode {
                    registers::UART_START_READ => {
                        self.geni_status |= registers::S_GENI_CMD_ACTIVE;
                        self.rx_enabled = true;
                    }
                    registers::UART_ABORT_READ => {
                        // Abort RX operation - clear active flags and reset state
                        self.geni_status &= !registers::S_GENI_CMD_ACTIVE;
                        self.rx_enabled = false;
                        self.dma_rx_active = false;
                        self.rx_fifo_len = 0; // Clear pending RX data
                        self.rx_stale_timeout_active = false; // Cancel stale timeout
                        self.geni_s_irq_status |= registers::S_CMD_DONE_EN;
                        // RX operation aborted (no debug logging available)
                        return true;
                    }
                    // Similar detection for secondary sequencer commands
                    0x3..=0x1F => {
                        log_mask_ln!(Log::Unimp,
                            "QUP GENI UART: Non-UART secondary command opcode 0x{:x} attempted - device only supports UART mode",
                            opcode);
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
            SeGeniTxFifoN => {
                if self.tx_fifo_len < QUP_FIFO_DEPTH {
                    let idx = self.tx_fifo_len;
                    self.tx_fifo[idx] = value;
                    self.tx_fifo_len += 1;

                    // Check watermark
                    if self.tx_fifo_len <= self.geni_tx_watermark {
                        self.geni_m_irq_status |= registers::M_TX_FIFO_WATERMARK_EN;
                        return true;
                    }
                }
            }
            // DMA register writes
            SeDmaTxPtrL => {
                self.dma_tx_ptr_l = value;
            }
            SeDmaTxPtrH => {
                self.dma_tx_ptr_h = value;
            }
            SeDmaTxAttr => {
                self.dma_tx_attr = value;
            }
            SeDmaTxLen => {
                self.dma_tx_len = value;
                if self.dma_mode_enabled && value > 0 {
                    // Start DMA TX transfer
                    self.start_dma_tx_transfer(char_backend);
                }
            }
            SeDmaTxIrqEn => {
                self.dma_tx_irq_en = value;
                return true;
            }
            SeDmaTxIrqEnSet => {
                self.dma_tx_irq_en |= value;
                return true;
            }
            SeDmaTxIrqEnClr => {
                self.dma_tx_irq_en &= !value;
                return true;
            }
            SeDmaTxIrqClr => {
                self.dma_tx_irq_stat &= !value;
                return true;
            }
            SeDmaRxPtrL => {
                self.dma_rx_ptr_l = value;
            }
            SeDmaRxPtrH => {
                self.dma_rx_ptr_h = value;
            }
            SeDmaRxAttr => {
                self.dma_rx_attr = value;
            }
            SeDmaRxLen => {
                self.dma_rx_len = value;
                if self.dma_mode_enabled && value > 0 {
                    // Prepare for DMA RX transfer
                    self.dma_rx_active = true;
                }
            }
            SeDmaRxIrqEn => {
                self.dma_rx_irq_en = value;
                return true;
            }
            SeDmaRxIrqEnSet => {
                self.dma_rx_irq_en |= value;
                return true;
            }
            SeDmaRxIrqEnClr => {
                self.dma_rx_irq_en &= !value;
                return true;
            }
            SeDmaRxIrqClr => {
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

    fn start_dma_tx_transfer(&mut self, char_backend: &CharBackend) {
        let dma_addr = ((self.dma_tx_ptr_h as u64) << 32) | (self.dma_tx_ptr_l as u64);
        let length = self.dma_tx_len;

        if length == 0 {
            return;
        }

        self.dma_tx_active = true;

        // Allocate buffer for DMA transfer
        let mut buffer = vec![0u8; length as usize];

        // Read data from guest memory
        let result = unsafe {
            address_space_rw(
                &raw mut address_space_memory as *mut AddressSpace,
                dma_addr as hwaddr,
                MEMTXATTRS_UNSPECIFIED,
                buffer.as_mut_ptr() as *mut c_void,
                length as hwaddr,
                false, // false = read
            )
        };

        match result {
            MEMTX_OK => {
                // Send data to character backend
                if let Err(_) = char_backend.write_all(&buffer) {
                    log_mask_ln!(
                        Log::GuestError,
                        "QUP GENI UART: DMA TX character backend write failed"
                    );
                    self.dma_tx_irq_stat |= registers::DMA_AHB_ERR_EN;
                } else {
                    log_mask_ln!(
                        Log::Unimp,
                        "QUP GENI UART: DMA TX transfer of {} bytes completed",
                        length
                    );
                    self.dma_tx_irq_stat |= registers::DMA_DONE_EN | registers::DMA_EOT_EN;
                }
            }
            MEMTX_ERROR => {
                log_mask_ln!(
                    Log::GuestError,
                    "QUP GENI UART: DMA TX memory read error at address 0x{:x}",
                    dma_addr
                );
                self.dma_tx_irq_stat |= registers::DMA_AHB_ERR_EN;
            }
            MEMTX_DECODE_ERROR => {
                log_mask_ln!(
                    Log::GuestError,
                    "QUP GENI UART: DMA TX memory decode error at address 0x{:x}",
                    dma_addr
                );
                self.dma_tx_irq_stat |= registers::DMA_AHB_ERR_EN;
            }
            _ => {
                log_mask_ln!(
                    Log::GuestError,
                    "QUP GENI UART: DMA TX unknown memory error {} at address 0x{:x}",
                    result,
                    dma_addr
                );
                self.dma_tx_irq_stat |= registers::DMA_AHB_ERR_EN;
            }
        }

        self.dma_tx_active = false;
        // Clear transfer length to indicate completion
        self.dma_tx_len = 0;
    }

    fn handle_dma_rx_transfer(&mut self, data: &[u8]) -> bool {
        if !self.dma_rx_active || self.dma_rx_len == 0 {
            return false;
        }

        let dma_addr = ((self.dma_rx_ptr_h as u64) << 32) | (self.dma_rx_ptr_l as u64);
        let max_len = std::cmp::min(data.len() as u32, self.dma_rx_len);

        if max_len == 0 {
            return false;
        }

        // Write data to guest memory
        let result = unsafe {
            address_space_rw(
                &raw mut address_space_memory as *mut AddressSpace,
                dma_addr as hwaddr,
                MEMTXATTRS_UNSPECIFIED,
                data.as_ptr() as *mut c_void,
                max_len as hwaddr,
                true, // true = write
            )
        };

        match result {
            MEMTX_OK => {
                log_mask_ln!(
                    Log::Unimp,
                    "QUP GENI UART: DMA RX transfer of {} bytes completed",
                    max_len
                );
                self.dma_rx_irq_stat |= registers::DMA_DONE_EN | registers::DMA_EOT_EN;

                // Update DMA pointer and remaining length
                self.dma_rx_ptr_l = (dma_addr + max_len as u64) as u32;
                if (dma_addr + max_len as u64) > u32::MAX as u64 {
                    self.dma_rx_ptr_h += 1;
                }
                self.dma_rx_len -= max_len;

                if self.dma_rx_len == 0 {
                    self.dma_rx_active = false;
                }

                return true;
            }
            MEMTX_ERROR => {
                log_mask_ln!(
                    Log::GuestError,
                    "QUP GENI UART: DMA RX memory write error at address 0x{:x}",
                    dma_addr
                );
                self.dma_rx_irq_stat |= registers::DMA_AHB_ERR_EN;
            }
            MEMTX_DECODE_ERROR => {
                log_mask_ln!(
                    Log::GuestError,
                    "QUP GENI UART: DMA RX memory decode error at address 0x{:x}",
                    dma_addr
                );
                self.dma_rx_irq_stat |= registers::DMA_AHB_ERR_EN;
            }
            _ => {
                log_mask_ln!(
                    Log::GuestError,
                    "QUP GENI UART: DMA RX unknown memory error {} at address 0x{:x}",
                    result,
                    dma_addr
                );
                self.dma_rx_irq_stat |= registers::DMA_AHB_ERR_EN;
            }
        }

        self.dma_rx_active = false;
        false
    }

    fn handle_tx_command(&mut self, length: u32, char_backend: &CharBackend) {
        if self.dma_mode_enabled {
            // In DMA mode, use the configured DMA registers for transfer
            if self.dma_tx_len > 0 {
                self.start_dma_tx_transfer(char_backend);
            } else {
                log_mask_ln!(
                    Log::GuestError,
                    "QUP GENI UART: DMA TX command but no DMA length configured"
                );
            }
            // Command completion is handled by DMA transfer function
            self.geni_m_irq_status |= registers::M_CMD_DONE_EN;
            return;
        }

        // FIFO mode operation
        let bytes_to_send = std::cmp::min(length, self.tx_fifo_len * 4) as usize;

        if bytes_to_send > 0 {
            let mut tx_data = Vec::with_capacity(bytes_to_send);

            for i in 0..(bytes_to_send + 3) / 4 {
                if i < self.tx_fifo_len as usize {
                    let word = self.tx_fifo[i as u32];
                    for j in 0..4 {
                        if tx_data.len() < bytes_to_send {
                            tx_data.push(((word >> (j * 8)) & 0xff) as u8);
                        }
                    }
                }
            }

            // Handle loopback mode
            if self.uart_loopback_cfg & 0x1 != 0 {
                // In loopback mode, TX data goes to RX FIFO
                for chunk in tx_data.chunks(4) {
                    if self.rx_fifo_len < QUP_FIFO_DEPTH {
                        let mut word = 0u32;
                        for (i, &byte) in chunk.iter().enumerate() {
                            word |= (byte as u32) << (i * 8);
                        }
                        let idx = self.rx_fifo_len;
                        self.rx_fifo[idx] = word;
                        self.rx_fifo_len += 1;
                    }
                }

                // Generate RX watermark interrupt
                if self.rx_fifo_len >= self.geni_rx_watermark {
                    self.geni_m_irq_status |= registers::M_RX_FIFO_WATERMARK_EN;
                }
            } else {
                // Normal mode - send to character backend
                let _ = char_backend.write_all(&tx_data);
            }

            // Clear TX FIFO
            self.tx_fifo_len = 0;
        }

        // Check TX FIFO watermark interrupt - triggers when FIFO level is at or below watermark
        // After processing, TX FIFO should be empty (0 words) which is below watermark
        if self.tx_fifo_len <= self.geni_tx_watermark {
            self.geni_m_irq_status |= registers::M_TX_FIFO_WATERMARK_EN;
        }
    }

    pub fn reset(&mut self) {
        self.geni_status = 0;
        self.geni_m_cmd0 = 0;
        self.geni_m_cmd_ctrl = 0;
        self.geni_m_irq_status = 0;
        self.geni_m_irq_en = 0;
        self.geni_s_cmd0 = 0;
        self.geni_s_cmd_ctrl = 0;
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

        self.tx_fifo_len = 0;
        self.rx_fifo_len = 0;
        self.tx_enabled = false;
        self.rx_enabled = false;
        self.clk_rate = 19200000; // Default 19.2MHz clock
        self.dma_mode_enabled = false;

        // DMA registers
        self.dma_tx_ptr_l = 0;
        self.dma_tx_ptr_h = 0;
        self.dma_tx_attr = 0;
        self.dma_tx_len = 0;
        self.dma_tx_irq_en = 0;
        self.dma_tx_irq_stat = 0;
        self.dma_rx_ptr_l = 0;
        self.dma_rx_ptr_h = 0;
        self.dma_rx_attr = 0;
        self.dma_rx_len = 0;
        self.dma_rx_irq_en = 0;
        self.dma_rx_irq_stat = 0;
        self.dma_tx_active = false;
        self.dma_rx_active = false;

        // Stale timeout state
        self.rx_stale_timeout_active = false;
    }

    fn start_rx_stale_timeout(&mut self) {
        // Start timeout based on stale count and baud rate
        // For now, use a simple timeout calculation
        if self.uart_rx_stale_cnt > 0 && !self.rx_stale_timeout_active {
            self.rx_stale_timeout_active = true;
            // Note: In a real implementation, would start a QEMU timer here
            // For this fix, we'll trigger stale timeout on next receive if needed
        }
    }

    #[allow(dead_code)]
    fn handle_rx_stale_timeout(&mut self) -> bool {
        if self.rx_stale_timeout_active && self.rx_fifo_len > 0 {
            // Generate stale timeout interrupt if RX data is waiting
            self.geni_m_irq_status |= registers::M_RX_FIFO_LAST_EN;
            self.rx_stale_timeout_active = false;
            return true;
        }
        false
    }
}

impl QupGeniUartState {
    /// Initializes a pre-allocated, uninitialized instance of `QupGeniUartState`.
    ///
    /// # Safety
    ///
    /// `self` must point to a correctly sized and aligned location for the
    /// `QupGeniUartState` type. It must not be called more than once on the same
    /// location/instance. All its fields are expected to hold uninitialized
    /// values with the sole exception of `parent_obj`.
    unsafe fn init(mut this: ParentInit<Self>) {
        static QUP_GENI_UART_OPS: MemoryRegionOps<QupGeniUartState> =
            MemoryRegionOpsBuilder::<QupGeniUartState>::new()
                .read(&QupGeniUartState::read)
                .write(&QupGeniUartState::write)
                .native_endian()
                .impl_sizes(4, 4)
                .build();

        // SAFETY: this and this.iomem are guaranteed to be valid at this point
        MemoryRegion::init_io(
            &mut uninit_field_mut!(*this, iomem),
            &QUP_GENI_UART_OPS,
            "qup-geni-uart",
            0x6000, // 24KB as per device tree
        );

        uninit_field_mut!(*this, regs).write(Default::default());
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
                    self.char_backend.accept_input();
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
                .write(reg, value as u32, &self.char_backend);
            if update_irq {
                self.update_irq();
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
        if regs.rx_enabled {
            QUP_FIFO_DEPTH - regs.rx_fifo_len
        } else {
            0
        }
    }

    fn receive(&self, buf: &[u8]) {
        let mut regs = self.regs.borrow_mut();
        if !regs.rx_enabled || (regs.uart_loopback_cfg & 0x1 != 0) {
            return;
        }

        // Handle DMA mode if enabled
        if regs.dma_mode_enabled {
            let handled = regs.handle_dma_rx_transfer(buf);
            if handled {
                // Release the BqlRefCell before calling self.update_irq()
                drop(regs);
                self.update_irq();
                return;
            }
            // If DMA didn't handle it, fall back to FIFO mode
        }

        let mut update_irq = false;

        // Pack bytes into 32-bit words and add to RX FIFO
        for chunk in buf.chunks(4) {
            if regs.rx_fifo_len < QUP_FIFO_DEPTH {
                let mut word = 0u32;
                for (i, &byte) in chunk.iter().enumerate() {
                    word |= (byte as u32) << (i * 8);
                }
                let idx = regs.rx_fifo_len;
                regs.rx_fifo[idx] = word;
                regs.rx_fifo_len += 1;

                // Check for watermark interrupt
                if regs.rx_fifo_len >= regs.geni_rx_watermark {
                    regs.geni_m_irq_status |= registers::M_RX_FIFO_WATERMARK_EN;
                    update_irq = true;
                }
            }
        }

        // Release the BqlRefCell before calling self.update_irq()
        drop(regs);
        if update_irq {
            self.update_irq();
        }
    }

    fn event(&self, _event: Event) {
        // Handle character device events if needed
    }

    fn realize(&self) -> util::Result<()> {
        self.char_backend
            .enable_handlers(self, Self::can_receive, Self::receive, Self::event);
        Ok(())
    }

    fn reset_hold(&self, _type: ResetType) {
        self.regs.borrow_mut().reset();
    }

    fn update_irq(&self) {
        let regs = self.regs.borrow();
        let irq_active = (regs.geni_m_irq_status & regs.geni_m_irq_en) != 0;
        self.irq.set(irq_active);
    }
}

/// # Safety
///
/// We expect the FFI user of this function to pass a valid pointer for `chr`
/// and `irq`.
#[no_mangle]
pub unsafe extern "C" fn qup_geni_uart_create(
    addr: u64,
    irq: *mut IRQState,
    chr: *mut Chardev,
) -> *mut DeviceState {
    // SAFETY: The callers promise that they have owned references.
    let irq = unsafe { Owned::<IRQState>::from(&*irq) };

    let dev = QupGeniUartState::new();
    if !chr.is_null() {
        let chr = unsafe { Owned::<Chardev>::from(&*chr) };
        dev.prop_set_chr("chardev", &chr);
    }
    dev.sysbus_realize();
    dev.mmio_map(0, addr);
    dev.connect_irq(0, &irq);

    // The pointer is kept alive by the QOM tree; drop the owned ref
    dev.as_mut_ptr()
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
        vmstate_of!(QupGeniUartRegs, geni_m_cmd_ctrl),
        vmstate_of!(QupGeniUartRegs, geni_m_irq_status),
        vmstate_of!(QupGeniUartRegs, geni_m_irq_en),
        vmstate_of!(QupGeniUartRegs, geni_s_cmd0),
        vmstate_of!(QupGeniUartRegs, geni_s_cmd_ctrl),
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
        vmstate_of!(QupGeniUartRegs, tx_fifo),
        vmstate_of!(QupGeniUartRegs, rx_fifo),
        vmstate_of!(QupGeniUartRegs, tx_fifo_len),
        vmstate_of!(QupGeniUartRegs, rx_fifo_len),
        vmstate_of!(QupGeniUartRegs, tx_enabled),
        vmstate_of!(QupGeniUartRegs, rx_enabled),
        vmstate_of!(QupGeniUartRegs, clk_rate),
        vmstate_of!(QupGeniUartRegs, dma_mode_enabled),
        vmstate_of!(QupGeniUartRegs, dma_tx_ptr_l),
        vmstate_of!(QupGeniUartRegs, dma_tx_ptr_h),
        vmstate_of!(QupGeniUartRegs, dma_tx_attr),
        vmstate_of!(QupGeniUartRegs, dma_tx_len),
        vmstate_of!(QupGeniUartRegs, dma_tx_irq_en),
        vmstate_of!(QupGeniUartRegs, dma_tx_irq_stat),
        vmstate_of!(QupGeniUartRegs, dma_rx_ptr_l),
        vmstate_of!(QupGeniUartRegs, dma_rx_ptr_h),
        vmstate_of!(QupGeniUartRegs, dma_rx_attr),
        vmstate_of!(QupGeniUartRegs, dma_rx_len),
        vmstate_of!(QupGeniUartRegs, dma_rx_irq_en),
        vmstate_of!(QupGeniUartRegs, dma_rx_irq_stat),
        vmstate_of!(QupGeniUartRegs, dma_tx_active),
        vmstate_of!(QupGeniUartRegs, dma_rx_active),
        vmstate_of!(QupGeniUartRegs, rx_stale_timeout_active),
        })
        .build()
);

// VMState implementation for QupGeniUartState
pub const VMSTATE_QUP_GENI_UART: VMStateDescription<QupGeniUartState> =
    VMStateDescriptionBuilder::<QupGeniUartState>::new()
        .name(c"qup_geni_uart")
        .version_id(1)
        .minimum_version_id(1)
        .fields(vmstate_fields! {
            vmstate_of!(QupGeniUartState, regs),
        })
        .build();

