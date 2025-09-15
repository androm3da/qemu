/*
 * Qualcomm QCS6490 board emulation
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/boot.h"
#include "hw/arm/primecell.h"
#include "hw/boards.h"
#include "hw/char/pl011.h"
#include "hw/char/qup_geni_uart.h"
#include "system/memory.h"
#include "system/ioport.h"
#include "hw/intc/arm_gicv3_common.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "net/net.h"
#include "system/device_tree.h"
#include "system/kvm.h"
#include "target/arm/cpu.h"
#include "target/arm/cpu-features.h"
#include "target/arm/multiprocessing.h"
#include "hw/loader.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "system/system.h"
#include "hw/misc/unimp.h"
#include "qobject/qlist.h"
#include "hw/pci-host/qcs6490-pcie.h"
#include "hw/pci/pci.h"
#include "hw/ufs/ufs.h"
#include "hw/misc/rpmh-rsc.h"
#include "hw/misc/spmi-controller.h"
#include "hw/misc/pm7325.h"
#include "hw/misc/pmk8350.h"
#include "hw/misc/pm8350c.h"
#include "hw/misc/pm7250b.h"
#include "hw/misc/pmr735a.h"
#include "hw/misc/qcs6490_gcc.h"
#include "hw/usb/hcd-dwc3.h"
#include "hw/block/flash.h"
#include "qemu/datadir.h"
#include "system/block-backend.h"

#define TYPE_QCS6490_MACHINE MACHINE_TYPE_NAME("qcs6490")

typedef struct QCS6490MachineState {
    MachineState parent_obj;

    DeviceState *gic;
    PL011State *uart[1];  /* Keep UART0 as PL011 for compatibility */
    DeviceState *qup_uart[15];  /* QUP GENI UARTs 1-15 */
    struct arm_boot_info bootinfo;
    MemoryRegion sysmem;
    MemoryRegion secure_sysmem;
    DeviceState *pcie_host;
    PCIBus *pcie_bus;
    DeviceState *ufs_dev;
    PFlashCFI01 *flash[2];
    bool firmware_loaded;
    Notifier machine_done;
} QCS6490MachineState;

OBJECT_DECLARE_SIMPLE_TYPE(QCS6490MachineState, QCS6490_MACHINE)

/* Include memory map definitions */
#include "hw/arm/qcs6490-memmap.inc"

#define NUM_IRQS 1024
#define PPI(n) (n + 16)
#define PCI_NUM_PINS 4
#define QCS6490_FLASH_SECTOR_SIZE (256 * KiB)

static void create_pcie(QCS6490MachineState *s)
{
    DeviceState *dev;
    SysBusDevice *sysbus;
    PCIHostState *pci;
    MemoryRegion *ecam_alias, *mmio_alias;
    MemoryRegion *ecam_reg, *mmio_reg;
    int i;

    dev = qdev_new(TYPE_QCS6490_PCIE_HOST);
    sysbus = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(sysbus, &error_fatal);

    pci = PCI_HOST_BRIDGE(dev);
    s->pcie_host = dev;
    s->pcie_bus = pci->bus;

    /* Connect PCIe IRQs to GIC */
    for (i = 0; i < PCI_NUM_PINS; i++) {
        sysbus_connect_irq(sysbus, i, qdev_get_gpio_in(s->gic,
                           qcs6490_pcie_irqs[i]));
        qcs6490_pcie_set_irq_num(QCS6490_PCIE_HOST(dev), i,
                                 qcs6490_pcie_irqs[i]);
    }

    /*
     * Map PARF registers - these are specific to QCS6490 PCIe controller
     * Use higher priority to coexist with ECAM space that overlaps region
     */
    MemoryRegion *parf_region = sysbus_mmio_get_region(sysbus, 3);
    memory_region_add_subregion_overlap(&s->sysmem,
                                        qcs6490_memmap[QCS6490_PCIE0].base,
                                        parf_region, 1);

    /* Map ECAM space (configuration) */
    ecam_alias = g_new0(MemoryRegion, 1);
    ecam_reg = sysbus_mmio_get_region(sysbus, 0);
    memory_region_init_alias(ecam_alias, OBJECT(dev), "pcie-ecam",
                             ecam_reg, 0,
                             qcs6490_memmap[QCS6490_PCIE_ECAM].size);
    /* Map ECAM at address expected by qtest (0x01c00000) for compatibility */
    memory_region_add_subregion(&s->sysmem, 0x01c00000, ecam_alias);

    /* Map MMIO space */
    mmio_alias = g_new0(MemoryRegion, 1);
    mmio_reg = sysbus_mmio_get_region(sysbus, 1);
    memory_region_init_alias(mmio_alias, OBJECT(dev), "pcie-mmio",
                             mmio_reg, qcs6490_memmap[QCS6490_PCIE_MMIO].base,
                             qcs6490_memmap[QCS6490_PCIE_MMIO].size);
    memory_region_add_subregion(&s->sysmem,
                                qcs6490_memmap[QCS6490_PCIE_MMIO].base,
                                mmio_alias);

    /* Map PIO space */
    sysbus_mmio_map(sysbus, 2, qcs6490_memmap[QCS6490_PCIE_PIO].base);
}

