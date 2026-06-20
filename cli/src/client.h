#pragma once

#include <string>
#include <vector>
#include <memory>
#include "graph.h"

class LLMClient {
public:
    LLMClient();
    
    // Sends the prompt and file contexts to the Gemini API, returns response
    std::string requestEdits(const std::string& prompt, const std::vector<std::shared_ptr<DependencyNode>>& contextFiles);

    // Requests a multi-step edit plan from the LLM based on the user prompt and project dependency graph
    std::string requestPlan(const std::string& prompt, const std::string& projectGraphText);

    // Requests edits for a single file as part of a larger plan
    std::string requestStepEdit(const std::string& prompt, const std::string& planText, const std::string& stepInstruction, const std::string& filepath);

    // Requests edits to fix compiler errors based on logs and file contexts
    std::string requestCompileFix(const std::string& compileErrors, const std::vector<std::string>& filepaths);

private:
    std::string apiKey;
    
    // Reads file content from disk
    std::string readFileContent(const std::string& filepath);
};

