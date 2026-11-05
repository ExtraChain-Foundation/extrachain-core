"""Compile the actual admission hash comparison with checked std::optional access."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class CollectionHashTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which(os.environ.get('RACCOON_TEST_CXX', 'c++'))
        if not compiler:
            raise unittest.SkipTest('a C++20 compiler is required')
        repo = Path(__file__).resolve().parents[1]
        source = (repo / 'sources/dfs/load_manager.cpp').read_text(encoding='utf-8')
        admission = source.index('void LoadManager::add_to_queue(const ActorId&')
        start = source.index('auto hash_size = dfs_vector.data_hash_size();', admission)
        opening = source.index('{', start)
        depth, end = 1, opening + 1
        while depth:
            depth += (source[end] == '{') - (source[end] == '}')
            end += 1
        comparison = source[start:end]
        cls.directory = tempfile.TemporaryDirectory(prefix='dfs collection hash ')
        cls.addClassCleanup(cls.directory.cleanup)
        directory = Path(cls.directory.name)
        fixture = directory / 'fixture.cpp'
        cls.binary = directory / ('fixture.exe' if os.name == 'nt' else 'fixture')
        fixture.write_text(r'''
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#ifdef _WIN32
#include <crtdbg.h>
#endif

using HashSize = std::optional<std::pair<std::string, std::uint64_t>>;
struct DfsVector {
    HashSize result;
    int reads = 0;
    HashSize data_hash_size() { ++reads; return result; }
};
struct DirRow { std::string hash = "expected"; };
void check(DfsVector& dfs_vector, const DirRow& dir_row, bool& continued) {
''' + comparison + r'''
    continued = true;
}
int main(int argc, char** argv) {
#ifdef _WIN32
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    if (argc != 2) { return 2; }
    const std::string mode = argv[1];
    DirRow row;
    DfsVector vector;
    if (mode == "absent-empty") { row.hash.clear(); }
    else if (mode == "match") { vector.result = std::pair{row.hash, std::uint64_t{10}}; }
    else if (mode == "different") { vector.result = std::pair{std::string{"other"}, std::uint64_t{10}}; }
    else if (mode != "absent") { return 3; }
    bool continued = false;
    check(vector, row, continued);
    return vector.reads == 1 && continued == (mode != "match") ? 0 : 1;
}
''', encoding='utf-8')
        if Path(compiler).name.lower() in ('cl', 'cl.exe'):
            command = [compiler, '/nologo', '/std:c++20', '/EHsc', '/MTd',
                       '/D_ITERATOR_DEBUG_LEVEL=2', str(fixture), '/Fe:' + str(cls.binary)]
        else:
            command = [compiler, '-std=c++20', '-D_GLIBCXX_ASSERTIONS',
                       str(fixture), '-o', str(cls.binary)]
        result = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def check(self, mode):
        options = {}
        if os.name != 'nt':
            def disable_core():
                import resource
                resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
            options['preexec_fn'] = disable_core
        result = subprocess.run([str(self.binary), mode], capture_output=True, timeout=10, **options)
        self.assertEqual(result.returncode, 0, 'admission hash comparison failed ' + mode)

    def test_unavailable_hash_continues_admission(self):
        self.check('absent')

    def test_unavailable_hash_is_not_an_empty_matching_hash(self):
        self.check('absent-empty')

    def test_matching_available_hash_skips_admission(self):
        self.check('match')

    def test_different_available_hash_continues_admission(self):
        self.check('different')


if __name__ == '__main__':
    unittest.main()
