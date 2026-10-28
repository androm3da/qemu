#!/usr/bin/env python3
#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset, skipBigDataTest
from qemu_test import exec_command_and_wait_for_pattern


class HexagonHVMLinuxBase(LinuxKernelTest):
    '''Boot a Linux HVM guest with QEMU as its monitor.'''
    ASSET_KERNEL = \
        Asset('https://artifacts.codelinaro.org/artifactory/'
              'codelinaro-toolchain-for-hexagon/23.1.2/qcs6490-cdsp/'
              'vmlinux',
              '1908e9c684df4eb44d700c4096e1b2f73'
              'af6652a874e377cd8c6af0f5760d66a')
    ASSET_INITRD = \
        Asset('https://artifacts.codelinaro.org/artifactory/'
              'codelinaro-toolchain-for-hexagon/23.1.2/qcs6490-cdsp/'
              'rootfs.cpio.gz',
              '2b958945b38db54145051f76b886f42d'
              'd5dee70db399f6222ccc54808d6bdd6b')

    def boot_and_check(self):
        self.set_machine('qcs6490-cdsp')

        kernel_path = self.ASSET_KERNEL.fetch()
        initrd_path = self.ASSET_INITRD.fetch()
        self.vm.set_console()

        self.vm.add_args(
            '-accel', 'tcg,thread=multi',
            '-machine', 'hvm=on',
            '-kernel', kernel_path,
            '-initrd', initrd_path,
            '-append', 'console=ttyAMA0 rdinit=/bin/sh',
        )
        self.vm.launch()

        self.wait_for_console_pattern('~ # ')
        exec_command_and_wait_for_pattern(self, 'mount -t proc proc /proc',
                                          '# ')
        exec_command_and_wait_for_pattern(self, 'mount -t sysfs sysfs /sys',
                                          '# ')

        exec_command_and_wait_for_pattern(
            self, 'cat /sys/devices/system/clocksource/clocksource0'
            '/current_clocksource', 'HVM timer')

        exec_command_and_wait_for_pattern(self, 'poweroff -f',
                                          'reboot: Power off not available: '
                                          'System halted instead')
        self.vm.wait()
        self.assertEqual(self.vm.exitcode(), 0)


class HexagonHVMQcs6490CdspLinuxTest(HexagonHVMLinuxBase):

    @skipBigDataTest()
    def test_linux_boot(self):
        self.boot_and_check()


if __name__ == '__main__':
    LinuxKernelTest.main()
