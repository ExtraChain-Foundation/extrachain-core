"""Exercise the actual account discovery/load methods with controlled profile stores."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class ProfileAccessTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which(os.environ.get('RACCOON_TEST_CXX', 'c++'))
        if not compiler:
            raise unittest.SkipTest('a C++23 compiler is required')
        root = Path(__file__).resolve().parents[1]
        header = (root / 'headers/managers/account_controller.h').read_text()
        source = (root / 'sources/managers/account_controller.cpp').read_text()
        errors = header[header.index('enum class LoadError'):header.index('enum class ProfileType')]
        access = header[header.index('struct ProfileAccess'):header.index('/**', header.index('struct ProfileAccess'))]
        load = source[source.index('std::expected<void, LoadError> AccountController::load('):
                      source.index('bool AccountController::load_profile(')]
        discovery = source[source.index('std::set<ActorId> AccountController::multiple_profiles('):
                           source.index('const Actor<KeyPrivate> &AccountController::system_actor(')]
        cls.temp = tempfile.TemporaryDirectory(prefix='account-profile-access-')
        cls.addClassCleanup(cls.temp.cleanup)
        folder = Path(cls.temp.name)
        cls.binary = folder / ('fixture.exe' if os.name == 'nt' else 'fixture')
        cpp = folder / 'fixture.cpp'
        cpp.write_text(r'''
#include <expected>
#include <optional>
#include <set>
#include <string>
#include <stdexcept>
using KeyPass = int;
struct ActorId {
    std::string value;
    std::string to_string() const { return value; }
    bool operator<(const ActorId& other) const { return value < other.value; }
};
void require(bool value) { if (!value) throw std::runtime_error("account access contract"); }
int derivations = 0, legacyReads = 0, seedReads = 0;
bool deriveFails = false, loadSucceeds = true;
std::set<std::string> legacy, seeds;
namespace Cryptography {
    std::expected<KeyPass, int> key_from_password(const std::string& hash) {
        ++derivations;
        if (deriveFails) return std::unexpected(1);
        return hash == "correct" ? 42 : 17;
    }
}
struct PrivateProfile {
    static std::optional<int> read(const ActorId& id, const std::string&, void*, KeyPass key) {
        ++legacyReads;
        return key == 42 && legacy.contains(id.value) ? std::optional<int>(1) : std::nullopt;
    }
};
struct SeedProfile {
    static std::optional<int> load(const std::string& id, KeyPass key) {
        ++seedReads;
        return key == 42 && seeds.contains(id) ? std::optional<int>(1) : std::nullopt;
    }
};
void eLog(const char*) {}
''' + errors + access + r'''
struct AccountController {
    void* node = nullptr;
    std::set<ActorId> listed;
    int loads = 0;
    std::string selected;
    std::set<ActorId> profiles_list() { return listed; }
    bool load_profile(const ActorId& id, const std::string& hash, std::optional<KeyPass> key) {
        ++loads; selected = id.value;
        require(hash == "correct" && key == 42);
        return loadSucceeds;
    }
    std::expected<void, LoadError> load(const std::string&);
    std::set<ActorId> multiple_profiles(const std::string&);
    std::expected<ProfileAccess, LoadError> find_profiles(const std::string&);
};
''' + load + discovery + r'''
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::string mode = argv[1];
    AccountController account;
    if (mode == "empty") {
        auto result = account.find_profiles("");
        require(!result && result.error() == LoadError::EmptyHash && derivations == 0);
    } else if (mode == "no-profiles") {
        auto result = account.find_profiles("correct");
        require(!result && result.error() == LoadError::NoProfiles && derivations == 0);
    } else {
        account.listed = {{"legacy"}, {"seed"}, {"foreign"}};
        legacy = {"legacy"}; seeds = {"seed"};
        if (mode == "wrong-key" || mode == "derive-failure") {
            deriveFails = mode == "derive-failure";
            auto result = account.load("wrong");
            require(!result && result.error() == (deriveFails ? LoadError::Unknown : LoadError::NoAuthProfiles));
            require(derivations == 1 && account.loads == 0);
            if (deriveFails) require(legacyReads == 0 && seedReads == 0);
        } else if (mode == "discover") {
            auto result = account.find_profiles("correct");
            require(result && result->hash == "correct" && result->key == 42 && result->profiles.size() == 2);
            require(derivations == 1 && account.loads == 0 && legacyReads == 3 && seedReads == 2);
        } else if (mode == "multiple") {
            auto result = account.load("correct");
            require(!result && result.error() == LoadError::Multiple && derivations == 1 && account.loads == 0);
        } else if (mode == "legacy-load" || mode == "seed-load" || mode == "load-failure") {
            if (mode == "seed-load") legacy.clear(); else seeds.clear();
            loadSucceeds = mode != "load-failure";
            auto result = account.load("correct");
            require(result.has_value() == loadSucceeds && derivations == 1 && account.loads == 1);
            require(account.selected == (mode == "seed-load" ? "seed" : "legacy"));
            if (!loadSucceeds) require(result.error() == LoadError::Unknown);
        } else if (mode == "list-only") {
            require(account.multiple_profiles("correct").size() == 2 && derivations == 1 && account.loads == 0);
        } else return 3;
    }
}
''', encoding='utf-8')
        if Path(compiler).name.lower() in ('cl', 'cl.exe'):
            command = [compiler, '/nologo', '/std:c++latest', '/EHsc', str(cpp), '/Fe:' + str(cls.binary)]
        else:
            command = [compiler, '-std=c++23', str(cpp), '-o', str(cls.binary)]
        result = subprocess.run(command, cwd=folder, capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def test_profile_discovery_and_login_contract(self):
        for case in ('empty', 'no-profiles', 'wrong-key', 'derive-failure', 'discover',
                     'multiple', 'legacy-load', 'seed-load', 'load-failure', 'list-only'):
            with self.subTest(case=case):
                result = subprocess.run([str(self.binary), case], capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
