/*
 * Qualcomm QCS6490 SoC Memory Map
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

enum {
    QCS6490_FLASH,
    QCS6490_MEM,
    QCS6490_GIC_DIST,
    QCS6490_GIC_REDIST,
    /* UARTs */
    QCS6490_UART0,
    QCS6490_UART1,
    QCS6490_UART2,
    QCS6490_UART3,
    QCS6490_UART4,
    QCS6490_UART5,
    QCS6490_UART6,
    QCS6490_UART7,
    QCS6490_UART8,
    QCS6490_UART9,
    QCS6490_UART10,
    QCS6490_UART11,
    QCS6490_UART12,
    QCS6490_UART13,
    QCS6490_UART14,
    QCS6490_UART15,
    /* Reserved memory regions */
    QCS6490_XBL_MEM,
    QCS6490_CDSP_SECURE_HEAP,
    QCS6490_CAMERA_MEM,
    QCS6490_WPSS_MEM,
    QCS6490_ADSP_MEM,
    QCS6490_CDSP_MEM,
    QCS6490_VIDEO_MEM,
    QCS6490_CVP_MEM,
    QCS6490_IPA_FW_MEM,
    QCS6490_GPU_MICROCODE,
    QCS6490_TRUSTZONE_MEM,
    QCS6490_QTEE_MEM,
    QCS6490_TRUSTED_APPS_MEM,
    /* Major subsystems */
    QCS6490_TLMM,
    QCS6490_GCC_REG,
    QCS6490_MDSS,
    QCS6490_VENUS,
    QCS6490_GPU,
    QCS6490_LPASS,
    QCS6490_UFS_HC,
    QCS6490_UFS_PHY,
    QCS6490_SDHC1,
    QCS6490_SDHC2,
    QCS6490_USB3_PRIM,
    QCS6490_USB3_SEC,
    QCS6490_PCIE0,
    QCS6490_PCIE1,
    QCS6490_WIFI,
    QCS6490_BLUETOOTH,
    /* PMICs and power management */
    QCS6490_RPMH_RSC,
    QCS6490_SPMI_CONTROLLER,
    QCS6490_PM7325,
    QCS6490_PM8350C,
    QCS6490_PMK8350,
    QCS6490_PM7250B,
    QCS6490_PMR735A,
    /* Additional QUP and other peripherals */
    QCS6490_QUP0,
    QCS6490_QUP1,
    QCS6490_CAMERA_CC,
    QCS6490_DISP_CC,
    QCS6490_GPU_CC,
    QCS6490_MSS,
    QCS6490_THERMAL,
    /* PCIe regions */
    QCS6490_PCIE_ECAM,
    QCS6490_PCIE_MMIO,
    QCS6490_PCIE_PIO,
};

