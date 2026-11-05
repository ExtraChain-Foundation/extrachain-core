"""Exercise the actual history response handler with side-effect-free dependencies."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class HistoryResponseTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which(os.environ.get('RACCOON_TEST_CXX', 'c++'))
        if not compiler:
            raise unittest.SkipTest('a C++ compiler is required')
        source = (Path(__file__).resolve().parents[1] / 'sources/dfs/dfs_controller.cpp').read_text(encoding='utf-8')
        start = source.index('void DfsController::network_response_historical_collection(')
        method = source[start:source.index('\nvoid DfsController::network_response_content_collection(', start)]
        cls.temporary = tempfile.TemporaryDirectory(prefix='dfs history response ')
        cls.addClassCleanup(cls.temporary.cleanup)
        root = Path(cls.temporary.name)
        cls.binary = root / ('fixture.exe' if os.name == 'nt' else 'fixture')
        code = root / 'fixture.cpp'
        code.write_text(r'''
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

using ActorId = std::string;
enum class CollectionOperation { StructuralTemplated, Structural };
struct HistoricalCollectionRow { CollectionOperation operation; std::string data; int sequence; };
struct Effects {
    int reads = 0, actors = 0, parses = 0, templates = 0, creates = 0, paths = 0, opens = 0, closes = 0;
    bool metadata = true, template_found = true, create_ok = true, file_exists = true;
    std::vector<int> writes;
} effects;
template<class... T> void eCritical(const char*, T&&...) {}
namespace Dfs {
struct CollectionTemplate {};
struct CollectionTemplateLink { ActorId owner_id = "template-owner"; std::string file_id = "template-file"; };
namespace Tables::DirsFile::ActorSpace {
std::optional<int> get_dir_row(int, const ActorId&, const std::string&) {
    ++effects.reads;
    return effects.metadata ? std::optional<int>{1} : std::nullopt;
}
std::optional<CollectionTemplate> get_collection_template_file_id(const ActorId&, const std::string&) {
    ++effects.templates;
    return effects.template_found ? std::optional<CollectionTemplate>{CollectionTemplate{}} : std::nullopt;
}
}
namespace Path {
struct File { bool exists() const { return effects.file_exists; } };
std::optional<File> file_path(const ActorId&, const std::string&) { ++effects.paths; return File{}; }
}
namespace Historical { constexpr auto HISTORICAL_TABLE = "history"; }
}
namespace Json {
template<class T> std::optional<T> deserialize(const std::string& data) {
    ++effects.parses;
    return data == "valid" ? std::optional<T>{T{}} : std::nullopt;
}
}
struct Account { int system_actor() { ++effects.actors; return 1; } };
struct Node { Account account; Account* account_controller() { return &account; } };
struct DirsManager { int get_db_instance() { return 1; } };
struct HistoricalCollection {
    static std::optional<HistoricalCollection> create(Node*, int, const ActorId&, const std::string&, Dfs::CollectionTemplate) {
        ++effects.creates;
        return effects.create_ok ? std::optional<HistoricalCollection>{HistoricalCollection{}} : std::nullopt;
    }
    std::filesystem::path get_historical_path() { return "synthetic-history"; }
};
struct DbConnector {
    template<class T> explicit DbConnector(const T&) {}
    void open() { ++effects.opens; }
    void close() { ++effects.closes; }
    void replace(const char*, int row) { effects.writes.push_back(row); }
};
namespace Utils { int to_dbrow(const HistoricalCollectionRow& row) { return row.sequence; } }
struct DfsController {
    Node instance;
    Node* node = &instance;
    DirsManager dirs_manager_;
    void network_response_historical_collection(const ActorId&, const std::string&, const std::vector<HistoricalCollectionRow>&);
};
''' + method + r'''
void require(bool value) { if (!value) { throw std::runtime_error("fixture assertion"); } }
int main(int argc, char** argv) {
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    if (argc != 2) { return 3; }
    try {
        const std::string mode = argv[1];
        DfsController controller;
        std::vector<HistoricalCollectionRow> rows{{CollectionOperation::StructuralTemplated, "valid", 0},
                                                  {CollectionOperation::Structural, "second", 1}};
        if (mode == "empty" || mode == "empty-no-metadata") { rows = std::vector<HistoricalCollectionRow>{}; }
        if (mode == "empty-no-metadata" || mode == "missing-metadata") { effects.metadata = false; }
        if (mode == "structural" || mode == "bad-link" || mode == "missing-template") {
            rows.front().operation = CollectionOperation::Structural;
        }
        if (mode == "bad-template" || mode == "bad-link") { rows.front().data = "invalid"; }
        if (mode == "missing-template") { effects.template_found = false; }
        if (mode == "create-failed") { effects.create_ok = false; }
        if (mode == "file-missing") { effects.file_exists = false; }
        controller.network_response_historical_collection("owner", "file", rows);
        if (mode == "empty" || mode == "empty-no-metadata") {
            require(effects.reads == 0 && effects.actors == 0 && effects.parses == 0 && effects.templates == 0);
            require(effects.creates == 0 && effects.paths == 0 && effects.opens == 0 && effects.closes == 0 && effects.writes.empty());
        } else if (mode == "missing-metadata") {
            require(effects.reads == 1 && effects.actors == 0 && effects.creates == 0 && effects.writes.empty());
        } else if (mode == "bad-template" || mode == "bad-link" || mode == "missing-template") {
            require(effects.reads == 1 && effects.parses == 1 && effects.creates == 0 && effects.writes.empty());
        } else if (mode == "create-failed" || mode == "file-missing") {
            require(effects.creates == 1 && effects.opens == 0 && effects.closes == 0 && effects.writes.empty());
        } else {
            require(effects.reads == 1 && effects.actors == 1 && effects.parses == 1);
            require(effects.templates == (mode == "structural" ? 1 : 0));
            require(effects.creates == 1 && effects.paths == 1 && effects.opens == 1 && effects.closes == 1);
            require(effects.writes == std::vector<int>({0, 1}));
        }
        return 0;
    } catch (...) { return 2; }
}
''', encoding='utf-8')
        if Path(compiler).name.lower() in ('cl', 'cl.exe'):
            command = [compiler, '/nologo', '/std:c++20', '/EHsc', str(code), '/Fe:' + str(cls.binary)]
        else:
            command = [compiler, '-std=c++20', '-fsanitize=undefined', '-fno-sanitize-recover=undefined', str(code), '-o', str(cls.binary)]
        result = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def test_empty_history_and_nonempty_controls(self):
        for mode in ('empty', 'empty-no-metadata', 'missing-metadata', 'templated', 'structural',
                     'bad-template', 'bad-link', 'missing-template', 'create-failed', 'file-missing'):
            with self.subTest(mode=mode):
                result = subprocess.run([str(self.binary), mode], capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 0, mode + ': ' + result.stderr)


if __name__ == '__main__':
    unittest.main()
