#pragma once

#include <string>
#include <vector>
#include <memory>

struct ASTInfo {
    std::string filepath;
    std::string language;
    std::vector<std::string> functions;
    std::vector<std::string> classes;
    std::vector<std::string> imports;
};

class ASTParser {
public:
    // Parses a file and returns extracted AST information
    static std::shared_ptr<ASTInfo> parseFile(const std::string& filepath);

private:
    static std::string readFile(const std::string& filepath);
    static std::string detectLanguage(const std::string& filepath);
};