static const MemMapEntry qcs6490_memmap[] = {
    [QCS6490_FLASH]      = { 0x00000000, 0x08000000 }, /* 128MB flash */
    [QCS6490_MEM]        = { 0x80000000, 0x80000000 }, /* 2GB DDR */
    [QCS6490_GIC_DIST]   = { 0x17a00000, 0x00010000 }, /* GIC Distributor */
    [QCS6490_GIC_REDIST] = { 0x17a60000, 0x00100000 }, /* GIC Redistributor */

    /* UARTs - QUP GENI UARTs */
    [QCS6490_UART0]      = { 0x00980000, 0x00004000 }, /* UART0 */
    [QCS6490_UART1]      = { 0x00984000, 0x00004000 }, /* UART1 */
    [QCS6490_UART2]      = { 0x00988000, 0x00004000 }, /* UART2 */
    [QCS6490_UART3]      = { 0x0098c000, 0x00004000 }, /* UART3 */
    [QCS6490_UART4]      = { 0x00990000, 0x00004000 }, /* UART4 */
    [QCS6490_UART5]      = { 0x00994000, 0x00004000 }, /* UART5 (debug) */
    [QCS6490_UART6]      = { 0x00998000, 0x00004000 }, /* UART6 */
    [QCS6490_UART7]      = { 0x0099c000, 0x00004000 }, /* UART7 (BT) */
    [QCS6490_UART8]      = { 0x00a80000, 0x00004000 }, /* UART8 */
    [QCS6490_UART9]      = { 0x00a84000, 0x00004000 }, /* UART9 */
    [QCS6490_UART10]     = { 0x00a88000, 0x00004000 }, /* UART10 */
    [QCS6490_UART11]     = { 0x00a8c000, 0x00004000 }, /* UART11 */
    [QCS6490_UART12]     = { 0x00a90000, 0x00004000 }, /* UART12 */
    [QCS6490_UART13]     = { 0x00a94000, 0x00004000 }, /* UART13 */
    [QCS6490_UART14]     = { 0x00a98000, 0x00004000 }, /* UART14 */
    [QCS6490_UART15]     = { 0x00a9c000, 0x00004000 }, /* UART15 */

    /* Reserved memory regions */
    [QCS6490_XBL_MEM]    = { 0x80700000, 0x00100000 }, /* XBL bootloader */
    [QCS6490_CDSP_SECURE_HEAP] = { 0x81800000, 0x01e00000 }, /* CDSP heap */
    [QCS6490_CAMERA_MEM] = { 0x84300000, 0x00500000 }, /* Camera memory */
    [QCS6490_WPSS_MEM]   = { 0x84800000, 0x01900000 }, /* WPSS memory */
    [QCS6490_ADSP_MEM]   = { 0x86100000, 0x02800000 }, /* ADSP memory */
    [QCS6490_CDSP_MEM]   = { 0x88900000, 0x01e00000 }, /* CDSP memory */
    [QCS6490_VIDEO_MEM]  = { 0x8a700000, 0x00700000 }, /* Video memory */
    [QCS6490_CVP_MEM]    = { 0x8ae00000, 0x00500000 }, /* CVP memory */
    [QCS6490_IPA_FW_MEM] = { 0x8b300000, 0x00010000 }, /* IPA firmware */
    [QCS6490_GPU_MICROCODE] = { 0x8b31a000, 0x00002000 }, /* GPU code */
    [QCS6490_TRUSTZONE_MEM] = { 0xc0000000, 0x00100000 }, /* TrustZone */
    [QCS6490_QTEE_MEM]   = { 0xc1300000, 0x00500000 }, /* QTEE */
    [QCS6490_TRUSTED_APPS_MEM] = { 0xc1800000, 0x01c00000 }, /* Trusted apps */

    /* Major subsystems */
    [QCS6490_TLMM]       = { 0x0f100000, 0x00300000 }, /* TLMM GPIO */
    [QCS6490_GCC_REG]    = { 0x00100000, 0x001f0000 }, /* Global Clock */
    [QCS6490_MDSS]       = { 0x0ae00000, 0x00200000 }, /* Display Subsys */
    [QCS6490_VENUS]      = { 0x0aa00000, 0x00200000 }, /* Venus video codec */
    [QCS6490_GPU]        = { 0x03d00000, 0x00100000 }, /* Adreno GPU */
    [QCS6490_LPASS]      = { 0x03000000, 0x00500000 }, /* Audio Subsys */
    [QCS6490_UFS_HC]     = { 0x01d84000, 0x00003000 }, /* UFS Host Controller */
    [QCS6490_UFS_PHY]    = { 0x01d87000, 0x00001000 }, /* UFS PHY */
    [QCS6490_SDHC1]      = { 0x07c4000,  0x00001000 }, /* SDHC1 controller */
    [QCS6490_SDHC2]      = { 0x08804000, 0x00001000 }, /* SDHC2 controller */
    [QCS6490_USB3_PRIM]  = { 0x0a600000, 0x00100000 }, /* USB3 Primary */
    [QCS6490_USB3_SEC]   = { 0x08c00000, 0x00100000 }, /* USB3 Secondary */
    [QCS6490_PCIE0]      = { 0x01c08000, 0x00004000 }, /* PCIe 0 PARF regs */
    [QCS6490_PCIE1]      = { 0x01c0a000, 0x00002000 }, /* PCIe 1 PARF regs */
    [QCS6490_WIFI]       = { 0x17a10000, 0x00010000 }, /* WiFi subsystem */
    [QCS6490_BLUETOOTH]  = { 0x0a700000, 0x00100000 }, /* Bluetooth */

    /* PMICs and power management */
    [QCS6490_RPMH_RSC]   = { 0x18200000, 0x00100000 }, /* RPMH RSC */
    [QCS6490_SPMI_CONTROLLER] = { 0x0c440000, 0x00010000 }, /* SPMI Controller */
    [QCS6490_PM7325]     = { 0x0c400000, 0x00010000 }, /* PM7325 PMIC */
    [QCS6490_PM8350C]    = { 0x0c450000, 0x00010000 }, /* PM8350C PMIC */
    [QCS6490_PMK8350]    = { 0x0c000000, 0x00010000 }, /* PMK8350 PMIC */
    [QCS6490_PM7250B]    = { 0x0c800000, 0x00010000 }, /* PM7250B PMIC */
    [QCS6490_PMR735A]    = { 0x0c880000, 0x00010000 }, /* PMR735A PMIC */

    /* QUP and other peripherals */
    [QCS6490_QUP0]       = { 0x00900000, 0x00080000 }, /* QUP0 subsystem */
    [QCS6490_QUP1]       = { 0x00a00000, 0x000a0000 }, /* QUP1 subsystem */
    [QCS6490_CAMERA_CC]  = { 0x0ad00000, 0x00010000 }, /* Camera Clock */
    [QCS6490_DISP_CC]    = { 0x0af00000, 0x00020000 }, /* Display Clock */
    [QCS6490_GPU_CC]     = { 0x03d90000, 0x00009000 }, /* GPU Clock */
    [QCS6490_MSS]        = { 0x04080000, 0x00100000 }, /* Modem subsystem */
    [QCS6490_THERMAL]    = { 0x0c263000, 0x00001000 }, /* Thermal sensors */

    /* PCIe regions */
    [QCS6490_PCIE_ECAM]  = { 0x01d00000, 0x00100000 }, /* PCIe ECAM */
    [QCS6490_PCIE_MMIO]  = { 0x20000000, 0x10000000 }, /* PCIe MMIO */
    [QCS6490_PCIE_PIO]   = { 0x30000000, 0x00010000 }, /* PCIe PIO */
};

