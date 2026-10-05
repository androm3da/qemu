.. SPDX-License-Identifier: GPL-2.0-or-later

Qualcomm DSP subsystems
=======================

The Qualcomm DSP machines model the Hexagon subsystems of particular SoCs:
the compute DSP (CDSP) and neural signal processors (NSP).  Their layout
comes from the SoC, but they share a board layer, so they are all used the
same way.

Every machine contains:

* the Hexagon core, L2VIC, QTimer and config table of its SoC
* DDR at ``0x80000000`` (1 GiB by default)
* a PL011 UART at ``0x10000000``, interrupt 15 of the L2VIC.  The subsystems
  have no UART; this one exists only so that the guest has a console.
* eight virtio-mmio transports from ``0x11000000``, ``0x1000`` bytes apart,
  on L2VIC interrupts 768 to 775, for ``-drive`` and ``-device``.  A drive
  given without a ``-device`` is attached to the first free transport.
* stubs that read as zero for the SoC register windows that guest software
  is known to probe

Booting Linux
-------------

These subsystems run the H2 hypervisor with Linux as a guest.  ``-bios`` and
``-kernel`` take ELF files, and behave the same on every machine:

``-bios F -kernel K``
  ``F`` is an H2 ``hexagon_loadlinux`` built for the machine.  The kernel ``K`` is
  loaded at the kernel address that ``F`` expects (given for each machine), a generated device tree describing the machine to Linux is
  placed after it, and ``F`` is started with the device tree address in
  ``r1:r0``.  An initramfs is passed with ``-initrd``.

``-bios F``
  ``F`` is loaded and started at its entry point.

``-kernel K``
  ``K`` is loaded where it was linked and started at its entry point, with
  no device tree.

Neither
  Nothing is loaded and the first thread runs from the reset vector.

``-initrd`` needs both ``-bios`` and ``-kernel``.

The kernel sizes its memory from ``mem=``, and ``lpj=`` avoids calibrating
its delay loop against H2's virtual timer::

    $ qemu-system-hexagon -M qcs6490-cdsp \
        -bios hexagon_loadlinux_qcs6490_cdsp \
        -kernel vmlinux -initrd rootfs.cpio \
        -append 'console=ttyAMA0 mem=240M lpj=89124080' \
        -nographic -serial mon:stdio

The standalone ``hexagon_loadlinux_v*`` images link H2 at physical address 0,
where these machines have no memory; use an image built for the machine.

Machines
--------

.. toctree::
   :maxdepth: 1

   qcs6490-cdsp
   sa8775p-cdsp
   sc8480xp-nsp0
   sa8797p-nsp0
   sm8975-nsp
