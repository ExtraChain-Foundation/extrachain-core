#ifndef DIFF_H
#define DIFF_H

bool createDiffPatch(
    const std::string& initFilePath,
    const std::string& updatedFilePath,
    const std::string& resultPatchFilePath);

bool applyDiffPatch(
    const std::string& initFilePath,
    const std::string& patchFilePath,
    const std::string& resultNewFilePath);

#endif // DIFF_H
