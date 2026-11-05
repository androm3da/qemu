.. SPDX-License-Identifier: GPL-2.0-or-later

Hexagon machines and memory maps
================================

Choose a machine for the program's runtime and memory layout, as well as
its instruction set. The machine selects the peripheral map, configuration
table, and boot protocol; ``-cpu`` selects the CPU model.
Use ``qemu-system-hexagon -machine help`` and ``-cpu help`` to inspect
what a particular build provides.

Simulator-compatible machines
-----------------------------

These machines support SDK standalone OS and QuRT simulator programs.
They have L2VIC, QTimer, a configuration table, VTCM, and semihosting.

.. csv-table::
   :header: "Machine", "Default CPU", "Default threads", "Maximum threads"

   V66G_1024, v66, 4, 8
   V68N_1024, v68, 6, 8
   V73NC_1, v73, 6, 8
   V79NA_1, v79, 8, 8
   V81DGB_1, v81, 12, 12
   V81QA_1, v81, 12, 12
   V85NA_1, v85, 12, 12
   V85QA_1, v85, 12, 12

``V66G_1024`` is the default when ``-M`` is omitted. ``sim`` is an alias
for ``V85NA_1``; selecting ``-M sim`` is therefore different from omitting
``-M``. Specify the machine explicitly in reproducible examples.

RAM defaults to 4 GiB starting at physical address zero. Devices and ROM
regions overlay parts of that address space; an application must avoid
those regions. ``-m`` sets the RAM size at this base address. ``-smp N``
selects the number of hardware threads, subject to the machine limit.

The following peripheral addresses are shared by these models:

.. csv-table::
   :header: "Region", "Default physical base"

   QTimer control, 0xfc900000
   L2VIC, 0xfc910000
   QTimer frames, 0xfc921000

The ``subsystem-base`` machine property relocates these three regions
and updates the subsystem base advertised by the configuration table.
The offsets are respectively zero, ``0x10000``, and ``0x21000``. For
example:

.. code-block:: sh

    qemu-system-hexagon -M V68N_1024,subsystem-base=0x20000000 \
        -display none -kernel ./prog.elf

The base must be nonzero, aligned to 64 KiB, and leave room for the
regions below the 4 GiB boundary. The property is available on
``V66G_1024``, ``V68N_1024``, ``V79NA_1``, ``V81DGB_1``, and
``V81QA_1``.

.. csv-table:: Simulator memory differences
   :header: "Machine", "Configuration ROM", "Fast L2VIC", "VTCM base", "VTCM size"

   V66G_1024, 0xd8180000, 0xd81e0000, 0xd8200000, 256 KiB
   V68N_1024, 0xde000000, 0xd81e0000, 0xd8400000, 4 MiB
   V73NC_1, 0xd8180000, 0xd81e0000, 0xd9000000, 2 MiB
   V79NA_1, 0xd8180000, 0xd81e0000, 0xd9000000, 8 MiB
   V81DGB_1, 0xd8380000, 0xd83e0000, 0xd8400000, 2 MiB
   V81QA_1, 0xd8380000, 0xd83e0000, 0xd9000000, 16 MiB
   V85NA_1, 0xd8180000, 0xd81e0000, 0xd9000000, 12 MiB
   V85QA_1, 0xd8180000, 0xd81e0000, 0xd9000000, 16 MiB

The configuration table exposed through ``CFGBASE`` reports the model
capabilities.

The virtual machine (``virt``)
------------------------------

``virt`` is a synthetic board for operating systems and bare-metal
programs. It uses the v68 subsystem configuration, defaults to CPU
``v68``, eight threads (maximum eight), and 4 GiB of RAM at zero. It adds
an Arm PL011 UART and eight virtio-mmio transports. QTimer control is at
``0xfc900000``, L2VIC at ``0xfc910000``, and QTimer frames at ``0xfc921000``.

.. csv-table:: Additional virt regions
   :header: "Region", "Physical base", "Size / spacing", "L2VIC input"

   PL011 UART, 0x10000000, 0x1000 bytes, 15
   virtio-mmio slot 0, 0x11000000, 0x1000 bytes per slot, 16
   virtio-mmio slots 1--7, 0x11001000--0x11007000, 0x1000 bytes per slot, 17--23
   Generated DTB, 0x99800000, reserve 4 MiB,
   QEMU boot stub (firmware boot only), 0x99c00000, reserve 0x200 bytes,
   Hexagon VM guest timer description (firmware boot only), 0xab000000, 0x1000 bytes, 12
   Kernel window (firmware boot only), 0xa0000000, up to 896 MiB,

