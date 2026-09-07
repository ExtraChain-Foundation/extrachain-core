import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest


@unittest.skipUnless(shutil.which('gdb') and sys.platform == 'linux', 'Requires Linux and GDB')
class CrashCaptureTest(unittest.TestCase):
    def run_verifier(self, core, source):
        wrapper = pathlib.Path(__file__).with_name('shadow_crash_capture.py')
        return subprocess.run([sys.executable, str(wrapper), '--core', str(core), '--',
                               sys.executable, '-c', source], capture_output=True, text=True)

    def test_exit_status(self):
        with tempfile.TemporaryDirectory() as directory:
            for status in (0, 7):
                with self.subTest(status=status):
                    core = pathlib.Path(directory) / f'exit-{status}.core'
                    result = self.run_verifier(core, f'raise SystemExit({status})')
                    self.assertEqual(result.returncode, status, result.stdout + result.stderr)
                    self.assertFalse(core.exists())

    def test_fatal_signal_and_existing_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            core = pathlib.Path(directory) / 'abort with space.core'
            result = self.run_verifier(core, 'import os,signal;os.kill(os.getpid(),signal.SIGABRT)')
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('OFFLINE VERIFIER CRASH', result.stdout)
            with core.open('rb') as stream:
                self.assertEqual(stream.read(4), b'\x7fELF')
            original = core.stat()
            script = core.with_suffix('.core.gdb')
            original_script = script.read_bytes()
            retry = self.run_verifier(core, 'raise SystemExit(0)')
            self.assertEqual(retry.returncode, 2)
            self.assertEqual(core.stat().st_mtime_ns, original.st_mtime_ns)
            self.assertEqual(core.stat().st_size, original.st_size)
            self.assertEqual(script.read_bytes(), original_script)


if __name__ == '__main__':
    unittest.main()
