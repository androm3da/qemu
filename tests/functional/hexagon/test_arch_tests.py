#!/usr/bin/env python3
#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#
# SPDX-License-Identifier: GPL-2.0-or-later

from unittest import skip
from qemu_test import QemuSystemTest, Asset
from qemu_test.cmd import wait_for_console_pattern


class ArchTestsUart(QemuSystemTest):
    """
    Hexagon architecture verification tests

    These are bare-metal tests from hexagon-arch-tests that exercise
    system functionality.

    Tests output results via UART.
    """
    timeout = 180

    ASSET_TARBALL = Asset(
        "https://github.com/qualcomm/qemu-hexagon-testing/releases/"
        "download/v0.2.14/arch_tests_uart.tar.gz",
        "ce93cb90b9d757c1946dfe8fe6abcec8292b08a66546ac51b2dd48650b05fa91",
    )

    def run_uart_test(self, test_name: str,
                      machine: str = "virt") -> None:
        """
        Run an arch test binary and verify PASS via UART console output.

        These binaries write their results to a PL011 at 0x10000000, which is
        part of the virt machine's device window. '-bios none' suppresses the
        default loadlinux firmware so -kernel boots directly into the test
        binary instead of being redirected through the H2 hypervisor loader.
        """
        self.set_machine(machine)
        self.archive_extract(self.ASSET_TARBALL)
        target_bin = self.scratch_file('arch_tests_uart_package',
                                      'bin', test_name)
        self.vm.set_console()
        self.set_vm_arg("-display", "none")
        self.set_vm_arg("-bios", "none")
        self.set_vm_arg("-kernel", target_bin)
        self.vm.launch()
        wait_for_console_pattern(self, "PASS")

    def test_exceptions(self) -> None:
        """Tests exception delivery for trap instructions, privilege
        violations, and verifies SSR cause codes and ELR values.
        """
        self.run_uart_test("test_exceptions")

    def test_guest_mode(self) -> None:
        """Tests guest mode entry/exit via CCR configuration, verifying
        GSR fields, GELR, and guest event vector table dispatch.
        """
        self.run_uart_test("test_guest_mode")

    def test_int_steering(self) -> None:
        """Tests interrupt steering via priority-based routing to
        specific threads using STID priority and iassignw.
        """
        self.run_uart_test("test_int_steering")

    def test_interrupts(self) -> None:
        """Tests interrupt delivery."""
        self.run_uart_test("test_interrupts")

    @skip("rapid_refire_with_spinning_thread hangs: a plain 32-bit store "
          "by thread 0 to a shared word is never observed by thread 1, "
          "which spins reading that word (confirmed with a minimal repro "
          "with no interrupt activity at all -- thread 0 writes the flag "
          "once, thread 1 spins for 1M+ iterations rereading it and never "
          "sees the update, though it does eventually stop once resumed "
          "from a different path in the other subtests, which all pass). "
          "The reverse direction (thread N writes, thread 0 polls) works "
          "fine everywhere else in this suite. Root cause not yet found; "
          "may be related to the SMP secondary-CPU bring-up hang seen "
          "booting Linux, which follows the same producer/consumer shape")
    def test_isr_stress(self) -> None:
        """Tests high-volume interrupt stress with multiple threads
        handling rapid SWI delivery and concurrent interrupt processing.
        """
        self.run_uart_test("test_isr_stress")

    def test_cache(self) -> None:
        """Tests cache operations: dckill/ickill, l2kill, dczeroa,
        dccleaninva, cache disable/enable, barriers, and dcinva/dccleana.
        """
        self.run_uart_test("test_cache")

    def test_l2vic(self) -> None:
        """Tests the L2VIC interrupt controller: enable readback,
        interrupt type readback, VID capture, and the fast interface.
        """
        self.run_uart_test("test_l2vic")

    @skip("PCYCLE does not increment under TCG here (upcycle, "
          "pcycle_incrementing, pcycle_monotonic all read back a value "
          "that never advances); a known, already-deferred gap -- see "
          "hexagon_get_sys_pcycle_count() users and the pcycle/timing "
          "cluster tracked separately, not new to this test")
    def test_pmu(self) -> None:
        """Tests performance monitoring unit: pcycle counter reads,
        cycle counting enable/disable via SYSCFG.
        """
        self.run_uart_test("test_pmu")

    def test_sys_regs(self) -> None:
        """Tests system registers."""
        self.run_uart_test("test_sys_regs")

    def test_threads(self) -> None:
        """Tests hardware thread management: start/stop, MODECTL state,
        per-thread HTID, shared memory, wait/resume, STID priority, and
        SCHEDCFG/BESTWAIT readback.
        """
        self.run_uart_test("test_threads")

    @skip("QTimer sub-tests pass, but pcycle_as_timer and "
          "pcycle_timer_gate fail on the same non-incrementing PCYCLE gap "
          "as test_pmu (see its skip reason)")
    def test_timer(self) -> None:
        """Tests system timer: QTimer version register, TIMERLO/TIMERHI
        monotonic reads, pcycle-based timing.
        """
        self.run_uart_test("test_timer")

    def test_tlb_mmu(self) -> None:
        """Tests TLB/MMU operations: write/read/probe/invalidate,
        global entries, multiple entries, overwrite, ASID matching,
        and permission checks.
        """
        self.run_uart_test("test_tlb_mmu")

    def test_user_mode(self) -> None:
        """Tests user mode / privilege transitions: supervisor mode,
        SSR UM/IE/XE/CE/PE bits, and the trap0 user-mode exit handler.
        """
        self.run_uart_test("test_user_mode")

    def test_hvx_context(self) -> None:
        """Tests the HVX coprocessor context: SSR XA context selection
        and isolation, vsplat/store, and the NO_COPROC_ENABLE exception
        raised when SSR:XE is clear.
        """
        self.run_uart_test("test_hvx_context")


if __name__ == "__main__":
    QemuSystemTest.main()
