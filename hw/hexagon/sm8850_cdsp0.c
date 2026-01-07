/*
 * SM8850 CDSP0
 *
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/misc/unimp.h"
#include "hw/hexagon/sm8850_cdsp0.h"
#include "qapi/error.h"

void sm8850_cdsp0_create_unimplemented_devices(void)
{
    /* Clock Controllers */
    create_unimplemented_device("gcc", 0x100000, 0x1000);
    create_unimplemented_device("turing-cc", 0x221c8000, 0x1000);
    create_unimplemented_device("turing-q6-cc", 0x26340000, 0x1000);
    create_unimplemented_device("turing-q6-hmx-cc", 0x26348000, 0x1000);

    /* IPCC (Inter-Processor Communication Controller) */
    create_unimplemented_device("ipcc-mproc", 0x1105000, 0x1000);
    create_unimplemented_device("ipcc-compute-l0", 0x11c2000, 0x1000);
    create_unimplemented_device("ipcc-compute-l1", 0x1282000, 0x1000);
    create_unimplemented_device("ipcc-periph", 0x1342000, 0x1000);
    create_unimplemented_device("ipcc-apps", 0x1402000, 0x1000);
    create_unimplemented_device("ipcc-legacy", 0x26388004, 0x1000);

    /* Timers */
    create_unimplemented_device("timer-0", 0x263a2000, 0x1000);
    create_unimplemented_device("timer-1", 0x263a3000, 0x1000);

    /* Pin Control */
    create_unimplemented_device("pinctrl", 0xf100000, 0x1000);

    /* Debug and Trace Components */
    create_unimplemented_device("cxstmtrace", 0x16000000, 0x1000);
    create_unimplemented_device("cxstmcfg", 0x10002000, 0x1000);
    create_unimplemented_device("cxetb", 0x11305000, 0x1000);
    create_unimplemented_device("tpdm-0", 0x11181000, 0x1000);
    create_unimplemented_device("tpdm-1", 0x11182000, 0x1000);
    create_unimplemented_device("tpdm-2", 0x11185000, 0x1000);
    create_unimplemented_device("tpdm-3", 0x11186000, 0x1000);
    create_unimplemented_device("funnel-0", 0x10041000, 0x1000);
    create_unimplemented_device("funnel-1", 0x11304000, 0x1000);
    create_unimplemented_device("tpda", 0x11188000, 0x1000);

    /* System Cache */
    create_unimplemented_device("systemcache", 0x31800000, 0x800000);

    /* Hardware Lock */
    create_unimplemented_device("hwlock", 0x1f40000, 0x1000);

    /* Test Devices */
    create_unimplemented_device("kernel-test-devices", 0x0, 0x1000);
    create_unimplemented_device("test-device1", 0xf101000, 0x1000);
    create_unimplemented_device("test-device2", 0x1011000, 0x1000);
    create_unimplemented_device("test-device3", 0x0, 0x1);
    create_unimplemented_device("test-device4", 0x1, 0x1);
    create_unimplemented_device("test-device5", 0x1010000, 0x1000);

    /* Interrupt Controller */
    create_unimplemented_device("interrupt-controller", 0x10140000, 0x10000);

    /* Additional Turing/Q6SS regions based on clock controller reg ranges */
    create_unimplemented_device("turing-pll", 0x26000000, 0x8000);
    create_unimplemented_device("turing-cc-reg", 0x26008000, 0x14000);
    create_unimplemented_device("turing-q6-cc-reg", 0x26344000, 0x4000);
    create_unimplemented_device("turing-q6-acd-mnd", 0x26350000, 0x800);
    create_unimplemented_device("turing-q6-hmx-ahb2phy", 0x2634a000, 0x400);
    create_unimplemented_device("turing-q6-hmx-ahb2phy-broadcast",
                                0x2634b000, 0x400);
    create_unimplemented_device("turing-q6-hmx-cc-reg", 0x2634c000, 0x4000);
    create_unimplemented_device("turing-q6-hmx-acd-mnd", 0x26350800, 0x800);

    /* GCC (Global Clock Controller) sub-regions */
    create_unimplemented_device("gcc-gpll0", 0x100000, 0x1000);
    create_unimplemented_device("gcc-gpll1", 0x101000, 0x1000);
    create_unimplemented_device("gcc-gpll2", 0x102000, 0x1000);
    create_unimplemented_device("gcc-gpll3", 0x103000, 0x1000);
    create_unimplemented_device("gcc-gpll4", 0x104000, 0x1000);
    create_unimplemented_device("gcc-gpll5", 0x105000, 0x1000);
    create_unimplemented_device("gcc-gpll6", 0x106000, 0x1000);
    create_unimplemented_device("gcc-gpll7", 0x107000, 0x1000);
    create_unimplemented_device("gcc-gpll8", 0x108000, 0x1000);
    create_unimplemented_device("gcc-gpll9", 0x109000, 0x1000);
    create_unimplemented_device("gcc-gpll10", 0x10a000, 0x1000);
    create_unimplemented_device("gcc-gpll11", 0x10b000, 0x1000);
    create_unimplemented_device("gcc-jbist", 0x10c000, 0x1000);
    create_unimplemented_device("gcc-ahb2phy-swman", 0x10e000, 0x400);
    create_unimplemented_device("gcc-ahb2phy-broadcast", 0x10f000, 0x400);
    create_unimplemented_device("gcc-clk-ctl", 0x110000, 0x1e0000);
    create_unimplemented_device("gcc-rpu", 0x2f0000, 0x4200);

    /* Fuse Controller */
    create_unimplemented_device("fuse-controller", 0x221c8000, 0x1000);

    /* Chip info */
    create_unimplemented_device("chip-info", 0xa00000, 0x10000);
}

