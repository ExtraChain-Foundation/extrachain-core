import tempfile
import unittest
from pathlib import Path

from shadow_artifacts import audit_files, homes


class ArtifactAuditTest(unittest.TestCase):
    def test_requires_every_publisher_and_original_bytes_on_every_member(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            directories = homes(work, 7)
            paths = []
            for publisher in range(7):
                owner, file_id = f'{publisher:040x}', f'{publisher:064x}'
                payload = bytes((offset * (131 + publisher) + 17 + publisher * 7) & 255
                                for offset in range(256))
                (work / f'node-{publisher}.log').write_text(
                    f'[node-run] DFS stored owner={owner} file_id={file_id} size=256\n')
                copies = []
                for directory in directories:
                    path = directory / 'dfs' / owner / file_id
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(payload)
                    copies.append(path)
                paths.append(copies)
            cache = {}
            self.assertTrue(audit_files(work, size=256, cache=cache)['complete'])
            publication = work / 'node-5.log'
            publication.write_text('\x1b[0m\x1b[32m' + publication.read_text().rstrip('\n') + '\x1b[0m\n')
            self.assertTrue(audit_files(work, size=256, cache=cache)['complete'])
            for path in paths[0]:
                path.write_bytes(b'X' * 256)
            self.assertFalse(audit_files(work, size=256, cache=cache)['complete'])
            for path in paths[0]:
                path.write_bytes(bytes((offset * 131 + 17) & 255 for offset in range(256)))
            paths[6][6].unlink()
            self.assertFalse(audit_files(work, size=256, cache=cache)['complete'])
            (work / 'node-6.log').write_text('')
            result = audit_files(work, size=256, cache=cache)
            self.assertFalse(result['complete'])
            self.assertEqual(result['expected_copies'], 49)

    def test_late_observer_must_receive_every_existing_publication(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            for publisher in range(7):
                owner, file_id = f'{publisher:040x}', f'{publisher:064x}'
                payload = bytes((offset * (131 + publisher) + 17 + publisher * 7) & 255
                                for offset in range(256))
                (work / f'node-{publisher}.log').write_text(
                    f'[node-run] DFS stored owner={owner} file_id={file_id} size=256\n')
                for directory in homes(work, 8):
                    path = directory / 'dfs' / owner / file_id
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(payload)
            result = audit_files(work, nodes=8, size=256, publishers=7)
            self.assertTrue(result['complete'])
            self.assertEqual(result['copies'], 56)
            self.assertFalse(audit_files(work, nodes=8, size=256)['complete'])
            path.unlink()
            self.assertFalse(audit_files(work, nodes=8, size=256, publishers=7)['complete'])
            for count in (0, 9):
                with self.assertRaises(ValueError):
                    audit_files(work, nodes=8, size=256, publishers=count)


if __name__ == '__main__':
    unittest.main()