static void create_ufs(QCS6490MachineState *s)
{
    DeviceState *dev;

    if (!s->pcie_bus) {
        error_report("UFS requires PCIe support");
        return;
    }

    /* Create UFS Host Controller */
    dev = qdev_new(TYPE_UFS);
    qdev_prop_set_string(dev, "serial", "QCS6490-UFS");
    qdev_prop_set_uint8(dev, "nutrs", 32);     /* Max transfer request slots */
    qdev_prop_set_uint8(dev, "nutmrs", 8);     /* Max task mgmt request slots */
    object_property_set_bool(OBJECT(dev), "mcq", true, &error_fatal);
    qdev_prop_set_uint8(dev, "mcq-maxq", 8);   /* 8 MCQ queues */

    pci_realize_and_unref(PCI_DEVICE(dev), s->pcie_bus, &error_fatal);
    s->ufs_dev = dev;
}

static PFlashCFI01 *qcs6490_flash_create1(QCS6490MachineState *s,
                                          const char *name,
                                          const char *alias_prop_name)
{
    DeviceState *dev = qdev_new(TYPE_PFLASH_CFI01);

    qdev_prop_set_uint64(dev, "sector-length", QCS6490_FLASH_SECTOR_SIZE);
    qdev_prop_set_uint8(dev, "width", 4);
    qdev_prop_set_uint8(dev, "device-width", 2);
    qdev_prop_set_bit(dev, "big-endian", false);
    qdev_prop_set_uint16(dev, "id0", 0x89);
    qdev_prop_set_uint16(dev, "id1", 0x18);
    qdev_prop_set_uint16(dev, "id2", 0x00);
    qdev_prop_set_uint16(dev, "id3", 0x00);
    qdev_prop_set_string(dev, "name", name);
    object_property_add_child(OBJECT(s), name, OBJECT(dev));
    object_property_add_alias(OBJECT(s), alias_prop_name,
                              OBJECT(dev), "drive");
    return PFLASH_CFI01(dev);
}

static void qcs6490_flash_create(QCS6490MachineState *s)
{
    s->flash[0] = qcs6490_flash_create1(s, "qcs6490.flash0", "pflash0");
    s->flash[1] = qcs6490_flash_create1(s, "qcs6490.flash1", "pflash1");
}

