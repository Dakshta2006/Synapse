#include <iostream>
#include <string>
#include <cxxopts.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <cstdio>
#include <array>
#include <memory>
#include <set>
#include "parser.h"
#include "graph.h"
#include "client.h"
#include "patcher.h"

using json = nlohmann::json;

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

// Returns pair of {stdout_and_stderr, exit_code}
static std::pair<std::string, int> runCommand(const std::string& command) {
    std::string fullCommand = command + " 2>&1";
    std::array<char, 256> buffer;
    std::string result;
    
    FILE* pipe = popen(fullCommand.c_str(), "r");
    if (!pipe) {
        return {"Error spawning command process", -1};
    }
    
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    
    int rawExit = pclose(pipe);
    int exitCode = rawExit;
#ifndef _WIN32
    if (WIFEXITED(rawExit)) {
        exitCode = WEXITSTATUS(rawExit);
    }
#endif
    return {result, exitCode};
}

// Extracted files from compilation errors
static std::vector<std::string> extractFilesFromErrors(const std::string& errors, const std::vector<std::string>& allWorkspaceFiles) {
    std::vector<std::string> results;
    for (const auto& file : allWorkspaceFiles) {
        std::filesystem::path p(file);
        std::string filename = p.filename().string();
        
        if (!filename.empty() && errors.find(filename) != std::string::npos) {
            results.push_back(file);
        }
    }
    return results;
}


