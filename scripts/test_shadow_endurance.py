import pathlib
import tempfile
import time
import types
import unittest
from unittest.mock import Mock, patch

import msgpack

from shadow_endurance import Endurance


class RestartReadinessTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.work = pathlib.Path(self.directory.name)
        self.run = Endurance.__new__(Endurance)
        self.run.work = self.work
        self.run.barrier = self.work / 'barrier'
        self.run.barrier.mkdir()
        self.run.args = types.SimpleNamespace(senders=6, per_sender=48, file_bytes=1024,
                                             recovery=300, port=20000)
        self.run.wave = 1
        self.run.observer_online = False
        self.run.submissions, self.run.cursors, self.run.file_cache = {}, {}, {}
        self.run.last_minted = 0
        self.run.event = Mock()
        self.run.home = lambda index: self.work / f'home-{index}'

    def test_persisted_data_does_not_make_a_restarting_member_ready(self):
        for index in range(7):
            (self.run.barrier / f'node-{index}').write_text('ready')
            state = self.run.home(index) / 'data/consensus/mining-state.msgpack'
            state.parent.mkdir(parents=True)
            state.write_bytes(msgpack.packb(['checkpoint', [0, 100, 10, 5, [], [], []]]))
        marker = self.run.barrier / 'node-0'
        marker.unlink()

        def wait_for(predicate, description, timeout):
            self.assertFalse(predicate(), 'Old receipts cannot prove readiness of the new process')
            marker.write_text('ready')
            self.assertTrue(predicate())

        self.run.wait_for = wait_for
        with patch('shadow_endurance.audit', return_value={'complete': True}), \
                patch('shadow_endurance.audit_waves', return_value={'complete': True}):
            self.run.converge()
        self.assertEqual(self.run.last_minted, 5)
        self.run.event.assert_called_once()

    def test_restart_removes_the_previous_ready_marker_before_launch(self):
        marker = self.run.barrier / 'node-0'
        marker.write_text('ready')
        self.run.deadline = time.monotonic() + 300
        self.run.environment = {}
        self.run.binary = self.work / 'node-run'
        self.run.offline = {0}

        def start(*args):
            self.assertFalse(marker.exists(), 'The previous process cannot mark its replacement ready')
            return types.SimpleNamespace(pid=123)

        self.run.start = start
        child = self.run.start_member(0)
        self.assertEqual(child.pid, 123)
        self.assertEqual((self.run.barrier / 'pid-0').read_text(), '123')
        self.assertNotIn(0, self.run.offline)


if __name__ == '__main__':
    unittest.main()
