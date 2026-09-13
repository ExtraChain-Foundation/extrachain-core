#!/usr/bin/env python3
"""Reject incomplete and corrupt wave artifacts, including stale publication records."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from shadow_artifacts import homes
from shadow_wave_audit import audit_waves


class WaveAuditTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)
        (self.work / 'barrier').mkdir()
        self.size = 513
        for wave in range(2):
            for publisher in range(7):
                record = dict(owner=f'{publisher + 1:040x}', file_id=f'{wave + 1:064x}',
                              size=str(self.size), wave=str(wave))
                self.marker(wave, publisher).write_text(json.dumps(record))
                content = bytes((offset * (131 + publisher) + 17 + publisher * 7 + wave) & 255
                                for offset in range(self.size))
                for home in homes(self.work, 8):
                    path = home / 'dfs' / record['owner'] / record['file_id']
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(content)

    def marker(self, wave=1, publisher=6):
        return self.work / 'barrier' / f'published-wave-{wave}-{publisher}.json'

    def payload(self):
        return homes(self.work, 8)[7] / 'dfs' / f'{7:040x}' / f'{2:064x}'

    def audit(self, cache=None):
        return audit_waves(self.work, 2, 8, self.size, cache)

    def test_complete_then_corrupt_cached_copy(self):
        cache = {}
        result = self.audit(cache)
        self.assertTrue(result['complete'])
        self.assertEqual(result['copies'], 112)
        self.payload().write_bytes(bytes(self.size))
        self.assertFalse(self.audit(cache)['complete'])
        self.assertEqual(len(cache), 111)

    def test_missing_copy(self):
        self.payload().unlink()
        self.assertFalse(self.audit()['complete'])

    def test_missing_marker(self):
        self.marker().unlink()
        self.assertFalse(self.audit()['complete'])

    def test_invalid_marker(self):
        original = json.loads(self.marker().read_text())
        for field, value in (('wave', '0'), ('file_id', '../escape'), ('owner', []),
                             ('size', str(self.size + 1))):
            with self.subTest(field=field):
                self.marker().write_text(json.dumps(dict(original, **{field: value})))
                self.assertFalse(self.audit()['complete'])

    def test_reused_identity(self):
        record = json.loads(self.marker(0, 6).read_text())
        record['wave'] = '1'
        self.marker().write_text(json.dumps(record))
        self.assertFalse(self.audit()['complete'])

    def test_symlink_copy(self):
        original = self.payload().read_bytes()
        target = self.work / 'target'
        target.write_bytes(original)
        self.payload().unlink()
        self.payload().symlink_to(target)
        self.assertFalse(self.audit()['complete'])


if __name__ == '__main__':
    unittest.main()