int main(int argc, char** argv) {
    try {
        cxxopts::Options options("my_ai", "AI Coding Assistant CLI");

        options.add_options()
            ("p,prompt", "User prompt for the AI", cxxopts::value<std::string>())
            ("d,dir", "Target directory", cxxopts::value<std::string>()->default_value("."))
            ("f,file", "Test AST parser on a specific file", cxxopts::value<std::string>())
            ("u,undo", "Rollback the last applied changes", cxxopts::value<bool>()->default_value("false"))
            ("t,top", "Number of top relevant files to include in context", cxxopts::value<int>()->default_value("3"))
            ("divide", "Enable multi-agent planning (divide and conquer) mode", cxxopts::value<bool>()->default_value("false"))
            ("build-cmd", "Compilation/build command to run for the fix loop", cxxopts::value<std::string>())
            ("max-fixes", "Max number of compiler fix iterations", cxxopts::value<int>()->default_value("3"))
            ("h,help", "Print usage");

        auto result = options.parse(argc, argv);

        if (result.count("help")) {
            std::cout << options.help() << std::endl;
            return 0;
        }

        std::string dir = result["dir"].as<std::string>();
        int topN = result["top"].as<int>();

        // Undo mode
        if (result.count("undo") && result["undo"].as<bool>()) {
            std::cout << "[~] Rolling back changes in: " << dir << "\n";
            Patcher::undoPatches(dir);
            return 0;
        }

        // Test AST parser mode
        if (result.count("file")) {
            std::string filePath = result["file"].as<std::string>();
            std::cout << "[~] Reading & parsing AST for: " << filePath << "\n";
            
            auto info = ASTParser::parseFile(filePath);
            
            std::cout << "========================================\n";
            std::cout << " AST Parser Results\n";
            std::cout << "========================================\n";
            std::cout << "File:     " << info->filepath << "\n";
            std::cout << "Language: " << (info->language.empty() ? "Unsupported/Unknown" : info->language) << "\n";
            
            std::cout << "\n[Classes/Structs] (" << info->classes.size() << "):\n";
            for (const auto& c : info->classes) {
                std::cout << "  - " << c << "\n";
            }

            std::cout << "\n[Functions/Methods] (" << info->functions.size() << "):\n";
            for (const auto& f : info->functions) {
                std::cout << "  - " << f << "\n";
            }

            std::cout << "\n[Imports/Includes] (" << info->imports.size() << "):\n";
            for (const auto& imp : info->imports) {
                std::cout << "  - " << imp << "\n";
            }
            std::cout << "========================================\n";
            return 0;
        }

        if (!result.count("prompt")) {
            std::cerr << "Error: Either --prompt, --file, or --undo argument is required.\n\n";
            std::cerr << options.help() << std::endl;
            return 1;
        }

        std::string prompt = result["prompt"].as<std::string>();
        bool divideMode = result.count("divide") && result["divide"].as<bool>();
        std::string buildCmd = result.count("build-cmd") ? result["build-cmd"].as<std::string>() : "";
        int maxFixes = result["max-fixes"].as<int>();

        std::cout << "========================================\n";
        std::cout << " AI Coding Assistant CLI\n";
        std::cout << "========================================\n";
        std::cout << "Scanning directory: " << dir << "\n";
        
        DependencyGraph graph(dir);
        graph.build();
        
        std::cout << "[✓] Dependency Graph built.\n";
        
        std::vector<std::string> modifiedFilesList;
        std::set<std::string> modifiedFilesSet;
        LLMClient client;

        if (divideMode) {
            std::cout << "\n========================================\n";
            std::cout << " Phase 1: Planner Agent (Divide & Conquer)\n";
            std::cout << "========================================\n";
            
            std::string graphText = graph.serializeGraph();
            std::string planJsonStr = client.requestPlan(prompt, graphText);
            
            std::cout << "[✓] Plan received from Planner Agent.\n";
            
            json planJson;
            try {
                planJson = json::parse(planJsonStr);
            } catch (const std::exception& e) {
                std::cerr << "Error: Failed to parse planner response as JSON. Raw response:\n" 
                          << planJsonStr << "\n" << e.what() << "\n";
                return 1;
            }

            if (!planJson.is_array()) {
                std::cerr << "Error: Planner response is not a JSON array.\n";
                return 1;
            }

            std::cout << "\nProposed Steps:\n";
            for (size_t i = 0; i < planJson.size(); ++i) {
                std::string file = planJson[i].value("file", "unknown");
                std::string instruction = planJson[i].value("instruction", "");
                std::cout << "  " << i + 1 << ". [" << file << "]: " << instruction << "\n";
            }
            
            std::cout << "\n========================================\n";
            std::cout << " Phase 2: Execution Agent (Tiny Context Edits)\n";
            std::cout << "========================================\n";

            for (size_t i = 0; i < planJson.size(); ++i) {
                std::string file = planJson[i].value("file", "");
                std::string instruction = planJson[i].value("instruction", "");
                if (file.empty()) continue;

                std::cout << "\n[Step " << i + 1 << " / " << planJson.size() << "] Modifying: " << file << "\n";
                std::cout << "Instruction: " << instruction << "\n";

                std::string response = client.requestStepEdit(prompt, planJson.dump(2), instruction, file);
                
                std::cout << "[~] Applying patches for: " << file << "...\n";
                if (Patcher::applyPatches(response)) {
                    if (modifiedFilesSet.find(file) == modifiedFilesSet.end()) {
                        modifiedFilesSet.insert(file);
                        modifiedFilesList.push_back(file);
                    }
                }
            }
        } else {
            auto topFiles = graph.rankFiles(prompt, topN);
            
            std::cout << "\n========================================\n";
            std::cout << " Top " << topFiles.size() << " Most Relevant Files for Prompt\n";
            std::cout << "========================================\n";
            if (topFiles.empty()) {
                std::cout << "No matching source files found in the directory.\n";
                return 0;
            } else {
                for (size_t i = 0; i < topFiles.size(); ++i) {
                    std::cout << i + 1 << ". " << topFiles[i]->filepath 
                              << " (Score: " << topFiles[i]->score << ")\n";
                }
            }
            std::cout << "========================================\n";

            // Query LLM
            std::string response = client.requestEdits(prompt, topFiles);
            
            std::cout << "\n========================================\n";
            std::cout << " LLM Response & Suggestions\n";
            std::cout << "========================================\n";
            std::cout << response << "\n";
            std::cout << "========================================\n";

            // Apply patches
            std::cout << "[~] Applying patches...\n";
            if (Patcher::applyPatches(response)) {
                // Parse file paths from the response blocks to track modified files
                std::stringstream ss(response);
                std::string line;
                while (std::getline(ss, line)) {
                    if (line.rfind("FILE:", 0) == 0) {
                        std::string file = line.substr(5);
                        file.erase(0, file.find_first_not_of(" \t\r\n"));
                        file.erase(file.find_last_not_of(" \t\r\n") + 1);
                        if (!file.empty() && modifiedFilesSet.find(file) == modifiedFilesSet.end()) {
                            modifiedFilesSet.insert(file);
                            modifiedFilesList.push_back(file);
                        }
                    }
                }
            }
        }

        // Auto-Compile & Fix Loop (Phase 6)
        if (!buildCmd.empty()) {
            std::cout << "\n========================================\n";
            std::cout << " Phase 6: Auto-Compile & Fix Loop\n";
            std::cout << "========================================\n";

            bool compiledSuccessfully = false;
            for (int attempt = 1; attempt <= maxFixes; ++attempt) {
                std::cout << "\n[~] Building project (Attempt " << attempt << " / " << maxFixes << "):\n";
                std::cout << "Command: " << buildCmd << "\n";
                std::cout << "----------------------------------------\n";
                
                auto [buildOutput, exitCode] = runCommand(buildCmd);
                std::cout << buildOutput;
                std::cout << "----------------------------------------\n";
                std::cout << "Exit Code: " << exitCode << "\n";

                if (exitCode == 0) {
                    std::cout << "[✓] Build succeeded!\n";
                    compiledSuccessfully = true;
                    break;
                }

                std::cout << "[!] Build failed. Requesting LLM correction...\n";

                std::vector<std::string> allFiles = graph.getAllFiles();
                std::vector<std::string> errorFiles = extractFilesFromErrors(buildOutput, allFiles);

                // Combine errorFiles with modified files to create context list
                std::set<std::string> contextSet(modifiedFilesList.begin(), modifiedFilesList.end());
                contextSet.insert(errorFiles.begin(), errorFiles.end());
                std::vector<std::string> contextList(contextSet.begin(), contextSet.end());

                std::string fixResponse = client.requestCompileFix(buildOutput, contextList);

                std::cout << "\n========================================\n";
                std::cout << " Compiler Fix LLM Response\n";
                std::cout << "========================================\n";
                std::cout << fixResponse << "\n";
                std::cout << "========================================\n";

                std::cout << "[~] Applying compile fix patches...\n";
                if (Patcher::applyPatches(fixResponse)) {
                    // Update modified files list with any new files patched
                    std::stringstream ss(fixResponse);
                    std::string line;
                    while (std::getline(ss, line)) {
                        if (line.rfind("FILE:", 0) == 0) {
                            std::string file = line.substr(5);
                            file.erase(0, file.find_first_not_of(" \t\r\n"));
                            file.erase(file.find_last_not_of(" \t\r\n") + 1);
                            if (!file.empty() && modifiedFilesSet.find(file) == modifiedFilesSet.end()) {
                                modifiedFilesSet.insert(file);
                                modifiedFilesList.push_back(file);
                            }
                        }
                    }
                }
            }

            if (!compiledSuccessfully) {
                std::cerr << "\n[!] Warning: Build still failing after " << maxFixes << " attempts.\n";
            }
        }

    } catch (const cxxopts::exceptions::exception& e) {
        std::cerr << "Error parsing options: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
