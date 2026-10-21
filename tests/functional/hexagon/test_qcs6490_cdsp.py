#!/usr/bin/env python3
#
# Boot Linux on the QCS6490 CDSP machine, under the H2 hypervisor
#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset, skipBigDataTest
from qemu_test import exec_command_and_wait_for_pattern
from qemu_test import wait_for_console_pattern


class Qcs6490CdspLinuxTest(LinuxKernelTest):
    ASSET_KERNEL = \
        Asset('https://artifacts.codelinaro.org/artifactory'
           '/codelinaro-toolchain-for-hexagon/23.1.2/qcs6490-cdsp/vmlinux',
              '1908e9c684df4eb44d700c4096e1b2f73af6652a874e377cd8c6af0f5760d66a')
    ASSET_INITRD = \
        Asset('https://artifacts.codelinaro.org/artifactory'
           '/codelinaro-toolchain-for-hexagon/23.1.2/qcs6490-cdsp/initrd',
              '2b958945b38db54145051f76b886f42dd5dee70db399f6222ccc54808d6bdd6b')
    ASSET_H2 = \
        Asset('https://github.com/qualcomm/qemu-hexagon-testing/releases'
              '/download/v0.2.17b-h2/loadlinux_qcs6490-cdsp',
              'ad667005c7ae286de5a79f79613341f6e928da74cea31ced14772d5a94cb1716')

    @skipBigDataTest()
    def test_qcs6490_cdsp_linux(self):
        self.set_machine('qcs6490-cdsp')

        firmware = self.ASSET_H2.fetch()
        kernel_path = self.ASSET_KERNEL.fetch()
        initrd_path = self.ASSET_INITRD.fetch()
        self.vm.set_console()

        self.vm.add_args(
            '-bios', str(firmware),
            '-kernel', kernel_path,
            '-initrd', initrd_path,
            '-append', 'console=ttyAMA0 mem=240M lpj=89124080 rdinit=/init',
        )
        self.vm.launch()

        wait_for_console_pattern(self, 'Please press Enter to activate this '
                                 'console.', failure_message='Kernel panic')
        exec_command_and_wait_for_pattern(self, '', '# ')

        exec_command_and_wait_for_pattern(self, 'echo cpus=$(nproc)',
                                          'cpus=6')
        exec_command_and_wait_for_pattern(self, 'echo arch=$(uname -m)',
                                          'arch=hexagon')


if __name__ == '__main__':
    LinuxKernelTest.main()
