#!/usr/bin/env python3
#
# Copyright(c) Qualcomm Innovation Center, Inc. All Rights Reserved.
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import re
import time
import unittest

from qemu_test import QemuSystemTest, Asset, wait_for_console_pattern


_TARBALL_BIN_PATH = (
    "systests_standalone_package",
    "StandaloneSysTests_6.4.0.2_v68",
    "bin",
)

class SysTestsStandaloneTests(QemuSystemTest):
    SYSTEST_TIMEOUT_SEC = 30

    ASSET_TARBALL = Asset(
        "https://github.com/qualcomm/qemu-hexagon-testing/releases/download/v0.2.14/systests_standalone.tar.gz",
        "f0c535d746384126954757b6ce54452c5fe82624618c79862495eec24718a6ff",
    )

    def setUp(self):
        super().setUp()
        self.archive_extract(self.ASSET_TARBALL)

    def binary(self, name):
        path = self.scratch_file(*_TARBALL_BIN_PATH, name)
        self.assertTrue(os.path.exists(path))
        return path

    def run_exit_zero(self, binary_name, *extra_args, machine="sim"):
        self.set_machine(machine)
        self.set_vm_arg("-display", "none")
        self.set_vm_arg("-kernel", self.binary(binary_name))
        for flag, value in zip(extra_args[::2], extra_args[1::2]):
            self.set_vm_arg(flag, value)
        self.vm.launch()
        self.vm.wait(timeout=60.0)
        self.assertEqual(self.vm.exitcode(), 0,
                         f"Test {binary_name} exited with "
                         f"code {self.vm.exitcode()}, expected 0")

    def run_console_pattern(self, binary_name, pattern, *extra_args,
                            machine="sim"):
        self.set_machine(machine)
        self.set_vm_arg("-display", "none")
        self.set_vm_arg("-kernel", self.binary(binary_name))
        for flag, value in zip(extra_args[::2], extra_args[1::2]):
            self.set_vm_arg(flag, value)
        self.vm.set_console(semihosting=True)
        self.vm.launch()
        try:
            wait_for_console_pattern(self, pattern)
        finally:
            self.vm.kill()

    def test_fopen(self):
        """fopen reads a file passed via --append and verifies its contents."""
        import tempfile
        # The fopen binary has a short cmdline buffer; use a short path.
        dummy = os.path.join(tempfile.gettempdir(), "qemu_fopen_test.so")
        with open(dummy, "w", encoding="utf-8") as f:
            f.write("valid\n")
        self.run_exit_zero("fopen", "-append", dummy)

    def test_ftrunc(self):
        """ftrunc truncates _testfile_ftrunc from 6 bytes to 1 byte."""
        ftrunc_path = self.scratch_file("_testfile_ftrunc")
        with open(ftrunc_path, "w", encoding="utf-8") as f:
            f.write("valid\n")
        # Sleep 1 s so mtime change is observable
        time.sleep(1)
        self.run_exit_zero("ftrunc", "-append", ftrunc_path)
        self.assertEqual(os.path.getsize(ftrunc_path), 1,
                         "_testfile_ftrunc should be 1 byte after ftrunc")

    def test_access(self):
        """access checks R_OK|W_OK on _testfile_access."""
        testfile = self.scratch_file("_testfile_access")
        with open(testfile, "w", encoding="utf-8") as f:
            f.write("valid\n")
        self.run_exit_zero("access", "-append", testfile)

    def test_semihost(self):
        self.run_console_pattern("semihost", "PASS", "-append", "arg1", "arg2")

    def test_dtg_interrupt(self):
        self.run_exit_zero("dtg_interrupt")

    def test_mmu_multi_tlb(self):
        self.run_exit_zero("mmu_multi_tlb")

    def test_timer_reg(self):
        self.run_exit_zero("timer_reg")

    def test_hvx_multi(self):
        self.run_exit_zero("hvx-multi")

    def test_pendalot(self):
        self.run_console_pattern("pendalot", "PASS", machine="V81QA_1")

    def test_pendalot_v66(self):
        self.run_console_pattern("pendalot", "PASS", machine="V66G_1024")

    def test_pendalot_v68(self):
        self.run_console_pattern("pendalot", "PASS", machine="V68N_1024")

    def test_swi_wait(self):
        """Interrupt-delivery test gated on pcycle_pause() busy-waits."""
        self.run_console_pattern("swi_wait", "PASS")

    @unittest.skip(
        "guest recursively takes K0 in its ISR and hangs on hexagon-sim"
    )
    def test_pend_wake_wait(self):
        self.run_console_pattern("pend_wake_wait", "PASS")

    def test_standalone_vec(self):
        self.run_exit_zero("standalone_vec")

    def test_badva(self):
        self.run_console_pattern("badva", "PASS")

    def test_bestwait(self):
        self.run_console_pattern("bestwait", "PASS")

    def test_checkforpriv(self):
        self.run_console_pattern("checkforpriv", "PASS")

    def test_ciad_siad(self):
        self.run_console_pattern("ciad-siad", "PASS")

    def test_getcwd(self):
        self.run_exit_zero("getcwd")

    def test_gregs(self):
        self.run_console_pattern("gregs", "PASS")

    def test_hvx_64b(self):
        self.run_console_pattern("hvx_64b", "PASS")

    def test_invalid_insn_for_rev(self):
        self.run_console_pattern("invalid_insn_for_rev", "PASS")

    def test_invalid_opcode(self):
        self.run_console_pattern("invalid_opcode", "PASS")

    def test_k0lock(self):
        self.run_console_pattern("k0lock", "PASS")

    def test_k0lock_syscfg(self):
        self.run_console_pattern("k0lock-syscfg", "PASS")

    def test_mmu_asids(self):
        self.run_console_pattern("mmu_asids", "PASS")

    def test_mmu_overlap(self):
        self.run_console_pattern("mmu_overlap", "PASS")

    def test_mmu_page_size(self):
        self.run_console_pattern("mmu_page_size", "PASS")

    def test_mmu_permissions(self):
        self.run_console_pattern("mmu_permissions", "PASS")

    def test_reg_reads(self):
        self.run_console_pattern("reg-reads", "PASS")

    def test_rev(self):
        self.run_exit_zero("rev")

    def test_standalone_hw(self):
        self.run_exit_zero("standalone_hw")

    def test_start(self):
        self.run_console_pattern("start", "PASS")

    def test_swi(self):
        self.run_console_pattern("swi", "PASS")

    def test_sys_atomics(self):
        self.run_console_pattern("sys_atomics", "PASS")

    def test_thread_scheduling(self):
        self.run_console_pattern("thread_scheduling", "PASS")

    def test_tlblock(self):
        self.run_console_pattern("tlblock", "PASS")

    def test_vid_reg(self):
        self.run_exit_zero("vid_reg")

    def test_vtcm_error(self):
        self.run_exit_zero("vtcm_error")

if __name__ == "__main__":
    QemuSystemTest.main()
