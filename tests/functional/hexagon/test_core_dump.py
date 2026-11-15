#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

"""Check Hexagon system ELF notes and physical memory without a guest OS."""

import struct
from pathlib import Path

from qemu_test import QemuSystemTest


class HexagonCoreDump(QemuSystemTest):
    def test_elf_dump(self):
        self.set_machine('virt')
        self.require_accelerator('tcg')
        base = 0x00100000
        self.vm.add_args(
            '-accel', 'tcg', '-m', '32M', '-smp', '2', '-S',
            '-device', f'loader,addr={base},data=0x12345678,data-len=4',
            '-device', f'loader,addr={base},cpu-num=0',
            '-device', f'loader,addr={base + 4},cpu-num=1')
        self.vm.launch()

        core = self.scratch_file('system.core')
        self.vm.cmd('dump-guest-memory', paging=False,
                    protocol=f'file:{core}', begin=base, length=4096)
        dump = Path(core).read_bytes()
        self.assertEqual(dump[:7], b'\x7fELF\x01\x01\x01')
        self.assertEqual(struct.unpack_from('<HH', dump, 16), (4, 164))
        phoff = struct.unpack_from('<I', dump, 28)[0]
        phentsize, phnum = struct.unpack_from('<HH', dump, 42)
        self.assertEqual(phentsize, 32)

        threads = {}
        found_memory = False
        for i in range(phnum):
            phdr = struct.unpack_from('<8I', dump, phoff + i * phentsize)
            ptype, offset, _, paddr, filesz, _, _, _ = phdr
            self.assertLessEqual(offset + filesz, len(dump))
            if ptype == 1 and paddr == base:  # PT_LOAD
                self.assertEqual(dump[offset:offset + 4], b'\x78\x56\x34\x12')
                found_memory = True
            if ptype != 4:  # PT_NOTE
                continue
            end = offset + filesz
            while offset < end:
                namesz, descsz, ntype = struct.unpack_from('<III', dump, offset)
                offset += 12
                name = dump[offset:offset + namesz]
                offset += (namesz + 3) & ~3
                desc = dump[offset:offset + descsz]
                offset += (descsz + 3) & ~3
                self.assertLessEqual(offset, end)
                if name != b'CORE\0' or ntype != 1:  # NT_PRSTATUS
                    continue
                self.assertEqual(descsz, 268)
                tid = struct.unpack_from('<I', desc, 24)[0]
                self.assertNotIn(tid, threads)
                threads[tid] = struct.unpack_from('<48I', desc, 72)
                self.assertEqual(struct.unpack_from('<I', desc, 264)[0], 0)

        self.assertTrue(found_memory)
        # QEMU uses CPU index + 1 as the synthetic PID in system dumps.
        self.assertEqual(set(threads), {1, 2})
        for tid, regs in threads.items():
            self.assertEqual(regs[42], base + (tid - 1) * 4)  # PC
            self.assertEqual(regs[39], 0)  # packed P3:0
            self.assertEqual(regs[47], 0)  # ABI padding


if __name__ == '__main__':
    QemuSystemTest.main()
