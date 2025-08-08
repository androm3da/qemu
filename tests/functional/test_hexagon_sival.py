#!/usr/bin/env python3
#
# Copyright(c) 2024-2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import re
import tempfile
from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern
from unittest import skipUnless
from hexagon.utils import HexagonCheckError, scale_timeout, read_skip_file, list_test_cases

class SivalTests(QemuSystemTest):

    SIVAL_TIMEOUT_SEC = 100

    REPO = 'https://gitlab.qualcomm.com/qqvp/testing/qemu-sival-tests'
    GIT_REF = '4bac1e4a88b0592df3dbe48d0b7810a8abeff7df'
    ASSET_TARBALL = \
        Asset(f'{REPO}/-/archive/{GIT_REF}/qemu-sival-tests-{GIT_REF}.tar.gz',
              '9a8e9f093104b24ea64ab862b6ad597ec98efe65646c450902e1dcc04d47c5c8')

    SIVAL_MACHINES = {
        "V73": "V73NA_1024",
        "nordau_v1_diag_suite": "V81QA_1",
    }

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        # Extract archive once for all test methods
        if hasattr(cls, '_archive_extracted'):
            return
        cls.archive_extract(cls.ASSET_TARBALL)
        cls._archive_extracted = True

    def check_sival_result(self, test_name, tempfile_handle):
        """Check the results of a sival test by examining the register dump"""
        buf = tempfile_handle.read().decode('utf-8')
        # Reset the tempfile for the next test
        tempfile_handle.truncate(0)
        tempfile_handle.flush()
        tempfile_handle.seek(0)

        reg_regex = re.compile(".*r01.: ([^,]*),")
        matches = [reg_regex.match(line) for line in buf.split("\n")]
        # Remove non-matching lines
        regvals = [match.group(1) for match in filter(None, matches)]
        err = None

        # Must have at least one thread with r01 == 0xe0f0beef, and all
        # others be 0xe0f0beef or 0x00000000 (unused thread).
        if not any([v for v in regvals if v == "0xe0f0beef"]):
            err = HexagonCheckError(f'missing 0xe0f0beef, regs: {regvals}')
        else:
            unknown_regs = [v for v in regvals if v not in ("0xe0f0beef", "0x00000000")]
            if any(unknown_regs):
                err = HexagonCheckError(f'invalid regs: {unknown_regs}')

        if "_fail_" in test_name:
            if err is None:
                raise HexagonCheckError(f'Expected failure, but test succeeded.')
        elif err is not None:
            raise err

    def run_single_sival_test(self, arch_name, test_bin, timeout, tempfile_handle):
        """Run a single Sival test case"""
        test_name = os.path.basename(test_bin)

        # Configure VM for this architecture
        self.set_vm_arg('-M', self.SIVAL_MACHINES[arch_name])

        # Set up VM args
        self.vm.add_args('-cpu', f'any,dump-json-reg-file={tempfile_handle.name}')
        self.set_vm_arg('-kernel', test_bin)

        # Run the test
        self.vm.launch()
        self.vm.wait(timeout=timeout)

        # Check result
        try:
            self.check_sival_result(test_name, tempfile_handle)
        except HexagonCheckError as e:
            err_msg = f'FAILED (exit code {self.vm.exitcode()})\n' + \
                      '----------\n' + str(self.vm) + '\n----------\n' + \
                      str(e) + '\n----------'
            self.fail(f'Test {test_name} failed:\n{err_msg}')

    def _run_generic_test(self, arch_name, test_name):
        """Generic test runner that can be called by dynamically injected test methods"""
        self.setUpClass()

        # Find the test binary path
        test_dir = f'{self.workdir}/qemu-sival-tests-{self.GIT_REF}/{arch_name}/'
        test_bins = list_test_cases(test_dir)
        test_bin = None
        for candidate in test_bins:
            if os.path.basename(candidate) == test_name:
                test_bin = candidate
                break

        # Check if test should be skipped
        skip = read_skip_file(test_dir)
        if test_name in skip:
            self.skipTest(f'Test {test_name} is in SKIP file')

        # Run the specific test
        timeout_scale, timeout = scale_timeout(self.SIVAL_TIMEOUT_SEC)

        with tempfile.NamedTemporaryFile() as fp:
            self.run_single_sival_test(arch_name, test_bin, timeout, fp)


# Generate individual test methods for QEMU test discovery
def _inject_individual_tests():
    """Dynamically inject individual test methods into SivalTests class"""

    def create_test_method(arch_name, test_name):
        @skipUnless(os.getenv('QEMU_TEST_ALLOW_UNTRUSTED_CODE'), 'untrusted code')
        def test_method(self):
            self._run_generic_test(arch_name, test_name)
        return test_method

    # Hook into setUpClass to inject individual tests after archive extraction
    original_setup = SivalTests.setUpClass

    @classmethod
    def enhanced_setup(cls):
        # Call original setup to extract archive
        result = original_setup()

        # Inject individual test methods on first setup
        if not hasattr(cls, '_individual_methods_injected'):
            cls._individual_methods_injected = True

            # Create a temporary instance to access workdir after extraction
            temp_instance = cls.__new__(cls)
            temp_instance.setUpClass()

            # Inject individual test methods
            for arch_name in cls.SIVAL_MACHINES.keys():
                test_dir = f'{temp_instance.workdir}/qemu-sival-tests-{cls.GIT_REF}/{arch_name}/'
                if not os.path.exists(test_dir):
                    continue

                try:
                    skip = read_skip_file(test_dir)
                except FileNotFoundError:
                    skip = set()
                test_bins = list_test_cases(test_dir)

                for test_bin in test_bins:
                    test_name = os.path.basename(test_bin)
                    if test_name in skip:
                        continue

                    # Create safe method name
                    safe_arch = re.sub(r'[^a-zA-Z0-9_]', '_', arch_name)
                    safe_test = re.sub(r'[^a-zA-Z0-9_]', '_', os.path.splitext(test_name)[0])
                    method_name = f'test_{safe_arch}_{safe_test}'

                    # Only inject if method doesn't already exist
                    if not hasattr(cls, method_name):
                        test_method = create_test_method(arch_name, test_name)
                        test_method.__name__ = method_name
                        test_method.__doc__ = f'Sival test: {arch_name}/{test_name}'
                        setattr(cls, method_name, test_method)

        return result

    SivalTests.setUpClass = enhanced_setup

# Inject individual test methods for QEMU test discovery
_inject_individual_tests()

if __name__ == '__main__':
    QemuSystemTest.main()
