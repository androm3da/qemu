.. SPDX-License-Identifier: GPL-2.0-or-later

Compute DSP
===========

A Hexagon CDSP is designed as a computation offload device for an SoC.  The
``V66G_1024`` machine contains:

* L2VIC interrupt controller
* QTimer timer device

This machine will support any Hexagon CPU, but will default to ``v66``.

H2 boot firmware
----------------

The ``qcs6490-cdsp`` (``v68``) and ``sm8975-nsp`` (``v85``) machines can be
started through the H2 hypervisor images shipped in ``pc-bios``, selected
with ``-bios``.  The images are named after the machine, with ``-``
replaced by ``_``; ``qcs6490-cdsp`` is used below:

``hexagon_booter_qcs6490_cdsp``
  H2 plus a loader that reads guest ELF files from the host through QEMU
  semihosting::

    qemu-system-hexagon -M qcs6490-cdsp -serial stdio -display none \
        -bios hexagon_booter_qcs6490_cdsp \
        -semihosting-config enable=on,target=native,arg=booter,arg=guest.elf

``hexagon_loadlinux_qcs6490_cdsp``
  H2 plus a loader that starts a kernel which QEMU has placed at physical
  address ``0xa1000000`` (``0xa0000000`` on ``sm8975-nsp``) with
  ``-kernel``.  It needs no semihosting.

The images use the DDR window starting at ``0x80000000`` and are built with
HVX, HMX and HLX support.  See ``pc-bios/README`` for the H2 revision and
how to rebuild them.

The unchanged H2 ``loadlinux`` passes a zero device tree address to the
kernel, so a Linux kernel with no built-in device tree stops early with
"No usable device tree from bootloader", even if one is given with
``-dtb``.  A kernel-side fallback is needed to boot Linux to userspace with
this firmware.
