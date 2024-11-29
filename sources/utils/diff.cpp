#include "utils/diff.h"
#include <QDebug>

int            hdiff_cmd_line(int argc, const char* argv[]);
extern "C" int hpatch_cmd_line(int argc, const char* argv[]);

bool createDiffPatch(
    const std::string& initFilePath,
    const std::string& updatedFilePath,
    const std::string& resultPatchFilePath) {
    static constinit int size = 4;

    if (initFilePath.empty() || updatedFilePath.empty() || resultPatchFilePath.empty())
        return false;

    const char** values = new const char*[size];
    values[0]           = "";
    values[1]           = initFilePath.c_str();
    values[2]           = updatedFilePath.c_str();
    values[3]           = resultPatchFilePath.c_str();

    std::unique_ptr<int, std::function<void(int*)>> temp(new int(1), [&values, size = size](int* ptr) {
        delete ptr;

        // for (int i = 0; i < size; ++i)
        //     delete values[i];
        delete[] values;
    });

    bool res = hdiff_cmd_line(size, (const char**)values) == 0;
    return res;
}

bool applyDiffPatch(
    const std::string& initFilePath,
    const std::string& patchFilePath,
    const std::string& resultNewFilePath) {
    static constinit int size = 4;

    if (initFilePath.empty() || patchFilePath.empty() || resultNewFilePath.empty())
        return false;

    const char** values = new const char*[size];
    values[0]           = "";
    values[1]           = initFilePath.c_str();
    values[2]           = patchFilePath.c_str();
    values[3]           = resultNewFilePath.c_str();

    std::unique_ptr<int, std::function<void(int*)>> temp(new int(1), [&values, size = size](int* ptr) {
        delete ptr;

        // for (int i = 0; i < size; ++i)
        //     delete values[i];
        delete[] values;
    });

    bool res = hpatch_cmd_line(size, (const char**)values) == 0;
    return res;
}
