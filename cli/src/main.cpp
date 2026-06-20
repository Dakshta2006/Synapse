#include <iostream>
#include <string>
#include <cxxopts.hpp>
#include "parser.h"
#include "graph.h"
#include "client.h"
#include "patcher.h"

int main(int argc, char** argv) {
    try {
        cxxopts::Options options("my_ai", "AI Coding Assistant CLI");

        options.add_options()
            ("p,prompt", "User prompt for the AI", cxxopts::value<std::string>())
            ("d,dir", "Target directory", cxxopts::value<std::string>()->default_value("."))
            ("f,file", "Test AST parser on a specific file", cxxopts::value<std::string>())
            ("u,undo", "Rollback the last applied changes", cxxopts::value<bool>()->default_value("false"))
            ("t,top", "Number of top relevant files to include in context", cxxopts::value<int>()->default_value("3"))
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

        std::cout << "========================================\n";
        std::cout << " AI Coding Assistant CLI\n";
        std::cout << "========================================\n";
        std::cout << "Scanning directory: " << dir << "\n";
        
        DependencyGraph graph(dir);
        graph.build();
        
        std::cout << "[✓] Dependency Graph built.\n";
        
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
        LLMClient client;
        std::string response = client.requestEdits(prompt, topFiles);
        
        std::cout << "\n========================================\n";
        std::cout << " LLM Response & Suggestions\n";
        std::cout << "========================================\n";
        std::cout << response << "\n";
        std::cout << "========================================\n";

        // Apply patches
        std::cout << "[~] Applying patches...\n";
        Patcher::applyPatches(response);

    } catch (const cxxopts::exceptions::exception& e) {
        std::cerr << "Error parsing options: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
