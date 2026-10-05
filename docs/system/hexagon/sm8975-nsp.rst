.. SPDX-License-Identifier: GPL-2.0-or-later

SM8975 Neural Signal Processor
==============================

The ``sm8975-nsp`` machine models the Hexagon v81 neural signal processor (NSP) of the Qualcomm SM8975.

The machine contains:

* a v81 core with twelve hardware threads, 128-entry JTLB and eight HVX
  contexts
* the config table at ``0x24180000``
* the L2VIC at ``0x26390000``
* the QTimer, whose access-control frame is at ``0x263a0000`` and whose
  three frames follow at ``0x263a1000``
* 1 MiB of L2 TCM at ``0x24000000``
* 12 MiB of VTCM at ``0x25000000``
* DDR at ``0x80000000`` (1 GiB by default)
* the Linux kernel address, ``0xa0000000``, which is where the machine's
  H2 loadlinux expects it
* the console and virtio-mmio transports common to the
  :doc:`Qualcomm DSP machines <qcom-dsp>`
* stubs for the register windows that the SoC's device tree describes

Linux boots on it as described for the other
:doc:`Qualcomm DSP machines <qcom-dsp>`.
