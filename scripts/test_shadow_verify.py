from contextlib import closing, redirect_stdout
import io
import json
import os
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from shadow_verify import capture_checkpoint, main, node_dirs


class MembershipTest(unittest.TestCase):
    def test_includes_optional_observer(self):
        with tempfile.TemporaryDirectory() as temporary, patch.dict(os.environ, {'EXC_VERIFY_SKIP': ''}):
            root = Path(temporary)
            (root / 'bootstrap/server/data').mkdir(parents=True)
            for index in range(1, 7):
                (root / f'bootstrap/client{index}/data').mkdir(parents=True)
            self.assertEqual(len(node_dirs(temporary)), 7)
            (root / 'bootstrap/client7/data').mkdir(parents=True)
            self.assertEqual([name for name, _ in node_dirs(temporary)],
                             [f'node-{index}' for index in range(8)])


class ShutdownCheckpointTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.work = Path(temporary.name)
        environment = patch.dict(os.environ, {'EXC_VERIFY_SKIP': ''})
        environment.start()
        self.addCleanup(environment.stop)
        self.mining = [dict(node=index, section=200, reserved_units=100, minted_units=10)
                       for index in range(8)]
        for index in range(8):
            home = self.work / 'bootstrap' / ('server' if index == 0 else f'client{index}') / 'data'
            (home / 'consensus').mkdir(parents=True)
            (home / 'dag/hot').mkdir(parents=True)
            with closing(sqlite3.connect(home / 'consensus/safety.sqlite')) as database, database:
                database.executescript(
                    'CREATE TABLE consensus_finality_proofs (last_section INTEGER, finalized_hash TEXT);'
                    'CREATE TABLE consensus_batches (hash TEXT PRIMARY KEY, payload TEXT);')
                database.execute('INSERT INTO consensus_finality_proofs VALUES (200, ?)', ('a' * 64,))
                database.execute('INSERT INTO consensus_batches VALUES (?, ?)', ('a' * 64, 'batch'))
            with closing(sqlite3.connect(home / 'dag/hot/HotSections.db')) as database, database:
                database.execute('CREATE TABLE sections (section INTEGER PRIMARY KEY, payload TEXT)')
                database.executemany('INSERT INTO sections VALUES (?, ?)',
                                     [(section, '{"transactions": []}')
                                      for section in range(100, 201 + 20 * index)])
        self.homes = node_dirs(self.work)
        self.checkpoint = self.work / 'checkpoint.json'
        self.checkpoint.write_text(json.dumps(capture_checkpoint(self.homes, self.mining)))

    def verify(self, checkpoint=True):
        arguments = ['shadow_verify.py', str(self.work)]
        if checkpoint:
            arguments += ['--checkpoint', str(self.checkpoint)]
        with patch.object(sys, 'argv', arguments), redirect_stdout(io.StringIO()):
            return main()

    def mutate(self, relative, sql):
        with closing(sqlite3.connect(Path(self.homes[-1][1]) / relative)) as database, database:
            database.execute(sql)

    def test_retains_shared_checkpoint_with_valid_shutdown_suffixes(self):
        self.assertEqual(self.verify(checkpoint=False), 1)
        self.assertEqual(self.verify(), 0)

    def test_lagging_live_node_cannot_capture_checkpoint(self):
        self.mining[-1]['section'] = 180
        self.assertIsNone(capture_checkpoint(self.homes, self.mining))

    def test_live_counters_must_agree(self):
        self.mining[-1]['minted_units'] = 9
        self.assertIsNone(capture_checkpoint(self.homes, self.mining))

    def test_live_finalized_batches_must_agree(self):
        self.mutate('consensus/safety.sqlite', "UPDATE consensus_batches SET payload = 'other'")
        with self.assertRaises(ValueError):
            capture_checkpoint(self.homes, self.mining)

    def test_stopped_node_must_retain_batch(self):
        self.mutate('consensus/safety.sqlite', 'DELETE FROM consensus_batches')
        self.assertEqual(self.verify(), 1)

    def test_stopped_node_must_retain_finality_proof(self):
        self.mutate('consensus/safety.sqlite', 'DELETE FROM consensus_finality_proofs')
        self.assertEqual(self.verify(), 1)

    def test_stopped_batch_must_not_change(self):
        self.mutate('consensus/safety.sqlite', "UPDATE consensus_batches SET payload = 'other'")
        self.assertEqual(self.verify(), 1)

    def test_stopped_node_cannot_roll_back(self):
        self.mutate('dag/hot/HotSections.db', 'DELETE FROM sections WHERE section >= 200')
        self.assertEqual(self.verify(), 1)

    def test_common_content_must_still_match(self):
        self.mutate('dag/hot/HotSections.db',
                    "UPDATE sections SET payload = '{\"transactions\": [1]}' WHERE section = 190")
        self.assertEqual(self.verify(), 1)

    def test_common_coverage_must_still_match(self):
        self.mutate('dag/hot/HotSections.db', 'DELETE FROM sections WHERE section = 190')
        self.assertEqual(self.verify(), 1)

    def test_empty_common_range_cannot_pass(self):
        self.mutate('dag/hot/HotSections.db', 'DELETE FROM sections WHERE section <= 200')
        self.assertEqual(self.verify(), 1)

    def test_missing_checkpoint_fails(self):
        self.checkpoint.unlink()
        self.assertEqual(self.verify(), 1)

    def test_changed_membership_fails(self):
        with patch.dict(os.environ, {'EXC_VERIFY_SKIP': '7'}):
            self.assertEqual(self.verify(), 1)


if __name__ == '__main__':
    unittest.main()
