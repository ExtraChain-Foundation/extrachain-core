"""Build and run the secret chat session tests. Needs libsodium: set RACCOON_TEST_SODIUM to a
prefix that holds include/ and lib/ (a vcpkg installed triplet works)."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class SecretSessionTests(unittest.TestCase):
    SOURCES = ('secret_session', 'secret_payload', 'secret_engine')

    def build_and_run(self, test, sources=('secret_session',)):
        compiler = shutil.which(os.environ.get('RACCOON_TEST_CXX', 'c++'))
        prefix = os.environ.get('RACCOON_TEST_SODIUM')
        if not compiler or not prefix:
            self.skipTest('a C++23 compiler and RACCOON_TEST_SODIUM are required')
        root = Path(__file__).resolve().parents[1]
        prefix = Path(prefix)
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / (test + '_bin')
            build = subprocess.run(
                # Qt's emit, slots and signals are macros in the core build: a name that collides with
                # them compiles in isolation and breaks (or silently does nothing) inside the core
                [compiler, '-std=c++23', '-Wall', '-Wextra', '-O1', '-Demit=@', '-Dslots=@', '-Dsignals=@',
                 '-I', str(root / 'headers'), '-I', str(prefix / 'include'),
                 '-I', str(root / 'sources/chat'),
                 *[str(root / 'sources/chat' / (name + '.cpp')) for name in sources],
                 str(root / 'tests' / (test + '.cpp')),
                 str(prefix / 'lib/libsodium.a'), '-o', str(binary)],
                capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_session(self):
        self.build_and_run('secret_session')

    def test_older_readers_refuse_the_envelope(self):
        self.build_and_run('secret_custom_compat')

    def test_engine(self):
        self.build_and_run('secret_engine', self.SOURCES)


if __name__ == '__main__':
    unittest.main()