static void qcs6490_flash_map1(PFlashCFI01 *flash,
                               hwaddr base, hwaddr size,
                               MemoryRegion *sysmem)
{
    DeviceState *dev = DEVICE(flash);

    assert(QEMU_IS_ALIGNED(size, QCS6490_FLASH_SECTOR_SIZE));
    assert(size / QCS6490_FLASH_SECTOR_SIZE <= UINT32_MAX);
    qdev_prop_set_uint32(dev, "num-blocks", size / QCS6490_FLASH_SECTOR_SIZE);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    memory_region_add_subregion(sysmem, base,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0));
}

static void qcs6490_flash_map(QCS6490MachineState *s)
{
    hwaddr flashsize = qcs6490_memmap[QCS6490_FLASH].size / 2;
    hwaddr flashbase = qcs6490_memmap[QCS6490_FLASH].base;

    qcs6490_flash_map1(s->flash[0], flashbase, flashsize, &s->sysmem);
    qcs6490_flash_map1(s->flash[1], flashbase + flashsize, flashsize, &s->sysmem);
}

static bool qcs6490_firmware_init(QCS6490MachineState *s)
{
    MachineState *machine = MACHINE(s);
    BlockBackend *pflash_blk0;
    char *bios_name;
    int image_size;
    hwaddr firmware_addr = qcs6490_memmap[QCS6490_FLASH].base;

    /* Map flash devices */
    qcs6490_flash_map(s);

    pflash_blk0 = pflash_cfi01_get_blk(s->flash[0]);

    bios_name = machine->firmware;
    if (bios_name) {
        char *fname;
        MemoryRegion *mr;

        if (pflash_blk0) {
            error_report("The contents of the first flash device may be "
                         "specified with -bios or with -drive if=pflash... "
                         "but you cannot use both options at once");
            exit(1);
        }

        fname = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
        if (!fname) {
            error_report("Could not find ROM image '%s'", bios_name);
            exit(1);
        }
        mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(s->flash[0]), 0);
        image_size = load_image_mr(fname, mr);
        g_free(fname);
        if (image_size < 0) {
            error_report("Could not load ROM image '%s'", bios_name);
            exit(1);
        }

        /* Set CPU reset addresses */
        CPUState *cpu;
        CPU_FOREACH(cpu) {
            ARMCPU *armcpu = ARM_CPU(cpu);
            armcpu->rvbar_prop = firmware_addr;
            armcpu->reset_cbar = firmware_addr;
        }
    }

    return pflash_blk0 || bios_name;
}

static void qcs6490_machine_done(Notifier *notifier, void *data)
{
    QCS6490MachineState *s = container_of(notifier, QCS6490MachineState,
                                          machine_done);
    MachineState *ms = MACHINE(s);

    if (s->firmware_loaded) {
        /* Skip kernel loading if firmware is loaded */
        return;
    }

    /* Load kernel if no firmware */
    arm_load_kernel(ARM_CPU(first_cpu), ms, &s->bootinfo);
}