/* IRQ map - using GIC SPI numbers */
static const int qcs6490_irqmap[] = {
    [QCS6490_UART0]  = 355,  /* GIC_SPI 355 */
    [QCS6490_UART1]  = 356,  /* GIC_SPI 356 */
    [QCS6490_UART2]  = 357,  /* GIC_SPI 357 */
    [QCS6490_UART3]  = 358,  /* GIC_SPI 358 */
    [QCS6490_UART4]  = 359,  /* GIC_SPI 359 */
    [QCS6490_UART5]  = 360,  /* GIC_SPI 360 */
    [QCS6490_UART6]  = 361,  /* GIC_SPI 361 */
    [QCS6490_UART7]  = 362,  /* GIC_SPI 362 */
    [QCS6490_UART8]  = 363,  /* GIC_SPI 363 */
    [QCS6490_UART9]  = 364,  /* GIC_SPI 364 */
    [QCS6490_UART10] = 365,  /* GIC_SPI 365 */
    [QCS6490_UART11] = 366,  /* GIC_SPI 366 */
    [QCS6490_UART12] = 367,  /* GIC_SPI 367 */
    [QCS6490_UART13] = 368,  /* GIC_SPI 368 */
    [QCS6490_UART14] = 369,  /* GIC_SPI 369 */
    [QCS6490_UART15] = 370,  /* GIC_SPI 370 */
    [QCS6490_USB3_PRIM] = 274,  /* GIC_SPI 274 */
    [QCS6490_USB3_SEC] = 275,   /* GIC_SPI 275 */
};

/* PCIe IRQ map - 4 consecutive IRQs for PCI INTA/B/C/D */
static const int qcs6490_pcie_irqs[] = { 371, 372, 373, 374 };
