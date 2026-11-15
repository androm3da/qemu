#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

"""Hexagon CPU model discovery without a board or realized CPUs."""

from qemu_test import QemuSystemTest


class HexagonCPUModels(QemuSystemTest):
    MODELS = ('v5', 'v55', 'v60', 'v61', 'v62', 'v65', 'v66', 'v67',
              'v68', 'v69', 'v71', 'v73', 'v75', 'v79', 'v81', 'v83', 'v85')

    def setUp(self):
        super().setUp()
        self.set_machine('none')
        self.vm.add_args('-S', '-nodefaults')
        self.vm.launch()

    def expand(self, name, props=None):
        model = {'name': name}
        if props is not None:
            model['props'] = props
        return self.vm.cmd('query-cpu-model-expansion',
                           type='full', model=model)['model']

    def test_definitions(self):
        definitions = self.vm.cmd('query-cpu-definitions')
        names = [cpu['name'] for cpu in definitions]
        self.assertCountEqual(names, self.MODELS)
        for cpu in definitions:
            self.assertEqual(cpu['typename'], cpu['name'] + '-hexagon-cpu')
            self.assertFalse(cpu['static'])
            self.assertFalse(cpu['deprecated'])
            self.assertNotIn('migration-safe', cpu)
            self.assertNotIn('unavailable-features', cpu)
            properties = self.vm.cmd('device-list-properties',
                                     typename=cpu['typename'])
            self.assertIn('ieee-fp', [prop['name'] for prop in properties])

    def test_expansion(self):
        for name in self.MODELS:
            with self.subTest(model=name):
                default = self.expand(name)
                enabled = name not in self.MODELS[:8]
                self.assertEqual(default, {'name': name,
                                          'props': {'ieee-fp': enabled}})
                self.assertEqual(self.expand(name, {'ieee-fp': False}),
                                 {'name': name, 'props': {'ieee-fp': False}})
                self.assertEqual(self.expand(name, {'ieee-fp': True}), default)
                # Overrides affect only the temporary query object.
                self.assertEqual(self.expand(name), default)
                roundtrip = self.vm.cmd('query-cpu-model-expansion',
                                        type='full', model=default)['model']
                self.assertEqual(roundtrip, default)
        self.assertEqual(self.vm.cmd('query-cpus-fast'), [])

    def test_invalid_models(self):
        cases = [
            ('static', {'name': 'v68'}),
            ('full', {'name': 'unknown'}),
            ('full', {'name': 'hexagon-cpu'}),
            ('full', {'name': 'v68,ieee-fp=false'}),
            ('full', {'name': 'v68', 'props': None}),
            ('full', {'name': 'v68', 'props': True}),
            ('full', {'name': 'v68', 'props': []}),
            ('full', {'name': 'v68', 'props': 'invalid'}),
            ('full', {'name': 'v68', 'props': {'ieee-fp': 'false'}}),
            ('full', {'name': 'v68', 'props': {'ieee-fp': 1}}),
            ('full', {'name': 'v68', 'props': {'unknown-feature': True}}),
            ('full', {'name': 'v68', 'props': {'realized': True}}),
            ('full', {'name': 'v68', 'props': {'tlb': '/machine'}}),
            ('full', {'name': 'v68', 'props': {'short-circuit': False}}),
            ('full', {'name': 'v68',
                      'props': {'ieee-fp': False, 'unknown-feature': True}}),
        ]
        for expansion, model in cases:
            with self.subTest(type=expansion, model=model):
                response = self.vm.qmp('query-cpu-model-expansion',
                                       type=expansion, model=model)
                self.assertIn('error', response)
                self.assertTrue(response['error']['desc'])
        self.assertEqual(self.expand('v68'),
                         {'name': 'v68', 'props': {'ieee-fp': True}})
        self.assertEqual(self.vm.cmd('query-cpus-fast'), [])


if __name__ == '__main__':
    QemuSystemTest.main()
