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

class QURTTests(QemuSystemTest):

    QURT_TIMEOUT_SEC = 300

    REPO = 'https://gitlab.qualcomm.com/qqvp/testing/qemu-qurt-tests'
    GIT_REF = '79ab6880bd4eb893439af73331355c3dc2a241b4'
    ASSET_TARBALL = \
        Asset(f'{REPO}/-/archive/{GIT_REF}/qemu-qurt-tests-{GIT_REF}.tar.gz',
              '96672ff657464afd7cd3b7755c832f2332b642d9aae338d6edda044d0bae602a')

    QURT_MACHINES = {
        "nspv79NA_1": "V79NA_1",
        "nspv81QA_1": "V81QA_1",
    }

    QURT_CPUS = {
        "nspv81QA_1": "v81",
    }

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        # Extract archive once for all test methods
        if hasattr(cls, '_archive_extracted'):
            return
        cls.archive_extract(cls.ASSET_TARBALL)
        cls._archive_extracted = True

    def check_test_result(self, test_name):
        if self.vm.exitcode() != 0:
            raise HexagonCheckError(self.vm.get_log() or '')

    def run_single_qurt_test(self, arch_name, test_bin, timeout):
        """Run a single QURT test case"""
        test_name = os.path.basename(test_bin)

        # Configure VM for this architecture
        self.set_vm_arg('-M', self.QURT_MACHINES[arch_name])
        if arch_name in self.QURT_CPUS:
            self.set_vm_arg('-cpu', self.QURT_CPUS[arch_name])

        # Set up VM args
        self.vm.add_args('-m', '4G', '-no-reboot')
        self.vm.set_encoding("ISO-8859-1")  # Some qurt tests output unicode
        self.set_vm_arg('-kernel', test_bin)

        # Run the test
        self.vm.launch()
        self.vm.wait(timeout=timeout)

        # Check result
        try:
            self.check_test_result(test_name)
        except HexagonCheckError as e:
            err_msg = f'FAILED (exit code {self.vm.exitcode()})\n' + \
                      '----------\n' + str(self.vm) + '\n----------\n' + \
                      str(e) + '\n----------'
            self.fail(f'Test {test_name} failed:\n{err_msg}')

    def _run_generic_test(self, arch_name, test_name):
        """Generic test runner that can be called by dynamically injected test methods"""
        self.setUpClass()

        # Find the test binary path
        test_dir = f'{self.workdir}/qemu-qurt-tests-{self.GIT_REF}/{arch_name}/'
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
        timeout_scale, timeout = scale_timeout(self.QURT_TIMEOUT_SEC)
        self.run_single_qurt_test(arch_name, test_bin, timeout)


# Generate individual test methods for QEMU test discovery
def _inject_individual_tests():
    """Dynamically inject individual test methods into QURTTests class"""

    def create_test_method(arch_name, test_name):
        @skipUnless(os.getenv('QEMU_TEST_ALLOW_UNTRUSTED_CODE'), 'untrusted code')
        def test_method(self):
            self._run_generic_test(arch_name, test_name)
        return test_method

    # Hook into setUpClass to inject individual tests after archive extraction
    original_setup = QURTTests.setUpClass

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
            for arch_name in cls.QURT_MACHINES.keys():
                test_dir = f'{temp_instance.workdir}/qemu-qurt-tests-{cls.GIT_REF}/{arch_name}/'
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
                        test_method.__doc__ = f'QURT test: {arch_name}/{test_name}'
                        setattr(cls, method_name, test_method)

        return result

    QURTTests.setUpClass = enhanced_setup

# Inject individual test methods for QEMU test discovery
_inject_individual_tests()

if __name__ == '__main__':
    QemuSystemTest.main()
