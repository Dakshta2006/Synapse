#include "patcher.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

static std::vector<std::string> splitLines(const std::string& str) {
    std::vector<std::string> lines;
    std::stringstream ss(str);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

std::vector<PatchBlock> Patcher::parseResponse(const std::string& llmResponse) {
    std::vector<PatchBlock> patches;
    auto lines = splitLines(llmResponse);
    
    std::string currentFile = "";
    bool inSearch = false;
    bool inReplace = false;
    std::stringstream searchStream;
    std::stringstream replaceStream;

    for (const auto& line : lines) {
        if (line.rfind("FILE:", 0) == 0) {
            currentFile = line.substr(5);
            currentFile.erase(0, currentFile.find_first_not_of(" \t"));
            currentFile.erase(currentFile.find_last_not_of(" \t") + 1);
            continue;
        }

        if (line == "<<<<<<< SEARCH") {
            inSearch = true;
            searchStream.str("");
            searchStream.clear();
            continue;
        }

        if (line == "=======") {
            inSearch = false;
            inReplace = true;
            replaceStream.str("");
            replaceStream.clear();
            continue;
        }

        if (line == ">>>>>>> REPLACE") {
            inReplace = false;
            if (!currentFile.empty()) {
                PatchBlock patch;
                patch.filepath = currentFile;
                patch.searchText = searchStream.str();
                patch.replaceText = replaceStream.str();
                
                if (!patch.searchText.empty() && patch.searchText.back() == '\n') patch.searchText.pop_back();
                if (!patch.replaceText.empty() && patch.replaceText.back() == '\n') patch.replaceText.pop_back();

                patches.push_back(patch);
            }
            continue;
        }

        if (inSearch) {
            searchStream << line << "\n";
        } else if (inReplace) {
            replaceStream << line << "\n";
        }
    }
    return patches;
}

bool Patcher::createBackup(const std::string& filepath) {
    std::string backupPath = filepath + ".bak";
    try {
        if (fs::exists(backupPath)) {
            // Keep the first backup since it represents the original state before any AI edits
            return true;
        }
        fs::copy_file(filepath, backupPath, fs::copy_options::overwrite_existing);
        std::cout << "[~] Backup created: " << backupPath << "\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error creating backup for " << filepath << ": " << e.what() << "\n";
        return false;
    }
}

bool Patcher::applySinglePatch(const PatchBlock& patch) {
    if (!fs::exists(patch.filepath)) {
        std::cerr << "Error: Target file does not exist: " << patch.filepath << "\n";
        return false;
    }

    std::ifstream fileIn(patch.filepath, std::ios::in | std::ios::binary);
    if (!fileIn.is_open()) {
        std::cerr << "Error opening file to read: " << patch.filepath << "\n";
        return false;
    }
    std::ostringstream ss;
    ss << fileIn.rdbuf();
    std::string content = ss.str();
    fileIn.close();

    std::string normalizedContent = content;
    std::string normalizedSearch = patch.searchText;
    
    size_t pos = normalizedContent.find(normalizedSearch);
    if (pos == std::string::npos) {
        // Normalization fallback (strip \r)
        normalizedContent.erase(std::remove(normalizedContent.begin(), normalizedContent.end(), '\r'), normalizedContent.end());
        normalizedSearch.erase(std::remove(normalizedSearch.begin(), normalizedSearch.end(), '\r'), normalizedSearch.end());
        pos = normalizedContent.find(normalizedSearch);
        
        if (pos == std::string::npos) {
            std::cerr << "Error: Could not find SEARCH block in " << patch.filepath << ". Make sure spacing and code match exactly.\n";
            return false;
        }
        content = normalizedContent;
    }

    if (!createBackup(patch.filepath)) {
        return false;
    }

    content.replace(pos, normalizedSearch.length(), patch.replaceText);

    std::ofstream fileOut(patch.filepath, std::ios::out | std::ios::binary);
    if (!fileOut.is_open()) {
        std::cerr << "Error opening file to write: " << patch.filepath << "\n";
        return false;
    }
    fileOut << content;
    fileOut.close();

    std::cout << "[✓] Patched " << patch.filepath << " successfully!\n";
    return true;
}

bool Patcher::applyPatches(const std::string& llmResponse) {
    auto patches = parseResponse(llmResponse);
    if (patches.empty()) {
        std::cout << "No valid code patches found in LLM response.\n";
        return false;
    }

    bool allSuccess = true;
    for (const auto& patch : patches) {
        if (!applySinglePatch(patch)) {
            allSuccess = false;
        }
    }
    return allSuccess;
}

bool Patcher::undoPatches(const std::string& rootDir) {
    bool foundAny = false;
    try {
        if (!fs::exists(rootDir) || !fs::is_directory(rootDir)) {
            std::cerr << "Invalid directory: " << rootDir << "\n";
            return false;
        }

        for (const auto& entry : fs::recursive_directory_iterator(rootDir)) {
            if (entry.is_regular_file()) {
                std::string pathStr = entry.path().generic_string();
                if (pathStr.length() > 4 && pathStr.substr(pathStr.length() - 4) == ".bak") {
                    std::string originalPath = pathStr.substr(0, pathStr.length() - 4);
                    
                    std::cout << "[~] Restoring backup: " << originalPath << "\n";
                    fs::copy_file(entry.path(), originalPath, fs::copy_options::overwrite_existing);
                    fs::remove(entry.path());
                    foundAny = true;
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error during rollback: " << e.what() << "\n";
        return false;
    }

    if (foundAny) {
        std::cout << "[✓] Undo complete. All backups restored.\n";
    } else {
        std::cout << "No backups found to restore.\n";
    }
    return true;
}
