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

private:
    std::string apiKey;
    
    // Reads file content from disk
    std::string readFileContent(const std::string& filepath);
};
