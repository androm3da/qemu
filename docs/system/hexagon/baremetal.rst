.. SPDX-License-Identifier: GPL-2.0-or-later

A bare-metal UART program with LLVM
===================================

This example runs on ``qemu-system-hexagon -M virt`` using Clang's Hexagon
backend and LLD. It prints through the PL011 UART with semihosting
disabled, using its own startup code.

Source files and layout
-----------------------

Copy these files into a working directory:

* :download:`crt0.S <examples/crt0.S>`: startup code and event vectors.
* :download:`hello.c <examples/hello.c>`: polling UART driver and main().
* :download:`link.ld <examples/link.ld>`: ELF entry, sections, and stack.

The linker script reserves 1 MiB at ``0x80000000`` within ``virt`` RAM.
Code and initialized data have identical load and execution addresses;
QEMU's ELF loader places both directly in RAM. The stack grows down from
the end of this area, with at least 16 KiB reserved above static storage.

EVB's low eight bits are reserved, requiring 256-byte alignment for the
event vector base (see the `Hexagon Programmer's Reference Manual
<https://docs.qualcomm.com/doc/80-N2040-61/>`_). Each vector slot is one
four-byte jump packet. Slot zero enters startup; the remaining 47 slots
stop at ``unexpected_event``.

Startup installs ``EVB``, sets stack pointer ``r29``, clears frame pointer
``r30`` and ``.bss``, and calls C. The linker aligns both ends of ``.bss``
for the word-at-a-time clear. The MMU and interrupts remain disabled, and
only thread zero runs. If C returns, startup spins at ``stopped``.

The PL011 driver uses ``virt``'s UART at ``0x10000000`` with a 24 MHz
clock. It selects eight-bit data at 115200 baud, disables UART interrupts,
and polls the transmit FIFO. The program checks initialized data and
zero-initialized storage before printing.

Build and run
-------------

Use a Clang installation with ``hexagon`` in ``clang --print-targets``
and LLD with Hexagon ELF support (tested with Clang and LLD 21.1.0).
From the source-file directory:

.. code-block:: sh

    clang --target=hexagon -mcpu=hexagonv68 -G0 \
        -ffreestanding -fno-builtin -fno-stack-protector -O2 -g \
        -c crt0.S -o crt0.o
    clang --target=hexagon -mcpu=hexagonv68 -G0 \
        -ffreestanding -fno-builtin -fno-stack-protector -O2 -g \
        -c hello.c -o hello.o
    ld.lld -T link.ld crt0.o hello.o -o hello.elf
    qemu-system-hexagon -M virt -smp 1 -display none -monitor none \
        -serial stdio -semihosting-config enable=off -kernel ./hello.elf

``-G0`` disables small-data addressing. The freestanding build links only
the two objects above; supply any runtime helpers needed when extending
this program.

Expected output:

.. code-block:: text

    Hello from Hexagon!

The program then spins. Stop QEMU with ``Ctrl-c``; a ``timeout`` harness
exits with status 124 after printing the message.

To debug the program, add ``-s -S`` and follow :doc:`boot`. Break at
``main``, ``stopped``, or ``unexpected_event``.
