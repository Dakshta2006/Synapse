#include "client.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

LLMClient::LLMClient() {
    char* key = std::getenv("GEMINI_API_KEY");
    if (key) {
        apiKey = std::string(key);
    } else {
        std::ifstream envFile(".env");
        if (envFile.is_open()) {
            std::string line;
            while (std::getline(envFile, line)) {
                auto pos = line.find('=');
                if (pos != std::string::npos) {
                    std::string k = line.substr(0, pos);
                    std::string v = line.substr(pos + 1);
                    k.erase(0, k.find_first_not_of(" \t\r\n"));
                    k.erase(k.find_last_not_of(" \t\r\n") + 1);
                    v.erase(0, v.find_first_not_of(" \t\r\n\"'"));
                    v.erase(v.find_last_not_of(" \t\r\n\"'") + 1);
                    if (k == "GEMINI_API_KEY") {
                        apiKey = v;
                        break;
                    }
                }
            }
        }
    }
    if (apiKey.empty()) {
        std::cerr << "Warning: GEMINI_API_KEY environment variable is not set and no .env file found.\n";
    }
}

std::string LLMClient::readFileContent(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return "";
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

std::string LLMClient::requestEdits(const std::string& prompt, const std::vector<std::shared_ptr<DependencyNode>>& contextFiles) {
    if (apiKey.empty()) {
        return "Error: Gemini API key is missing. Please set the GEMINI_API_KEY environment variable.";
    }

    std::string url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key=" + apiKey;

    std::stringstream fullPrompt;
    fullPrompt << "You are an expert developer. You are tasked to help the user modify their code.\n";
    fullPrompt << "The user's prompt is: \"" << prompt << "\"\n\n";
    fullPrompt << "Here are the relevant files in the codebase for context:\n";

    for (const auto& node : contextFiles) {
        std::string content = readFileContent(node->filepath);
        fullPrompt << "\n--- START FILE: " << node->filepath << " ---\n";
        fullPrompt << content;
        fullPrompt << "\n--- END FILE: " << node->filepath << " ---\n";
    }

    fullPrompt << "\n\nInstructions for response:\n";
    fullPrompt << "Provide your edits in SEARCH/REPLACE blocks. For each change, output a block exactly like this:\n";
    fullPrompt << "<<<<<<< SEARCH\n";
    fullPrompt << "[exact lines in the original file to replace]\n";
    fullPrompt << "=======\n";
    fullPrompt << "[the replacement lines]\n";
    fullPrompt << ">>>>>>> REPLACE\n\n";
    fullPrompt << "Also specify the file path of the file to modify above the block like this:\n";
    fullPrompt << "FILE: <filepath>\n\n";
    fullPrompt << "Do not provide explanations or markdown formats outside of these blocks. If no changes are needed, return nothing.";

    json payload;
    payload["contents"] = json::array();
    
    json parts = json::array();
    parts.push_back({{"text", fullPrompt.str()}});
    
    json contentObj;
    contentObj["parts"] = parts;
    payload["contents"].push_back(contentObj);

    std::cout << "[~] Contacting Gemini API..." << std::endl;

    cpr::Response r = cpr::Post(
        cpr::Url{url},
        cpr::Header{{"Content-Type", "application/json"}},
        cpr::Body{payload.dump()}
    );

    if (r.status_code != 200) {
        return "HTTP Error " + std::to_string(r.status_code) + ": " + r.text;
    }

    try {
        json responseJson = json::parse(r.text);
        if (responseJson.contains("candidates") && !responseJson["candidates"].empty()) {
            auto candidate = responseJson["candidates"][0];
            if (candidate.contains("content") && candidate["content"].contains("parts") && !candidate["content"]["parts"].empty()) {
                return candidate["content"]["parts"][0]["text"].get<std::string>();
            }
        }
        return "Error: Unexpected response structure from Gemini API: " + r.text;
    } catch (const std::exception& e) {
        return "Error parsing JSON response: " + std::string(e.what()) + "\nRaw response:\n" + r.text;
    }
}
