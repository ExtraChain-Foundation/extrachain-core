import copy
import json
import os
from pathlib import Path
import tempfile
import time
import unittest
from unittest.mock import patch

import stand
from stand_evidence import Errors, Lines, Progress, confined, diagnostic, digest, verify_stage, write_json


EXPECT = dict(duration_s=600, interval_s=150, senders=6, per_sender=48, file_bytes=1024)


def successful_progress():
    progress = Progress()
    progress.event(dict(event='configuration', **EXPECT))
    progress.event(dict(event='endurance-start', monotonic_ms=1000))
    for wave in range(4):
        progress.event(dict(event='wave-converged', wave=wave, nodes=8,
                            receipts=(wave + 1) * 288, file_copies=(wave + 1) * 56))
    progress.event(dict(event='shutdown-checkpoint'))
    progress.event(dict(event='offline-audit-start'))
    progress.event(dict(event='PASS', elapsed_s=600, monotonic_ms=601000,
                        waves=4, receipts=1152, file_copies=224))
    return progress


class StandTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.addCleanup(self.temporary.cleanup)

    def run_files(self, state='RUNNING'):
        write_json(self.root / 'manifest.json', dict(purpose='validation', source={'revision': 'abc'}, stages=[]))
        write_json(self.root / 'status.json', dict(state=state, heartbeat=time.time(), stage=None))

    def test_partial_records_and_truncation(self):
        path = self.root / 'events'
        path.write_bytes(b'{"a":')
        reader = Lines(path)
        self.assertEqual(reader.read(), [])
        with path.open('ab') as stream:
            stream.write(b'1}\n')
        self.assertEqual(reader.read(), ['{"a":1}'])
        path.write_text('')
        with self.assertRaises(ValueError):
            reader.read()

    def test_replaced_journal_is_rejected(self):
        path = self.root / 'events'
        path.write_text('one\n')
        reader = Lines(path)
        reader.read()
        other = self.root / 'other'
        other.write_text('two\n')
        other.replace(path)
        with self.assertRaises(ValueError):
            reader.read()

    def test_verdict_requires_continuous_full_workload(self):
        self.assertEqual(successful_progress().verdict(EXPECT), 'PASS')
        for field, value in [('waves', 3), ('elapsed_s', 599), ('receipts', 1151),
                             ('monotonic_ms', 600999), ('file_copies', 223)]:
            progress = successful_progress()
            progress.last[field] = value
            self.assertEqual(progress.verdict(EXPECT), 'FAIL', field)
        progress = successful_progress()
        progress.checkpoint = False
        self.assertEqual(progress.verdict(EXPECT), 'FAIL')
        progress = successful_progress()
        del progress.waves[2]
        self.assertEqual(progress.verdict(EXPECT), 'FAIL')

    def test_interruption_and_failure_are_not_pass(self):
        for event, state in [('INTERRUPTED', 'INTERRUPTED'), ('FAIL', 'FAIL')]:
            progress = Progress()
            progress.event(dict(event=event))
            self.assertEqual(progress.verdict(EXPECT), state)
        self.assertEqual(Progress().verdict(EXPECT), 'INFRA_ERROR')

    def test_restarted_workload_is_rejected(self):
        progress = Progress()
        progress.event(dict(event='endurance-start', monotonic_ms=1))
        with self.assertRaises(ValueError):
            progress.event(dict(event='endurance-start', monotonic_ms=2))

    def test_missing_durable_audit_cannot_pass(self):
        (self.root / 'output.log').write_text('PASS\n')
        work = self.root / 'work'
        work.mkdir()
        write_json(work / 'durable-wave-receipts.json', dict(complete=True, expected=1152,
                   submitted=1152, nodes=[dict(index=n, finalized=1152, same_receipts=True, error=None)
                                          for n in range(8)]))
        write_json(work / 'durable-wave-files.json', dict(complete=True, copies=224, expected_copies=224))
        with self.assertRaisesRegex(ValueError, 'Missing stopped-data audit'):
            verify_stage(dict(kind='endurance', expect=EXPECT), self.root, successful_progress())

    def test_replayed_old_workload_cannot_pass_live_stage(self):
        (self.root / 'output.log').write_text('PASS\n')
        with self.assertRaisesRegex(ValueError, 'not from this continuous stage'):
            verify_stage(dict(kind='endurance', expect=EXPECT), self.root,
                         successful_progress(), started=time.monotonic())

    def test_errors_keep_first_and_group_repeated_messages(self):
        errors = Errors()
        errors.feed('ERROR node 1 failed at 100')
        errors.feed('ERROR node 2 failed at 200')
        for n in range(30):
            errors.feed('ERROR ' + chr(65 + n) * 500)
        result = errors.summary()
        self.assertEqual(result['first'], 'ERROR node 1 failed at 100')
        self.assertEqual(result['groups'][0]['count'], 2)
        self.assertEqual(len(result['groups']), 8)
        self.assertGreater(result['overflow'], 0)
        self.assertLess(len(json.dumps(result)), 5000)

    def test_finalize_waits_for_service_success(self):
        self.run_files('FINALIZING')
        with patch.dict(os.environ, SERVICE_RESULT='success'):
            stand.finalize(self.root)
        self.assertEqual(stand.status_view(self.root)['state'], 'PASS')

    def test_watchdog_and_stop_have_distinct_results(self):
        for stopped, expected in [(False, 'INFRA_ERROR'), (True, 'INTERRUPTED')]:
            self.run_files()
            if stopped:
                write_json(self.root / 'stop-request.json', {})
            with patch.dict(os.environ, SERVICE_RESULT='watchdog'):
                stand.finalize(self.root)
            self.assertEqual(stand.status_view(self.root)['state'], expected)

    def test_cleanup_failure_cannot_pass(self):
        self.run_files('FINALIZING')
        with patch.dict(os.environ, SERVICE_RESULT='timeout'):
            stand.finalize(self.root)
        self.assertEqual(stand.status_view(self.root)['state'], 'INFRA_ERROR')

    def test_status_without_service_proof_cannot_pass(self):
        self.run_files('PASS')
        self.assertEqual(stand.status_view(self.root)['state'], 'FINALIZING')

    def test_path_escape_and_symlink_are_rejected(self):
        with self.assertRaises(ValueError):
            confined(self.root, '../outside')
        (self.root / 'link').symlink_to('/tmp')
        with self.assertRaises(ValueError):
            confined(self.root, 'link/outside')
        with self.assertRaises(ValueError):
            digest(self.root)

    def test_cache_changes_when_inputs_or_workload_change(self):
        value = dict(source={'revision': 'a'}, inputs=[{'sha256': 'b'}], environment={},
                     platform={}, controller={})
        stage = dict(name='unit', kind='command', command=['true'], environment={'LOAD': '1'})
        key = stand.stage_key(value, stage)
        updated = copy.deepcopy(stage)
        updated['environment']['LOAD'] = '2'
        self.assertNotEqual(key, stand.stage_key(value, updated))
        updated = copy.deepcopy(value)
        updated['inputs'][0]['sha256'] = 'changed'
        self.assertNotEqual(key, stand.stage_key(updated, stage))
        self.assertEqual(key, stand.stage_key(value, dict(stage, reuse_from='/old')))

    def test_diagnostics_are_bounded(self):
        (self.root / 'controller.log').write_text('failure ' * 100000)
        errors = Errors()
        for n in range(8):
            errors.feed('ERROR ' + chr(65 + n) + '\U0001f600' * 400)
        write_json(self.root / 'errors.json', errors.summary())
        result = diagnostic(self.root, 'FAIL', '\U0001f600' * 10000)
        self.assertLess(len(json.dumps(result).encode()), 20000)

    def test_changed_link_and_manifest_are_rejected(self):
        first, second, link = [self.root / n for n in ('first', 'second', 'link')]
        first.write_text('same bytes')
        second.write_text('same bytes')
        link.symlink_to(first)
        value = dict(version=1, name='link-test', purpose='validation', root=str(self.root),
                     source={'path': str(self.root)}, min_free_bytes=1,
                     inputs=[{'path': str(link)}], required_stages=['check'],
                     stages=[dict(name='check', kind='command', timeout_s=10, command=['true'],
                                  checks=[dict(type='text', path='output.log', pattern='PASS')])])
        template, frozen = self.root / 'template.json', self.root / 'frozen.json'
        write_json(template, value)
        with patch.object(stand, 'git', side_effect=lambda _, *a: '' if a[0] == 'status' else 'abc'):
            stand.prepare(template, frozen)
            value = json.loads(frozen.read_text())
            stand.verify_inputs(value)
            link.unlink()
            link.symlink_to(second)
            with self.assertRaisesRegex(ValueError, 'Input link changed'):
                stand.verify_inputs(value)
            value['min_free_bytes'] = 2
            with self.assertRaisesRegex(ValueError, 'Manifest changed'):
                stand.verify_inputs(value)


if __name__ == '__main__':
    unittest.main()
