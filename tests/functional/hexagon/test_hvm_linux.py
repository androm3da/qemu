#!/usr/bin/env python3
#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset, skipBigDataTest
from qemu_test import exec_command_and_wait_for_pattern


class HexagonHVMLinuxBase(LinuxKernelTest):
    '''
    Boot Linux, a guest of the Hexagon Virtual Machine, with QEMU as the
    monitor of the VM: there is no hypervisor firmware.  QEMU handles
    hypercalls.
    '''
    MACHINE = None
    MACHINE_ARGS = []
    NUM_CPUS = 4
    ASSET_KERNEL = \
        Asset('https://artifacts.codelinaro.org/artifactory'
           '/codelinaro-toolchain-for-hexagon/23.1.0-rc1/vmlinux',
              '620859d3ebe83e6d858adac150701931197038deeb740d31d23c67da1b1e013a')
    ASSET_ROOTFS = \
        Asset('https://artifacts.codelinaro.org/artifactory'
           '/codelinaro-toolchain-for-hexagon/23.1.0-rc1/rootfs.ext2.gz',
              'c5743106d51fe4fd0693bd8da5c1b6b704ea4824a19f90a1e9324df3f847bd13')

    def boot_and_check(self):
        # Note the guest uses two consoles: the semihosting 'angel0' bootconsole,
        #  whose output goes to QEMU's stdout, and the PL011 that takes over once
        #  its driver probes.
        self.set_machine(self.MACHINE)
        self.require_netdev('user')

        kernel_path = self.ASSET_KERNEL.fetch()
        disk_path = self.uncompress(self.ASSET_ROOTFS)
        self.vm.set_console()

        self.vm.add_args(
            '-machine', 'hvm=on',
            '-kernel', kernel_path,
            '-append', 'console=ttyAMA0 root=/dev/vda rw',
            *self.MACHINE_ARGS,
            '-accel', 'tcg,thread=multi',
            '-drive', f'if=none,file={disk_path},format=raw,id=hd0',
            '-device', 'virtio-blk-device,drive=hd0',
            '-netdev', 'type=user,id=net0',
            '-device', 'virtio-net-device,netdev=net0',
        )
        self.vm.launch()

        self.wait_for_console_pattern('buildroot login:')
        exec_command_and_wait_for_pattern(self, 'root', '# ')

        exec_command_and_wait_for_pattern(self, 'echo cpus=$(nproc)',
                                          f'cpus={self.NUM_CPUS}')

        exec_command_and_wait_for_pattern(
            self, 'cat /sys/devices/system/clocksource/clocksource0'
            '/current_clocksource', 'HVM timer')

        exec_command_and_wait_for_pattern(self, 'cat /proc/partitions', 'vda')
        exec_command_and_wait_for_pattern(self, 'mount', 'on / type ext2')

        exec_command_and_wait_for_pattern(self, 'ip -4 addr show eth0',
                                          '10.0.2.15')

        # Interrupts reach every virtual CPU - each gets IPIs.
        exec_command_and_wait_for_pattern(
            self,
            "awk 'NR > 1 { for (i = 2; i <= %d; i++) if ($i > 0) c[i] = 1 }"
            " END { n = 0; for (i in c) n++; print \"irqcpus=\" n }'"
            " /proc/interrupts" % (self.NUM_CPUS + 1),
            f'irqcpus={self.NUM_CPUS}')

        exec_command_and_wait_for_pattern(self, 'poweroff',
                                          'reboot: Power down')
        self.vm.wait()
        self.assertEqual(self.vm.exitcode(), 0)


class HexagonHVMLinuxTest(HexagonHVMLinuxBase):
    MACHINE = 'virt'
    # The machine has the most threads that the v81 core does, but the
    # kernel brings up only as many as it was configured for.
    MACHINE_ARGS = ['-m', '4G', '-smp', '16']

    @skipBigDataTest()
    def test_linux_boot(self):
        self.boot_and_check()


class HexagonHVMQcs6490CdspLinuxTest(HexagonHVMLinuxBase):
    MACHINE = 'qcs6490-cdsp'

    @skipBigDataTest()
    def test_linux_boot(self):
        self.boot_and_check()


class HexagonHVMSa8775pCdspLinuxTest(HexagonHVMLinuxBase):
    MACHINE = 'sa8775p-cdsp'

    @skipBigDataTest()
    def test_linux_boot(self):
        self.boot_and_check()


class HexagonHVMSa8797pNsp0LinuxTest(HexagonHVMLinuxBase):
    MACHINE = 'sa8797p-nsp0'

    @skipBigDataTest()
    def test_linux_boot(self):
        self.boot_and_check()


class HexagonHVMSc8480xpNsp0LinuxTest(HexagonHVMLinuxBase):
    MACHINE = 'sc8480xp-nsp0'

    @skipBigDataTest()
    def test_linux_boot(self):
        self.boot_and_check()


class HexagonHVMSm8975NspLinuxTest(HexagonHVMLinuxBase):
    MACHINE = 'sm8975-nsp'

    @skipBigDataTest()
    def test_linux_boot(self):
        self.boot_and_check()


if __name__ == '__main__':
    LinuxKernelTest.main()
