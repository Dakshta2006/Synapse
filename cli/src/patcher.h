#pragma once

#include <string>
#include <vector>

struct PatchBlock {
    std::string filepath;
    std::string searchText;
    std::string replaceText;
};

class Patcher {
public:
    // Parses SEARCH/REPLACE blocks from LLM output and applies them
    static bool applyPatches(const std::string& llmResponse);

    // Recursively restores all .bak files in the directory and deletes backups
    static bool undoPatches(const std::string& rootDir);

private:
    static std::vector<PatchBlock> parseResponse(const std::string& llmResponse);
    static bool applySinglePatch(const PatchBlock& patch);
    static bool createBackup(const std::string& filepath);
};
