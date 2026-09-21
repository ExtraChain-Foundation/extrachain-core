"""Opt-in Linux lifecycle tests. Each test owns one transient system service."""
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

import stand
from stand_evidence import write_json


@unittest.skipUnless(os.environ.get('EXTRACHAIN_STAND_SYSTEM_TEST_ROOT'), 'Requires an explicit test root')
class SystemdTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(tempfile.mkdtemp(prefix='controller-tests-',
                        dir=os.environ['EXTRACHAIN_STAND_SYSTEM_TEST_ROOT']))
        cls.fixture = cls.root / 'fixture.py'
        cls.fixture.write_text('''import os, subprocess, sys, time
from pathlib import Path
mode, directory = sys.argv[1:]
child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(180)'], start_new_session=True)
Path(directory, 'child.pid').write_text(str(child.pid))
print('READY', flush=True)
if mode == 'wait':
    time.sleep(180)
print('DONE', flush=True)
raise SystemExit(3 if mode == 'fail' else 0)
''')
        cls.runs = []

    @classmethod
    def tearDownClass(cls):
        for run in cls.runs:
            status = stand.status_view(run)
            subprocess.run(['sudo', '-n', 'systemctl', 'stop', status['unit']],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print(json.dumps({'system_test_evidence': str(cls.root)}))

    def launch(self, mode='pass', timeout=120, disk=1, reuse=None, sequence=False):
        stage = dict(name='fixture', kind='command', timeout_s=timeout, cacheable=True,
                     command=[sys.executable, str(self.fixture), mode, '{stage}'],
                     checks=[dict(type='text', path='output.log', pattern='^DONE$')])
        if reuse:
            stage['reuse_from'] = str(reuse / 'fixture')
        value = dict(version=1, name='lifecycle-test', purpose='validation', root=str(self.root),
                     source={'path': os.environ['EXTRACHAIN_STAND_TEST_SOURCE']},
                     min_free_bytes=1, runtime_min_free_bytes=disk,
                     inputs=[{'path': str(self.fixture)}], stages=[stage], required_stages=['fixture'])
        if sequence:
            value['stages'] = [dict(stage, name=name,
                                   command=[sys.executable, str(self.fixture), outcome, '{stage}'])
                               for name, outcome in [('first', 'pass'), ('second', 'fail'), ('third', 'pass')]]
            value['required_stages'] = ['first', 'second', 'third']
        ident = str(time.time_ns())
        template, frozen = self.root / (ident + '.json'), self.root / (ident + '-frozen.json')
        write_json(template, value)
        stand.prepare(template, frozen)
        run = Path(stand.start(frozen)['directory'])
        self.runs.append(run)
        return run

    def wait_until(self, predicate, timeout=110):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if predicate():
                return
            time.sleep(0.2)
        self.fail('Lifecycle deadline exceeded')

    def complete(self, run, expected):
        self.wait_until(lambda: (run / 'service-result.json').exists()
                        and stand.status_view(run)['state'] in stand.TERMINAL)
        status = stand.status_view(run)
        self.assertEqual(status['state'], expected, status)
        self.assertLess(len(json.dumps(status).encode()), 2048)
        pid_file = run / 'fixture/child.pid'
        if pid_file.exists():
            pid = int(pid_file.read_text())
            self.wait_until(lambda: not Path(f'/proc/{pid}').exists(), 10)

    def ready(self, run):
        self.wait_until(lambda: (run / 'fixture/child.pid').exists())

    def test_success_cleans_detached_descendants_and_cache(self):
        run = self.launch()
        self.complete(run, 'PASS')
        reused = self.launch(reuse=run)
        self.complete(reused, 'PASS')
        result = json.loads((reused / 'fixture/result.json').read_text())
        self.assertEqual(result['reused_from'], str(run / 'fixture'))
        self.assertFalse((reused / 'fixture/child.pid').exists())

    def test_failure(self):
        self.complete(self.launch(mode='fail'), 'FAIL')

    def test_sequence_stops_after_failure(self):
        run = self.launch(sequence=True)
        self.complete(run, 'FAIL')
        self.assertEqual(json.loads((run / 'first/result.json').read_text())['state'], 'PASS')
        self.assertEqual(json.loads((run / 'second/result.json').read_text())['state'], 'FAIL')
        self.assertFalse((run / 'third').exists())
        for stage in ('first', 'second'):
            pid = int((run / stage / 'child.pid').read_text())
            self.wait_until(lambda: not Path(f'/proc/{pid}').exists(), 10)

    def test_disk_guard(self):
        run = self.launch(disk=2**62)
        self.complete(run, 'INFRA_ERROR')
        self.assertFalse((run / 'fixture/child.pid').exists())

    def test_stage_deadline(self):
        self.complete(self.launch(mode='wait', timeout=2), 'INFRA_ERROR')

    def test_user_stop(self):
        run = self.launch(mode='wait')
        self.ready(run)
        stand.stop(run)
        self.complete(run, 'INTERRUPTED')

    def test_controller_crash(self):
        run = self.launch(mode='wait')
        self.ready(run)
        unit = stand.status_view(run)['unit']
        pid = int(subprocess.check_output(['systemctl', 'show', unit, '-p', 'MainPID', '--value']))
        os.kill(pid, signal.SIGKILL)
        self.complete(run, 'INFRA_ERROR')

    def test_watchdog(self):
        run = self.launch(mode='wait')
        self.ready(run)
        unit = stand.status_view(run)['unit']
        pid = int(subprocess.check_output(['systemctl', 'show', unit, '-p', 'MainPID', '--value']))
        os.kill(pid, signal.SIGSTOP)
        self.complete(run, 'INFRA_ERROR')
        service = json.loads((run / 'service-result.json').read_text())
        self.assertEqual(service['result'], 'watchdog')


if __name__ == '__main__':
    unittest.main()
