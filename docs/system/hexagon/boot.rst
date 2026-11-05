.. SPDX-License-Identifier: GPL-2.0-or-later

Hexagon boot interface
======================

``qemu-system-hexagon`` loads the initial image. Guest startup code
initializes the stack, exception handling, and runtime state. When a
Hexagon VM host is loaded, it establishes the guest OS's boot environment.

Initial hardware state
----------------------

Only hardware thread zero starts executing. Other threads start powered
off; guest software must start them with the architecture's thread
control instructions. ``HTID`` identifies the hardware thread.

On initial CPU reset, the thread system registers are cleared; ``SSR``
is cleared except for its reset cause field. The shared ``SYSCFG`` is
zero, so the MMU and global interrupt enable are off. Execution uses
untranslated physical addresses in monitor mode. Guest software must
populate TLB entries before enabling the MMU, and set up exception vectors
and interrupt masks before enabling interrupts. See
:doc:`../../devel/hexagon-sys` for the system architecture.

QEMU sets thread zero's PC to the selected image entry address. ``EVB``
initially contains zero; ELF startup code installs its own aligned event
vector base.

Simulator-compatible machines
-----------------------------

``-kernel file`` loads a little-endian Hexagon ELF, places its loadable
segments according to their physical addresses, zero-fills the segments'
remaining memory, and starts thread zero at the ELF entry point. Link
startup code for untranslated execution, with the entry point accessible
at its linked address.

The loader also sets the ELF symbols ``isdb_secure_flag`` and
``isdb_trusted_flag`` to one when present, for SDK startup compatibility.
``-append`` supplies the semihosting command line.

The virt machine
----------------

``-kernel file`` alone loads a Hexagon ELF without address translation and
starts at its ELF entry point.

``-bios file`` alone loads the firmware named by the file, using QEMU's
firmware search path (including ``-L``). An ELF is loaded as linked and
started at its entry point. Any other file is a raw image, which may be as
large as RAM: it is loaded at physical address zero and started there, so
its first executable instruction must be at zero.

``-bios`` with ``-kernel`` boots the kernel through Hexagon VM host
firmware, as described in :ref:`hexagon-vm-boot`. The kernel window starts
at ``0xa0000000``, so use a firmware image whose guest address in
:doc:`cdsp` is ``0xa0000000``; the default 4 GiB of RAM covers it. With
this boot, the DTB also describes the RAM window and the guest timer.

Otherwise, guest software reads the generated DTB at ``0x99800000``.
``-append`` sets its ``/chosen/bootargs`` property.

Board-specific CDSP and NSP machines
------------------------------------

Both ``-kernel`` and ``-bios`` accept Hexagon ELF images. ``-bios`` uses
QEMU's firmware search path (including ``-L``). The combinations have
different meanings:

.. list-table::
   :header-rows: 1

   * - Options
     - Loading and entry
   * - ``-kernel`` alone
     - Load segments at ELF physical addresses and start at the ELF entry.
   * - ``-bios`` alone
     - Load firmware segments at ELF physical addresses and start at its entry.
   * - ``-bios`` and ``-kernel``
     - Load firmware normally, relocate the kernel segments into the board's
       kernel window, and enter firmware through a QEMU boot stub.

The combined case is described in :ref:`hexagon-vm-boot`, using the kernel
base in :doc:`cdsp`.

``-dtb file`` replaces the generated tree. A nonempty ``-append`` value
sets its ``/chosen/bootargs``. A replacement tree must describe the
selected hardware-thread count, actual RAM, and matching device drivers.

.. _hexagon-vm-boot:

Booting a kernel through Hexagon VM host firmware
-------------------------------------------------

On the ``virt`` and board-specific machines, ``-bios`` with ``-kernel``
starts the kernel through Hexagon VM host firmware. QEMU loads the firmware
ELF as linked and adds the machine's kernel base to the kernel ELF's
physical load addresses, so the kernel's addresses are relative to the
start of its window. The kernel must use the corresponding Hexagon VM
guest layout. QEMU starts thread zero at a boot stub at ``0x99c00000``.
The stub passes the DTB address with the low word in ``r0`` and the high
word in ``r1``, then jumps to the firmware's ELF entry point. The Hexagon
VM host defines the child VM's register and execution state.

QEMU places the DTB after the loaded kernel: the highest loaded address
plus 16 MiB, rounded up to a 4 MiB boundary. QEMU copies it to guest RAM
before entering the firmware. An optional ``-initrd`` is placed 4 MiB
after the DTB base, and its bounds are written to
``/chosen/linux,initrd-start`` and ``linux,initrd-end``. ``-initrd``
requires both ``-bios`` and ``-kernel``. Reserve these areas in guest
memory. The DTB RAM node describes the kernel window, capped at 896 MiB
and bounded by the selected RAM size.

Console and semihosting
-----------------------

SDK simulator programs use semihosting for console I/O. The ``virt`` and
board-specific machines also provide a PL011 UART, whose host backend is
selected with ``-serial``.

Debugging with LLDB
-------------------

Use ``lldb`` (tested with LLDB 21) to debug Hexagon guests through QEMU's
remote stub. Start the bare-metal guest from :doc:`baremetal` stopped:

.. code-block:: sh

    qemu-system-hexagon -M virt -smp 1 -display none -monitor none \
        -serial stdio -kernel ./hello.elf -s -S

Then, in another terminal:

.. code-block:: text

    $ lldb ./hello.elf
    (lldb) gdb-remote localhost:1234
    (lldb) breakpoint set --name main
    (lldb) continue
    (lldb) register read pc r29
    (lldb) thread step-inst

Instruction stepping advances one Hexagon instruction packet.

For remote-stub configuration, see :doc:`../gdb`. When debugging an OS,
load its firmware or kernel symbols in LLDB. A shared object loaded by
QuRT may need its runtime load address specified for symbol resolution.
