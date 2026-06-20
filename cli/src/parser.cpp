#include "parser.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <tree_sitter/api.h>

extern "C" const TSLanguage* tree_sitter_cpp();
extern "C" const TSLanguage* tree_sitter_python();

// Helper to extract text from a node
static std::string getNodeText(TSNode node, const std::string& source) {
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    if (start < source.length() && end <= source.length() && start <= end) {
        return source.substr(start, end - start);
    }
    return "";
}

// Helper to recursively find the first identifier node in a subtree
static std::string findFirstIdentifier(TSNode node, const std::string& source) {
    const std::string type = ts_node_type(node);
    if (type == "identifier" || type == "field_identifier") {
        return getNodeText(node, source);
    }
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
        std::string res = findFirstIdentifier(ts_node_child(node, i), source);
        if (!res.empty()) return res;
    }
    return "";
}

// Recursive AST traversal
static void traverseAST(TSNode node, const std::string& source, const std::string& lang, std::shared_ptr<ASTInfo>& info) {
    const std::string type = ts_node_type(node);

    if (lang == "python") {
        if (type == "function_definition") {
            TSNode nameNode = ts_node_child_by_field_name(node, "name", 4);
            if (!ts_node_is_null(nameNode)) {
                info->functions.push_back(getNodeText(nameNode, source));
            } else {
                std::string name = findFirstIdentifier(node, source);
                if (!name.empty()) info->functions.push_back(name);
            }
        } else if (type == "class_definition") {
            TSNode nameNode = ts_node_child_by_field_name(node, "name", 4);
            if (!ts_node_is_null(nameNode)) {
                info->classes.push_back(getNodeText(nameNode, source));
            }
        } else if (type == "import_statement" || type == "import_from_statement") {
            uint32_t count = ts_node_child_count(node);
            for (uint32_t i = 0; i < count; ++i) {
                TSNode child = ts_node_child(node, i);
                std::string childType = ts_node_type(child);
                if (childType == "dotted_name" || childType == "relative_import") {
                    info->imports.push_back(getNodeText(child, source));
                }
            }
        }
    } else if (lang == "cpp") {
        if (type == "function_definition") {
            TSNode decNode = ts_node_child_by_field_name(node, "declarator", 10);
            if (!ts_node_is_null(decNode)) {
                std::string name = findFirstIdentifier(decNode, source);
                if (!name.empty()) info->functions.push_back(name);
            }
        } else if (type == "class_specifier" || type == "struct_specifier") {
            TSNode nameNode = ts_node_child_by_field_name(node, "name", 4);
            if (!ts_node_is_null(nameNode)) {
                info->classes.push_back(getNodeText(nameNode, source));
            }
        } else if (type == "preproc_include") {
            uint32_t count = ts_node_child_count(node);
            for (uint32_t i = 0; i < count; ++i) {
                TSNode child = ts_node_child(node, i);
                std::string childType = ts_node_type(child);
                if (childType == "system_lib_string" || childType == "string_literal") {
                    std::string includeText = getNodeText(child, source);
                    if (includeText.length() > 2) {
                        includeText = includeText.substr(1, includeText.length() - 2);
                    }
                    info->imports.push_back(includeText);
                }
            }
        }
    }

    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
        traverseAST(ts_node_child(node, i), source, lang, info);
    }
}

std::shared_ptr<ASTInfo> ASTParser::parseFile(const std::string& filepath) {
    auto info = std::make_shared<ASTInfo>();
    info->filepath = filepath;
    
    std::string lang = detectLanguage(filepath);
    info->language = lang;

    if (lang.empty()) {
        return info;
    }

    std::string source = readFile(filepath);
    if (source.empty()) {
        return info;
    }

    TSParser* parser = ts_parser_new();
    const TSLanguage* tsLang = nullptr;
    if (lang == "cpp") {
        tsLang = tree_sitter_cpp();
    } else if (lang == "python") {
        tsLang = tree_sitter_python();
    }

    if (!tsLang) {
        ts_parser_delete(parser);
        return info;
    }

    ts_parser_set_language(parser, tsLang);

    TSTree* tree = ts_parser_parse_string(parser, nullptr, source.c_str(), source.length());
    if (tree) {
        TSNode root = ts_tree_root_node(tree);
        traverseAST(root, source, lang, info);
        ts_tree_delete(tree);
    }

    ts_parser_delete(parser);
    return info;
}

std::string ASTParser::detectLanguage(const std::string& filepath) {
    size_t dotPos = filepath.find_last_of('.');
    if (dotPos == std::string::npos) return "";
    std::string ext = filepath.substr(dotPos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == "cpp" || ext == "cc" || ext == "cxx" || ext == "h" || ext == "hpp") {
        return "cpp";
    } else if (ext == "py") {
        return "python";
    }
    return "";
}

std::string ASTParser::readFile(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::in | std::ios::binary);
    if (!file.is_open()) return "";
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}
