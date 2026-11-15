.. SPDX-License-Identifier: GPL-2.0-or-later

Querying Hexagon CPU models
==========================

``query-cpu-definitions`` lists the Hexagon CPU models available in the
binary. Each entry includes a model name, such as ``v68``, and its QOM
type, such as ``v68-hexagon-cpu``. The QOM type can be passed to
``device-list-properties`` to inspect CPU properties. The entries do not
promise cross-version migration compatibility or that a particular board
can run every listed CPU model.

``query-cpu-model-expansion`` supports the ``full`` expansion type. The
model name selects the ISA revision and the ``props`` object contains
configurable architectural features. For example::

    {"execute": "query-cpu-model-expansion",
     "arguments": {"type": "full",
                   "model": {"name": "v68", "props": {"ieee-fp": false}}}}

    {"return": {"model": {"name": "v68", "props": {"ieee-fp": false}}}}

Currently, ``ieee-fp`` is the supported optional feature property. It
controls HVX IEEE floating-point operations. Expansion reports false for
revisions before v68 regardless of the property setting, since those
operations require v68 or later. For v68 and later the default is true,
and an explicit false override disables the feature. Mandatory features
remain implicit in the selected ISA revision.

Expansion accepts feature overrides in the ``model.props`` object;
comma-separated ``-cpu`` option strings are not model names. Unsupported
expansion types, unknown models, unknown properties, and values with the
wrong type return QMP errors. CPU device links, debugger settings, and
translator tuning properties are not feature overrides.

The queries also work with ``-machine none`` and do not create or realize
a CPU. HVX context counts, VTCM, and HMX/HLX resources are configured by
the board and are not implied by a CPU model expansion. Inspect the
machine configuration separately when selecting those resources.
