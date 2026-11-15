.. SPDX-License-Identifier: GPL-2.0-or-later

Hexagon ELF core dumps
======================

System emulation
----------------

``qemu-system-hexagon`` supports ELF32 little-endian dumps through the
``dump-guest-memory`` QMP command. For example, after negotiating QMP
capabilities, stop the machine and request a physical-memory dump::

    {"execute": "stop"}
    {"execute": "dump-guest-memory",
     "arguments": {"paging": false, "protocol": "file:/tmp/hexagon.core"}}

The dump contains a Linux-compatible ``CORE/NT_PRSTATUS`` note for each
CPU. Its synthetic thread ID is the QEMU CPU index plus one. Registers
describe the current CPU state; QEMU does not reconstruct a saved guest
exception frame. Physical memory is written as ELF load segments. Use
the optional ``begin`` and ``length`` arguments to restrict the memory
range, particularly on machines with large RAM configurations.

The dump does not supply guest virtual-to-physical page-table mappings.
When inspecting a kernel that runs at virtual addresses different from
physical memory, account for that translation in the analysis tool.

Linux-user emulation
--------------------

``qemu-hexagon`` writes a guest ELF core when the application terminates
with a core-generating signal and its core size limit permits the dump.
For example::

    ulimit -c unlimited
    qemu-hexagon ./application

The file is created in the current directory with a name of the form
``qemu_application_YYYYMMDD-HHMMSS_PID.core``. It contains the process
memory, auxiliary vector, process information, and a register note for
each guest thread. The ELF flags are preserved from the loaded executable.

Register notes
--------------

Both modes use the 48-word Hexagon Linux ``user_regs_struct`` layout:
R0 through R31, SA0, LC0, SA1, LC1, M0, M1, USR, packed P3:0, GP, UGP,
PC, CAUSE, BADVA, CS0, CS1, and a zero padding word. This layout differs
from QEMU's internal control-register numbering.

System dumps obtain CAUSE from SSR and BADVA from the thread's system
registers. Linux-user records QEMU's exception cause and writes zero for
BADVA, which is not retained as an architectural register in that mode.
HVX, HMX, HLX, and additional system-register notes are not included.

Use an LLDB build with Hexagon Linux core support and supply the matching
executable explicitly, for example::

    hexagon-lldb ./application -c ./qemu_application_YYYYMMDD-HHMMSS_PID.core

Some LLDB versions fail when opening a Hexagon core without its executable.
