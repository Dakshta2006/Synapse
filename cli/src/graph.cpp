#include "graph.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <sstream>
#include <set>

namespace fs = std::filesystem;

DependencyGraph::DependencyGraph(const std::string& rootDir) : rootDir(rootDir) {}

std::vector<std::string> DependencyGraph::findSourceFiles(const std::string& dir) const {
    std::vector<std::string> files;
    try {
        if (!fs::exists(dir) || !fs::is_directory(dir)) {
            return files;
        }

        for (const auto& entry : fs::recursive_directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                
                if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || 
                    ext == ".h" || ext == ".hpp" || ext == ".py") {
                    // Convert backslashes to forward slashes for cross-platform consistency
                    files.push_back(entry.path().generic_string());
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error scanning directory: " << e.what() << std::endl;
    }
    return files;
}

void DependencyGraph::build() {
    auto filepaths = findSourceFiles(rootDir);
    
    for (const auto& path : filepaths) {
        auto ast = ASTParser::parseFile(path);
        auto node = std::make_shared<DependencyNode>();
        node->filepath = path;
        node->ast = ast;
        nodes[path] = node;
    }

    resolveDependencies();
}

void DependencyGraph::resolveDependencies() {
    for (auto& [path, node] : nodes) {
        for (const auto& importName : node->ast->imports) {
            // Find another node in the graph that matches this import
            for (auto& [otherPath, otherNode] : nodes) {
                if (otherPath == path) continue;

                fs::path otherFsPath(otherPath);
                std::string otherFilename = otherFsPath.filename().string();
                std::string otherStem = otherFsPath.stem().string();

                // Check matches:
                // 1. Exact filename (e.g. import name is "auth.h" and filename is "auth.h")
                // 2. Base name (e.g. import name is "auth" and stem is "auth")
                // 3. Substring check (e.g. import contains "utils/helper" and other path contains "utils/helper")
                
                bool matches = false;
                if (importName == otherFilename || importName == otherStem) {
                    matches = true;
                } else if (otherPath.find(importName) != std::string::npos) {
                    matches = true;
                }

                if (matches) {
                    node->resolvedDependencies.push_back(otherPath);
                    otherNode->resolvedDependents.push_back(path);
                }
            }
        }
    }
}

// Simple stop-words set to ignore during keyword matching
static const std::set<std::string> stopWords = {
    "the", "a", "an", "and", "or", "but", "if", "then", "else", "for", "to", "in", 
    "on", "at", "by", "from", "with", "about", "fix", "bug", "add", "implement",
    "create", "make", "feature", "change", "update", "delete", "remove"
};

static std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::string clean = text;
    // Replace non-alphanumeric with spaces
    for (char& c : clean) {
        if (!std::isalnum(static_cast<unsigned char>(c))) {
            c = ' ';
        } else {
            c = std::tolower(static_cast<unsigned char>(c));
        }
    }

    std::stringstream ss(clean);
    std::string token;
    while (ss >> token) {
        if (stopWords.find(token) == stopWords.end()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}

static bool containsIgnoreCase(const std::string& haystack, const std::string& needle) {
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](char ch1, char ch2) { return std::tolower(ch1) == std::tolower(ch2); }
    );
    return it != haystack.end();
}

std::vector<std::shared_ptr<DependencyNode>> DependencyGraph::rankFiles(const std::string& query, size_t topN) {
    auto queryTokens = tokenize(query);
    
    // 1. Initial Scoring (Lexical matching)
    for (auto& [path, node] : nodes) {
        node->score = 0.0;
        if (queryTokens.empty()) continue;

        fs::path fsPath(path);
        std::string filename = fsPath.filename().string();
        std::string fileContent = ASTParser::parseFile(path)->filepath; // Read file again or read from contents
        
        // Let's actually read file content to do full text search if needed
        std::ifstream file(path);
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();

        double fileScore = 0.0;
        for (const auto& token : queryTokens) {
            // Check filename (+1.5)
            if (containsIgnoreCase(filename, token)) {
                fileScore += 1.5;
            }
            
            // Check class names (+1.0)
            for (const auto& cls : node->ast->classes) {
                if (containsIgnoreCase(cls, token)) {
                    fileScore += 1.0;
                }
            }

            // Check function names (+0.8)
            for (const auto& func : node->ast->functions) {
                if (containsIgnoreCase(func, token)) {
                    fileScore += 0.8;
                }
            }

            // Check file content (+0.2)
            if (containsIgnoreCase(content, token)) {
                fileScore += 0.2;
            }
        }
        node->score = fileScore;
    }

    // 2. Score Propagation (Graph weights)
    // We create a copy of the scores to apply propagation concurrently
    std::unordered_map<std::string, double> propagatedScores;
    for (const auto& [path, node] : nodes) {
        propagatedScores[path] = node->score;
    }

    for (const auto& [path, node] : nodes) {
        if (node->score > 0.0) {
            double distribute = node->score * 0.3; // Distribute 30% of the score to neighbors
            
            // Propagate to dependencies
            for (const auto& dep : node->resolvedDependencies) {
                propagatedScores[dep] += distribute;
            }
            // Propagate to dependents
            for (const auto& dep : node->resolvedDependents) {
                propagatedScores[dep] += distribute;
            }
        }
    }

    // Apply propagated scores back
    std::vector<std::shared_ptr<DependencyNode>> rankedList;
    for (auto& [path, node] : nodes) {
        node->score = propagatedScores[path];
        rankedList.push_back(node);
    }

    // 3. Sort by score
    std::sort(rankedList.begin(), rankedList.end(), [](const auto& a, const auto& b) {
        return a->score > b->score;
    });

    // Trim to topN
    if (rankedList.size() > topN) {
        rankedList.resize(topN);
    }

    return rankedList;
}

void DependencyGraph::printGraph() const {
    std::cout << "\n========================================\n";
    std::cout << " Dependency Graph (" << nodes.size() << " nodes):\n";
    std::cout << "========================================\n";
    for (const auto& [path, node] : nodes) {
        std::cout << "Node: " << path << "\n";
        std::cout << "  Functions: " << node->ast->functions.size() << ", Classes: " << node->ast->classes.size() << "\n";
        std::cout << "  Imports:   ";
        for (const auto& imp : node->ast->imports) std::cout << imp << " ";
        std::cout << "\n  Depends On: ";
        for (const auto& dep : node->resolvedDependencies) std::cout << dep << " ";
        std::cout << "\n  Dependents: ";
        for (const auto& dep : node->resolvedDependents) std::cout << dep << " ";
        std::cout << "\n----------------------------------------\n";
    }
}
