#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "parser.h"

struct DependencyNode {
    std::string filepath;
    std::shared_ptr<ASTInfo> ast;
    std::vector<std::string> resolvedDependencies; // Filepaths this file depends on
    std::vector<std::string> resolvedDependents;   // Filepaths that depend on this file
    double score = 0.0;
};

class DependencyGraph {
public:
    DependencyGraph(const std::string& rootDir);

    // Crawls the directory, parses all files, and builds the dependency graph
    void build();

    // Ranks files based on prompt keyword matching and graph proximity
    std::vector<std::shared_ptr<DependencyNode>> rankFiles(const std::string& query, size_t topN = 3);

    // Debug print
    void printGraph() const;

private:
    std::string rootDir;
    std::unordered_map<std::string, std::shared_ptr<DependencyNode>> nodes;

    // Helper to recursively find all supported source files
    std::vector<std::string> findSourceFiles(const std::string& dir) const;

    // Helper to cross-reference imports/includes with defined files/classes/functions
    void resolveDependencies();
};
