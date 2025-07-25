#!/usr/bin/env python3
#
# Copyright(c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import re
from qemu_test import QemuSystemTest, Asset
from hexagon.utils import HexagonCheckError, scale_timeout

class SysTestsStandaloneTests(QemuSystemTest):

    SYSTEST_TIMEOUT_SEC = 120  # Increased for tests that may take longer

    # GitHub release URL for systests_standalone package
    ASSET_TARBALL = Asset(
        'https://github.com/androm3da/qemu-testing/releases/download/'
        'v0.1.1/systests_standalone.tar.gz',
        'd1a29e24755ddbc0036b8441b0d7ee1a44b3d4871d21a73b8b605410d3a9b976')

    def check(self, test_name: str) -> None:
        """
        Check the semihosting output from a systests_standalone test case.
        Expected pattern: Tests should write expected results via semihosting
        and exit with code 0 on success.
        """
        console_output = str(self.vm).strip()

        if self.vm.exitcode() != 0:
            raise HexagonCheckError(
                f'Test {test_name} exited with code {self.vm.exitcode()}')

        # Check for common success patterns in semihosting output
        # These patterns may need to be updated based on actual test output
        success_patterns = [
            r'PASS',
#           r'Test.*passed',
#           r'SUCCESS',
#           r'All tests passed'
        ]
        # Check for more specific failure patterns (avoid false positives)
        # Some tests output "FAIL" as part of normal test output, so we need
        # to be more specific about what constitutes a real failure
        failure_patterns = [
            r'ASSERTION.*failed',
            r'ERROR:.*',
            r'Test.*failed',
            r'Exception.*',
            r'Segmentation fault',
            r'Abort.*'
        ]

        # Look for failure patterns first
        if any(re.search(pattern, console_output, re.IGNORECASE)
               for pattern in failure_patterns):
            raise HexagonCheckError(
                f'Test {test_name} failed: found failure pattern in output')

        # Look for success patterns
        found_success = any(re.search(pattern, console_output, re.IGNORECASE)
                           for pattern in success_patterns)
        if not found_success:
            # If no explicit success/failure pattern, just check exit code
            # Some tests might only indicate success via exit code
            if self.vm.exitcode() == 0:
                found_success = True

        if not found_success:
            raise HexagonCheckError(
                f'Test {test_name} did not show clear success indication')

    def run_individual_test(self, test_name: str) -> bool:
        """
        Run a single systests_standalone test case
        """
        self.set_machine('V68N_1024')

        self.archive_extract(self.ASSET_TARBALL)

        # Find the extracted directory (name may vary based on tarball
        # structure). Look for the bin directory containing the test
        # executables.
        test_dir = None
        for root, dirs, files in os.walk(self.workdir):
            if 'bin' in dirs:
                bin_dir = os.path.join(root, 'bin')
                # Check if bin directory contains executable files (without ext)
                if any(os.access(os.path.join(bin_dir, f), os.X_OK)
                       for f in os.listdir(bin_dir)):
                    test_dir = bin_dir
                    break

        if not test_dir:
            self.fail(f'Could not find bin directory with executable files '
                      f'in {self.workdir}')

        # Find the specific test binary (executable files without extensions)
        target_bin = None
        for filename in os.listdir(test_dir):
            full_path = os.path.join(test_dir, filename)
            # Check if it's an executable file and matches the test name
            if os.access(full_path, os.X_OK) and filename == test_name:
                target_bin = full_path
                break

        self.vm.set_encoding("ISO-8859-1")
        self.set_vm_arg('-display', 'none')
        self.set_vm_arg('-kernel', target_bin)
        self.vm.launch()
        self.vm.wait(timeout=60.)
        try:
            self.check(test_name)
            return True
        except HexagonCheckError as e:
            self.fail(f'Test {test_name} failed: {str(e)}')


    # Explicitly defined test methods for all currently passing tests
    def test_badva(self) -> None:
        """Test systests/badva"""
        result = self.run_individual_test('badva')
        self.assertTrue(result, "Test badva failed")

    def test_bestwait(self) -> None:
        """Test systests/bestwait"""
        result = self.run_individual_test('bestwait')
        self.assertTrue(result, "Test bestwait failed")

    def test_checkforpriv(self) -> None:
        """Test systests/checkforpriv"""
        result = self.run_individual_test('checkforpriv')
        self.assertTrue(result, "Test checkforpriv failed")

    def test_ciad_siad(self) -> None:
        """Test systests/ciad-siad"""
        result = self.run_individual_test('ciad-siad')
        self.assertTrue(result, "Test ciad-siad failed")

    def test_double_ex(self) -> None:
        """Test systests/double_ex"""
        result = self.run_individual_test('double_ex')
        self.assertTrue(result, "Test double_ex failed")

    def test_fastint(self) -> None:
        """Test systests/fastint"""
        result = self.run_individual_test('fastint')
        self.assertTrue(result, "Test fastint failed")

    def test_fastl2vic(self) -> None:
        """Test systests/fastl2vic"""
        result = self.run_individual_test('fastl2vic')
        self.assertTrue(result, "Test fastl2vic failed")

    def test_float_excp(self) -> None:
        """Test systests/float_excp"""
        result = self.run_individual_test('float_excp')
        self.assertTrue(result, "Test float_excp failed")

    def test_framelimit(self) -> None:
        """Test systests/framelimit"""
        result = self.run_individual_test('framelimit')
        self.assertTrue(result, "Test framelimit failed")

    def test_getcwd(self) -> None:
        """Test systests/getcwd"""
        result = self.run_individual_test('getcwd')
        self.assertTrue(result, "Test getcwd failed")

    def test_gregs(self) -> None:
        """Test systests/gregs"""
        result = self.run_individual_test('gregs')
        self.assertTrue(result, "Test gregs failed")

    def test_hvx_multi(self) -> None:
        """Test systests/hvx-multi"""
        result = self.run_individual_test('hvx-multi')
        self.assertTrue(result, "Test hvx-multi failed")

    def test_hvx_64b(self) -> None:
        """Test systests/hvx_64b"""
        result = self.run_individual_test('hvx_64b')
        self.assertTrue(result, "Test hvx_64b failed")

    def test_hvx_ext(self) -> None:
        """Test systests/hvx_ext"""
        result = self.run_individual_test('hvx_ext')
        self.assertTrue(result, "Test hvx_ext failed")

    def test_int_range(self) -> None:
        """Test systests/int_range"""
        result = self.run_individual_test('int_range')
        self.assertTrue(result, "Test int_range failed")

    def test_invalid_insn_for_rev(self) -> None:
        """Test systests/invalid_insn_for_rev"""
        result = self.run_individual_test('invalid_insn_for_rev')
        self.assertTrue(result, "Test invalid_insn_for_rev failed")

    def test_invalid_opcode(self) -> None:
        """Test systests/invalid_opcode"""
        result = self.run_individual_test('invalid_opcode')
        self.assertTrue(result, "Test invalid_opcode failed")

    def test_k0lock(self) -> None:
        """Test systests/k0lock"""
        result = self.run_individual_test('k0lock')
        self.assertTrue(result, "Test k0lock failed")

    def test_k0lock_syscfg(self) -> None:
        """Test systests/k0lock-syscfg"""
        result = self.run_individual_test('k0lock-syscfg')
        self.assertTrue(result, "Test k0lock-syscfg failed")

    def test_levelint(self) -> None:
        """Test systests/levelint"""
        result = self.run_individual_test('levelint')
        self.assertTrue(result, "Test levelint failed")

    def test_llsc_on_excp(self) -> None:
        """Test systests/llsc_on_excp"""
        result = self.run_individual_test('llsc_on_excp')
        self.assertTrue(result, "Test llsc_on_excp failed")

    def test_mmu_asids(self) -> None:
        """Test systests/mmu_asids"""
        result = self.run_individual_test('mmu_asids')
        self.assertTrue(result, "Test mmu_asids failed")

    def test_mmu_multi_tlb(self) -> None:
        """Test systests/mmu_multi_tlb"""
        result = self.run_individual_test('mmu_multi_tlb')
        self.assertTrue(result, "Test mmu_multi_tlb failed")

    def test_mmu_overlap(self) -> None:
        """Test systests/mmu_overlap"""
        result = self.run_individual_test('mmu_overlap')
        self.assertTrue(result, "Test mmu_overlap failed")

    def test_mmu_page_size(self) -> None:
        """Test systests/mmu_page_size"""
        result = self.run_individual_test('mmu_page_size')
        self.assertTrue(result, "Test mmu_page_size failed")

    def test_multiple_writes(self) -> None:
        """Test systests/multiple_writes"""
        result = self.run_individual_test('multiple_writes')
        self.assertTrue(result, "Test multiple_writes failed")

    def test_pendalot(self) -> None:
        """Test systests/pendalot"""
        result = self.run_individual_test('pendalot')
        self.assertTrue(result, "Test pendalot failed")

    def test_pend_wake_wait(self) -> None:
        """Test systests/pend_wake_wait"""
        result = self.run_individual_test('pend_wake_wait')
        self.assertTrue(result, "Test pend_wake_wait failed")

    def test_qfloat_test(self) -> None:
        """Test systests/qfloat_test"""
        result = self.run_individual_test('qfloat_test')
        self.assertTrue(result, "Test qfloat_test failed")

    def test_qtimer(self) -> None:
        """Test systests/qtimer"""
        result = self.run_individual_test('qtimer')
        self.assertTrue(result, "Test qtimer failed")

    def test_qtimer_test(self) -> None:
        """Test systests/qtimer_test"""
        result = self.run_individual_test('qtimer_test')
        self.assertTrue(result, "Test qtimer_test failed")

    def test_reg_reads(self) -> None:
        """Test systests/reg-reads"""
        result = self.run_individual_test('reg-reads')
        self.assertTrue(result, "Test reg-reads failed")

    def test_rev(self) -> None:
        """Test systests/rev"""
        result = self.run_individual_test('rev')
        self.assertTrue(result, "Test rev failed")

    def test_single_step(self) -> None:
        """Test systests/single_step"""
        result = self.run_individual_test('single_step')
        self.assertTrue(result, "Test single_step failed")

    def test_standalone_vec(self) -> None:
        """Test systests/standalone_vec"""
        result = self.run_individual_test('standalone_vec')
        self.assertTrue(result, "Test standalone_vec failed")

    def test_start(self) -> None:
        """Test systests/start"""
        result = self.run_individual_test('start')
        self.assertTrue(result, "Test start failed")

    def test_swi2(self) -> None:
        """Test systests/swi2"""
        result = self.run_individual_test('swi2')
        self.assertTrue(result, "Test swi2 failed")

    def test_swi_fs(self) -> None:
        """Test systests/swi_fs"""
        result = self.run_individual_test('swi_fs')
        self.assertTrue(result, "Test swi_fs failed")

    def test_swi_wait(self) -> None:
        """Test systests/swi_wait"""
        result = self.run_individual_test('swi_wait')
        self.assertTrue(result, "Test swi_wait failed")

    def test_sys_atomics(self) -> None:
        """Test systests/sys_atomics"""
        result = self.run_individual_test('sys_atomics')
        self.assertTrue(result, "Test sys_atomics failed")

    def test_sys_reg_mut(self) -> None:
        """Test systests/sys_reg_mut"""
        result = self.run_individual_test('sys_reg_mut')
        self.assertTrue(result, "Test sys_reg_mut failed")

    def test_tlblock(self) -> None:
        """Test systests/tlblock"""
        result = self.run_individual_test('tlblock')
        self.assertTrue(result, "Test tlblock failed")

    def test_udma(self) -> None:
        """Test systests/udma"""
        result = self.run_individual_test('udma')
        self.assertTrue(result, "Test udma failed")

    def test_vid_group(self) -> None:
        """Test systests/vid-group"""
        result = self.run_individual_test('vid-group')
        self.assertTrue(result, "Test vid-group failed")

    def test_vid_reg(self) -> None:
        """Test systests/vid_reg"""
        result = self.run_individual_test('vid_reg')
        self.assertTrue(result, "Test vid_reg failed")


if __name__ == '__main__':
    QemuSystemTest.main()