The UART clock is 24 MHz. ``-serial`` attaches its host backend.
Keep the DTB area free of program segments, heap, and stacks.
This board uses the simulator configuration ROM and VTCM locations for
``V68N_1024`` listed above. A smaller ``-m`` must still cover the program
and the DTB location if the guest needs the generated tree.

QEMU generates a DTB describing CPUs, L2VIC, UART, virtio, and accelerator
capabilities. The ``virt`` loader places it at ``0x99800000``, unless the
kernel boots through Hexagon VM host firmware, in which case it follows
the kernel (see :doc:`boot`). The guest uses the RAM base and size
described above.

Board-specific CDSP and NSP machines
------------------------------------

These models describe the DSP subsystem of a named SoC. They add a
PL011 console and virtio transports for use by QEMU guests.

.. csv-table::
   :header: "Machine", "Default CPU", "Default / maximum threads", "RAM default"

   qcs6490-cdsp, v68, 6, 1 GiB
   sa8775p-cdsp, v73, 6, 1 GiB
   sc8480xp-nsp0, v81, 12, 1 GiB
   sa8797p-nsp0, v81, 12, 1 GiB
   sm8975-nsp, v85, 12, 1 GiB

RAM starts at ``0x80000000``. ``-m`` changes its size; ``-smp`` selects
up to the thread count in the table.

.. csv-table:: Shared board regions
   :header: "Region", "Physical base", "Size / spacing", "L2VIC input"

   PL011 UART, 0x37000000, 0x1000 bytes, 15
   virtio-mmio slot 0, 0x37010000, 0x1000 bytes per slot, 96
   virtio-mmio slots 1--7, 0x37011000--0x37017000, 0x1000 bytes per slot, 97--103
   QEMU boot stub, 0x99c00000, reserve 0x200 bytes,
   Shipped H2 firmware load base and entry, 0x9b800000, reserve firmware and working memory,
   Hexagon VM guest timer description, 0xab000000, 0x1000 bytes, 12

The guest timer is accessed through Hexagon VM hypercalls. The PL011
clock is 24 MHz. The firmware ELF determines its exact loaded
segments; the Hexagon VM host allocates runtime memory. Keep guest-managed
RAM within the window assigned by its loader.

.. csv-table:: DSP device bases
   :header: "Machine", "Configuration ROM", "QTimer control", "L2VIC", "QTimer frames"

   qcs6490-cdsp, 0x09980000, 0x0a3a0000, 0x0a390000, 0x0a3a1000
   sa8775p-cdsp, 0x24180000, 0x26300000, 0x26390000, 0x263a1000
   sc8480xp-nsp0, 0x30180000, 0x323a0000, 0x32390000, 0x323a1000
   sa8797p-nsp0, 0x30180000, 0x323a0000, 0x32390000, 0x323a1000
   sm8975-nsp, 0x24180000, 0x263a0000, 0x26390000, 0x263a1000

.. csv-table:: Local memories
   :header: "Machine", "Fast L2VIC", "VTCM base", "VTCM size", "L2 TCM RAM"

   qcs6490-cdsp, 0x099e0000, 0x09c00000, 2 MiB, none
   sa8775p-cdsp, 0x241e0000, 0x25000000, 8 MiB, none
   sc8480xp-nsp0, 0x301e0000, 0x31000000, 8 MiB, none
   sa8797p-nsp0, 0x301e0000, 0x31000000, 16 MiB, 1 MiB at 0x30000000
   sm8975-nsp, 0x241e0000, 0x25000000, 12 MiB, 1 MiB at 0x24000000

QTimer has three frames, wired to L2VIC inputs 2, 3, and 4, on all these
machine families. DTB interrupt specifiers for wired L2VIC inputs include
an offset of 32 for the per-CPU interrupt namespace; for example, the
UART's input 15 is described as interrupt 47. Use the interrupt
controller binding appropriate to the guest to interpret DTB specifiers.

Firmware choices are listed in :doc:`cdsp`.
