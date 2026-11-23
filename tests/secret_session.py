"""Build and run the secret chat session tests. Needs libsodium: set RACCOON_TEST_SODIUM to a
prefix that holds include/ and lib/ (a vcpkg installed triplet works)."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class SecretSessionTests(unittest.TestCase):
    def test_session(self):
        compiler = shutil.which(os.environ.get('RACCOON_TEST_CXX', 'c++'))
        prefix = os.environ.get('RACCOON_TEST_SODIUM')
        if not compiler or not prefix:
            self.skipTest('a C++23 compiler and RACCOON_TEST_SODIUM are required')
        root = Path(__file__).resolve().parents[1]
        prefix = Path(prefix)
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / 'secret_session_test'
            build = subprocess.run(
                [compiler, '-std=c++23', '-Wall', '-Wextra', '-O1',
                 '-I', str(root / 'headers'), '-I', str(prefix / 'include'),
                 str(root / 'sources/chat/secret_session.cpp'), str(root / 'tests/secret_session.cpp'),
                 str(prefix / 'lib/libsodium.a'), '-o', str(binary)],
                capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == '__main__':
    unittest.main()