static void init(MachineState *machine)
{
    QCS6490MachineState *s = QCS6490_MACHINE(machine);
    CPUState *cpu;
    ARMCPU *armcpu;
    DeviceState *gicdev;
    SysBusDevice *gicbus;
    int n;
    qemu_irq irq;

    memory_region_init(&s->sysmem, OBJECT(machine), "sysmem", UINT64_MAX);
    memory_region_init(&s->secure_sysmem, OBJECT(machine), "secure-sysmem",
                       UINT64_MAX);

    /* Create flash devices */
    qcs6490_flash_create(s);

    static const uint64_t mpidr_values[] = {
        0x00000000, 0x00000100, 0x00000200, 0x00000300,
        0x00000400, 0x00000500, 0x00000600, 0x00000700,
    };

    for (n = 0; n < machine->smp.cpus; n++) {
        Object *cpuobj = object_new(machine->cpu_type);

        object_property_set_link(cpuobj, "memory", OBJECT(&s->sysmem),
                                 &error_abort);
        object_property_set_link(cpuobj, "secure-memory", OBJECT(&s->secure_sysmem),
                                 &error_abort);
        object_property_set_bool(cpuobj, "has_el3", true, &error_abort);
        object_property_set_bool(cpuobj, "has_el2", true, &error_abort);
        object_property_set_int(cpuobj, "psci-conduit", QEMU_PSCI_CONDUIT_SMC,
                                &error_fatal);
        object_property_set_int(cpuobj, "mp-affinity",
                                mpidr_values[n],
                                &error_fatal);

        qdev_realize(DEVICE(cpuobj), NULL, &error_fatal);
    }


    /* Create RAM */
    memory_region_add_subregion(&s->sysmem, qcs6490_memmap[QCS6490_MEM].base,
                                machine->ram);

    /* Create GICv3 */
    gicdev = qdev_new("arm-gicv3");
    qdev_prop_set_uint32(gicdev, "num-cpu", machine->smp.cpus);
    qdev_prop_set_uint32(gicdev, "num-irq", 992); /* Divisible by 32 */
    qdev_prop_set_uint32(gicdev, "revision", 3);
    qdev_prop_set_bit(gicdev, "has-security-extensions", true);

    /* Set redistributor region count */
    QList *redist_region_count = qlist_new();
    qlist_append_int(redist_region_count, machine->smp.cpus);
    qdev_prop_set_array(gicdev, "redist-region-count", redist_region_count);

    gicbus = SYS_BUS_DEVICE(gicdev);
    sysbus_realize_and_unref(gicbus, &error_fatal);
    sysbus_mmio_map(gicbus, 0, qcs6490_memmap[QCS6490_GIC_DIST].base);
    sysbus_mmio_map(gicbus, 1, qcs6490_memmap[QCS6490_GIC_REDIST].base);

    s->gic = gicdev;

    n = 0;
    CPU_FOREACH(cpu) {
        armcpu = ARM_CPU(cpu);

        for (int i = 0; i < 4; i++) {
            sysbus_connect_irq(gicbus, n * 4 + i,
                              qdev_get_gpio_in(DEVICE(armcpu), i));
        }
        sysbus_connect_irq(gicbus, machine->smp.cpus * 4 + n,
                          qdev_get_gpio_in(DEVICE(armcpu), ARM_CPU_VIRQ));
        qdev_connect_gpio_out_named(DEVICE(armcpu), "pmu-interrupt", 0,
                                   qdev_get_gpio_in(s->gic,
                                                   PPI(7)));
        n++;
    }

    /* UART0 (debug) */
    s->uart[0] = PL011(qdev_new(TYPE_PL011));
    qdev_prop_set_chr(DEVICE(s->uart[0]), "chardev", serial_hd(0));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(s->uart[0]), &error_fatal);
    memory_region_add_subregion(&s->sysmem, qcs6490_memmap[QCS6490_UART0].base,
                               sysbus_mmio_get_region(
                                   SYS_BUS_DEVICE(s->uart[0]),
                                                      0));
    irq = qdev_get_gpio_in(s->gic, qcs6490_irqmap[QCS6490_UART0]);
    sysbus_connect_irq(SYS_BUS_DEVICE(s->uart[0]), 0, irq);

    create_pcie(s);
    create_ufs(s);
    create_unimplemented_device("qcs6490.xbl-mem",
                               qcs6490_memmap[QCS6490_XBL_MEM].base,
                               qcs6490_memmap[QCS6490_XBL_MEM].size);
    create_unimplemented_device("qcs6490.cdsp-secure-heap",
                               qcs6490_memmap[QCS6490_CDSP_SECURE_HEAP].base,
                               qcs6490_memmap[QCS6490_CDSP_SECURE_HEAP].size);
    create_unimplemented_device("qcs6490.camera-mem",
                               qcs6490_memmap[QCS6490_CAMERA_MEM].base,
                               qcs6490_memmap[QCS6490_CAMERA_MEM].size);
    create_unimplemented_device("qcs6490.wpss-mem",
                               qcs6490_memmap[QCS6490_WPSS_MEM].base,
                               qcs6490_memmap[QCS6490_WPSS_MEM].size);
    create_unimplemented_device("qcs6490.adsp-mem",
                               qcs6490_memmap[QCS6490_ADSP_MEM].base,
                               qcs6490_memmap[QCS6490_ADSP_MEM].size);
    create_unimplemented_device("qcs6490.cdsp-mem",
                               qcs6490_memmap[QCS6490_CDSP_MEM].base,
                               qcs6490_memmap[QCS6490_CDSP_MEM].size);
    create_unimplemented_device("qcs6490.video-mem",
                               qcs6490_memmap[QCS6490_VIDEO_MEM].base,
                               qcs6490_memmap[QCS6490_VIDEO_MEM].size);
    create_unimplemented_device("qcs6490.cvp-mem",
                               qcs6490_memmap[QCS6490_CVP_MEM].base,
                               qcs6490_memmap[QCS6490_CVP_MEM].size);
    create_unimplemented_device("qcs6490.ipa-fw-mem",
                               qcs6490_memmap[QCS6490_IPA_FW_MEM].base,
                               qcs6490_memmap[QCS6490_IPA_FW_MEM].size);
    create_unimplemented_device("qcs6490.gpu-microcode",
                               qcs6490_memmap[QCS6490_GPU_MICROCODE].base,
                               qcs6490_memmap[QCS6490_GPU_MICROCODE].size);
    create_unimplemented_device("qcs6490.trustzone-mem",
                               qcs6490_memmap[QCS6490_TRUSTZONE_MEM].base,
                               qcs6490_memmap[QCS6490_TRUSTZONE_MEM].size);
    create_unimplemented_device("qcs6490.qtee-mem",
                               qcs6490_memmap[QCS6490_QTEE_MEM].base,
                               qcs6490_memmap[QCS6490_QTEE_MEM].size);
    create_unimplemented_device("qcs6490.trusted-apps-mem",
                               qcs6490_memmap[QCS6490_TRUSTED_APPS_MEM].base,
                               qcs6490_memmap[QCS6490_TRUSTED_APPS_MEM].size);

    for (int i = 1; i <= 15; i++) {
        int uart_idx = QCS6490_UART0 + i;
        qemu_irq uart_irq = qdev_get_gpio_in(s->gic, qcs6490_irqmap[uart_idx]);
        Chardev *chr = serial_hd(i);  /* Get serial device for UART i */

        DeviceState *qup_dev = qdev_new(TYPE_QUP_GENI_UART);
        if (chr) {
            qdev_prop_set_chr(qup_dev, "chardev", chr);
        }
        sysbus_realize_and_unref(SYS_BUS_DEVICE(qup_dev), &error_fatal);

        memory_region_add_subregion(&s->sysmem, qcs6490_memmap[uart_idx].base,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(qup_dev), 0));
        sysbus_connect_irq(SYS_BUS_DEVICE(qup_dev), 0, uart_irq);

        s->qup_uart[i - 1] = qup_dev;
    }

    create_unimplemented_device("qcs6490.tlmm",
                               qcs6490_memmap[QCS6490_TLMM].base,
                               qcs6490_memmap[QCS6490_TLMM].size);

    /* GCC (Global Clock Controller) */
    DeviceState *gcc_dev = qdev_new(TYPE_QCS6490_GCC_DEV);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(gcc_dev), &error_fatal);
    memory_region_add_subregion(&s->sysmem,
                               qcs6490_memmap[QCS6490_GCC_REG].base,
                               sysbus_mmio_get_region(SYS_BUS_DEVICE(gcc_dev),
                                                      0));
    create_unimplemented_device("qcs6490.mdss",
                               qcs6490_memmap[QCS6490_MDSS].base,
                               qcs6490_memmap[QCS6490_MDSS].size);
    create_unimplemented_device("qcs6490.venus",
                               qcs6490_memmap[QCS6490_VENUS].base,
                               qcs6490_memmap[QCS6490_VENUS].size);
    create_unimplemented_device("qcs6490.gpu",
                               qcs6490_memmap[QCS6490_GPU].base,
                               qcs6490_memmap[QCS6490_GPU].size);
    create_unimplemented_device("qcs6490.lpass",
                               qcs6490_memmap[QCS6490_LPASS].base,
                               qcs6490_memmap[QCS6490_LPASS].size);

    create_unimplemented_device("qcs6490.ufs-phy",
                               qcs6490_memmap[QCS6490_UFS_PHY].base,
                               qcs6490_memmap[QCS6490_UFS_PHY].size);
    create_unimplemented_device("qcs6490.sdhc1",
                               qcs6490_memmap[QCS6490_SDHC1].base,
                               qcs6490_memmap[QCS6490_SDHC1].size);
    create_unimplemented_device("qcs6490.sdhc2",
                               qcs6490_memmap[QCS6490_SDHC2].base,
                               qcs6490_memmap[QCS6490_SDHC2].size);

    /* USB3 Primary Controller - Device mode (a600000.ssusb) */
    DeviceState *usb3_prim = qdev_new(TYPE_USB_DWC3);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(usb3_prim), &error_fatal);
    memory_region_add_subregion(&s->sysmem,
                               qcs6490_memmap[QCS6490_USB3_PRIM].base,
                               sysbus_mmio_get_region(SYS_BUS_DEVICE(usb3_prim),
                                                      0));

    /* USB3 Secondary Controller - Host mode (8c00000.hsusb) */
    DeviceState *usb3_sec = qdev_new(TYPE_USB_DWC3);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(usb3_sec), &error_fatal);
    memory_region_add_subregion(&s->sysmem,
                               qcs6490_memmap[QCS6490_USB3_SEC].base,
                               sysbus_mmio_get_region(SYS_BUS_DEVICE(usb3_sec),
                                                      0));
    create_unimplemented_device("qcs6490.wifi",
                               qcs6490_memmap[QCS6490_WIFI].base,
                               qcs6490_memmap[QCS6490_WIFI].size);
    create_unimplemented_device("qcs6490.bluetooth",
                               qcs6490_memmap[QCS6490_BLUETOOTH].base,
                               qcs6490_memmap[QCS6490_BLUETOOTH].size);

    /* Power management */
    DeviceState *rpmh_rsc = qdev_new(TYPE_RPMH_RSC);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(rpmh_rsc), &error_fatal);
    memory_region_add_subregion(&s->sysmem,
                               qcs6490_memmap[QCS6490_RPMH_RSC].base,
                               sysbus_mmio_get_region(SYS_BUS_DEVICE(rpmh_rsc),
                                                      0));
    memory_region_add_subregion(&s->sysmem,
                               qcs6490_memmap[QCS6490_RPMH_RSC].base + 0xD00,
                               sysbus_mmio_get_region(SYS_BUS_DEVICE(rpmh_rsc),
                                                      1));

    /* SPMI Controller - Interface for PMICs */
    DeviceState *spmi_controller = qdev_new(TYPE_SPMI_CONTROLLER);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(spmi_controller), &error_fatal);
    memory_region_add_subregion(&s->sysmem,
                               qcs6490_memmap[QCS6490_SPMI_CONTROLLER].base,
                               sysbus_mmio_get_region(
                                   SYS_BUS_DEVICE(spmi_controller), 0));

    /* PMK8350 - Master PMIC (SPMI 0) */
    DeviceState *pmk8350 = qdev_new(TYPE_PMK8350);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(pmk8350), &error_fatal);
    pmk8350_register_with_spmi(PMK8350(pmk8350),
                               SPMI_CONTROLLER(spmi_controller), 0);

    /* PM7325 - Primary PMIC (SPMI 1) */
    DeviceState *pm7325 = qdev_new(TYPE_PM7325);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(pm7325), &error_fatal);
    pm7325_register_with_spmi(PM7325(pm7325),
                              SPMI_CONTROLLER(spmi_controller), 1);

    /* PM8350C - Camera/Display PMIC (SPMI 2) */
    DeviceState *pm8350c = qdev_new(TYPE_PM8350C);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(pm8350c), &error_fatal);
    pm8350c_register_with_spmi(PM8350C(pm8350c),
                               SPMI_CONTROLLER(spmi_controller), 2);

    /* PM7250B - Battery Management PMIC (SPMI 3) */
    DeviceState *pm7250b = qdev_new(TYPE_PM7250B);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(pm7250b), &error_fatal);
    pm7250b_register_with_spmi(PM7250B(pm7250b),
                               SPMI_CONTROLLER(spmi_controller), 3);

    /* PMR735A - Peripheral power PMIC (SPMI 4) */
    DeviceState *pmr735a = qdev_new(TYPE_PMR735A);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(pmr735a), &error_fatal);
    pmr735a_register_with_spmi(PMR735A(pmr735a),
                               SPMI_CONTROLLER(spmi_controller), 4);

    /* QUP and other peripherals */
    create_unimplemented_device("qcs6490.qup0",
                               qcs6490_memmap[QCS6490_QUP0].base,
                               qcs6490_memmap[QCS6490_QUP0].size);
    create_unimplemented_device("qcs6490.qup1",
                               qcs6490_memmap[QCS6490_QUP1].base,
                               qcs6490_memmap[QCS6490_QUP1].size);
    create_unimplemented_device("qcs6490.camera-cc",
                               qcs6490_memmap[QCS6490_CAMERA_CC].base,
                               qcs6490_memmap[QCS6490_CAMERA_CC].size);
    create_unimplemented_device("qcs6490.disp-cc",
                               qcs6490_memmap[QCS6490_DISP_CC].base,
                               qcs6490_memmap[QCS6490_DISP_CC].size);
    create_unimplemented_device("qcs6490.gpu-cc",
                               qcs6490_memmap[QCS6490_GPU_CC].base,
                               qcs6490_memmap[QCS6490_GPU_CC].size);
    create_unimplemented_device("qcs6490.mss",
                               qcs6490_memmap[QCS6490_MSS].base,
                               qcs6490_memmap[QCS6490_MSS].size);
    create_unimplemented_device("qcs6490.thermal",
                               qcs6490_memmap[QCS6490_THERMAL].base,
                               qcs6490_memmap[QCS6490_THERMAL].size);

    /* Initialize firmware/flash */
    s->firmware_loaded = qcs6490_firmware_init(s);

    /* Boot configuration */
    s->bootinfo.ram_size = machine->ram_size;
    s->bootinfo.board_id = -1;
    s->bootinfo.loader_start = qcs6490_memmap[QCS6490_MEM].base;
    s->bootinfo.psci_conduit = QEMU_PSCI_CONDUIT_SMC;
    s->bootinfo.firmware_loaded = s->firmware_loaded;

    /* If firmware is loaded, let it handle secure boot */
    if (s->firmware_loaded) {
        s->bootinfo.secure_boot = true;
    }

    /* Register machine done notifier */
    s->machine_done.notify = qcs6490_machine_done;
    qemu_add_machine_init_done_notifier(&s->machine_done);
}

static void machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Qualcomm QCS6490";
    mc->init = init;
    mc->max_cpus = 8;
    mc->default_cpus = 8;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a55");
    mc->default_ram_size = (3355808 * 1024ULL);
    mc->default_ram_id = "qcs6490.ram";
}

static const TypeInfo qcs6490_machine_info = {
    .name = TYPE_QCS6490_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(QCS6490MachineState),
    .class_init = machine_class_init,
};

static void machine_register_types(void)
{
    type_register_static(&qcs6490_machine_info);
}

type_init(machine_register_types)
