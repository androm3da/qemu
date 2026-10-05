.. SPDX-License-Identifier: GPL-2.0-or-later

SA8775P Compute DSP
===================

The ``sa8775p-cdsp`` machine models the Hexagon v73 compute DSP (CDSP0) of the Qualcomm SA8775P.

The machine contains:

* a v73 core with six hardware threads, 128-entry JTLB and four HVX contexts
* the config table at ``0x24180000``
* the L2VIC at ``0x26390000``
* the QTimer, whose access-control frame is at ``0x26300000`` and whose
  three frames follow at ``0x263a1000``
* 8 MiB of VTCM at ``0x25000000``
* DDR at ``0x80000000`` (1 GiB by default)
* the Linux kernel address, ``0xa0000000``, which is where the machine's
  H2 loadlinux expects it
* the console and virtio-mmio transports common to the
  :doc:`Qualcomm DSP machines <qcom-dsp>`
* stubs for the register windows that the SoC's device tree describes

Linux boots on it as described for the other
:doc:`Qualcomm DSP machines <qcom-dsp>`.
