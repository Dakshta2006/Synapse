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

std::string LLMClient::requestPlan(const std::string& prompt, const std::string& projectGraphText) {
    if (apiKey.empty()) {
        return "[]";
    }

    std::string url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key=" + apiKey;

    std::stringstream fullPrompt;
    fullPrompt << "You are a software architect planning a complex multi-file edit in a codebase.\n";
    fullPrompt << "The user's instruction/prompt is: \"" << prompt << "\"\n\n";
    fullPrompt << "Here is the project dependency graph and files summary for context:\n";
    fullPrompt << projectGraphText << "\n\n";
    fullPrompt << "Based on the prompt and project graph, generate a step-by-step implementation plan.\n";
    fullPrompt << "For each step, specify the target file and the instruction for what needs to be changed in that file.\n";
    fullPrompt << "The steps should be ordered logically (dependencies first, then call sites, then tests).\n\n";
    fullPrompt << "Output your response strictly as a JSON array of objects. Do NOT include markdown code block formatting (such as ```json) or any other text outside the JSON. The response MUST be valid JSON.\n";
    fullPrompt << "JSON format:\n";
    fullPrompt << "[\n";
    fullPrompt << "  {\n";
    fullPrompt << "    \"file\": \"relative/path/to/file1.cpp\",\n";
    fullPrompt << "    \"instruction\": \"Detailed instruction for editing file1.cpp\"\n";
    fullPrompt << "  },\n";
    fullPrompt << "  {\n";
    fullPrompt << "    \"file\": \"relative/path/to/file2.h\",\n";
    fullPrompt << "    \"instruction\": \"Detailed instruction for editing file2.h\"\n";
    fullPrompt << "  }\n";
    fullPrompt << "]\n";

    json payload;
    payload["contents"] = json::array();
    
    json parts = json::array();
    parts.push_back({{"text", fullPrompt.str()}});
    
    json contentObj;
    contentObj["parts"] = parts;
    payload["contents"].push_back(contentObj);

    std::cout << "[~] Generating multi-agent plan with Gemini API..." << std::endl;

    cpr::Response r = cpr::Post(
        cpr::Url{url},
        cpr::Header{{"Content-Type", "application/json"}},
        cpr::Body{payload.dump()}
    );

    if (r.status_code != 200) {
        std::cerr << "HTTP Error " << r.status_code << ": " << r.text << std::endl;
        return "[]";
    }

    try {
        json responseJson = json::parse(r.text);
        if (responseJson.contains("candidates") && !responseJson["candidates"].empty()) {
            auto candidate = responseJson["candidates"][0];
            if (candidate.contains("content") && candidate["content"].contains("parts") && !candidate["content"]["parts"].empty()) {
                std::string resultText = candidate["content"]["parts"][0]["text"].get<std::string>();
                
                // Defensive helper to strip markdown code blocks
                if (resultText.rfind("```json", 0) == 0) {
                    resultText = resultText.substr(7);
                } else if (resultText.rfind("```", 0) == 0) {
                    resultText = resultText.substr(3);
                }
                if (resultText.length() >= 3 && resultText.substr(resultText.length() - 3) == "```") {
                    resultText = resultText.substr(0, resultText.length() - 3);
                }
                // Trim leading/trailing whitespace
                resultText.erase(0, resultText.find_first_not_of(" \t\r\n"));
                resultText.erase(resultText.find_last_not_of(" \t\r\n") + 1);
                
                return resultText;
            }
        }
        return "[]";
    } catch (const std::exception& e) {
        std::cerr << "Error parsing plan JSON response: " << e.what() << std::endl;
        return "[]";
    }
}

std::string LLMClient::requestStepEdit(const std::string& prompt, const std::string& planText, const std::string& stepInstruction, const std::string& filepath) {
    if (apiKey.empty()) {
        return "Error: Gemini API key is missing.";
    }

    std::string url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key=" + apiKey;

    std::stringstream fullPrompt;
    fullPrompt << "You are an expert developer. You are modifying a single file as part of a larger plan.\n";
    fullPrompt << "The overall user goal is: \"" << prompt << "\"\n\n";
    fullPrompt << "The overall plan is:\n" << planText << "\n\n";
    fullPrompt << "We are now executing the following step:\n";
    fullPrompt << "Target File: " << filepath << "\n";
    fullPrompt << "Instruction: " << stepInstruction << "\n\n";
    
    std::string content = readFileContent(filepath);
    fullPrompt << "Here is the current content of \"" << filepath << "\" (and ONLY this file is in your context window):\n";
    fullPrompt << "--- START FILE: " << filepath << " ---\n";
    fullPrompt << content;
    fullPrompt << "\n--- END FILE: " << filepath << " ---\n\n";

    fullPrompt << "Instructions for response:\n";
    fullPrompt << "Provide your edits in SEARCH/REPLACE blocks. For each change, output a block exactly like this:\n";
    fullPrompt << "<<<<<<< SEARCH\n";
    fullPrompt << "[exact lines in the original file to replace]\n";
    fullPrompt << "=======\n";
    fullPrompt << "[the replacement lines]\n";
    fullPrompt << ">>>>>>> REPLACE\n\n";
    fullPrompt << "Also specify the file path of the file to modify above the block like this:\n";
    fullPrompt << "FILE: " << filepath << "\n\n";
    fullPrompt << "Do not provide explanations or markdown formats outside of these blocks. If no changes are needed, return nothing.";

    json payload;
    payload["contents"] = json::array();
    
    json parts = json::array();
    parts.push_back({{"text", fullPrompt.str()}});
    
    json contentObj;
    contentObj["parts"] = parts;
    payload["contents"].push_back(contentObj);

    std::cout << "[~] Fetching step suggestion from Gemini API..." << std::endl;

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
        return "Error: Unexpected response structure from Gemini API.";
    } catch (const std::exception& e) {
        return "Error parsing JSON response: " + std::string(e.what());
    }
}

std::string LLMClient::requestCompileFix(const std::string& compileErrors, const std::vector<std::string>& filepaths) {
    if (apiKey.empty()) {
        return "Error: Gemini API key is missing.";
    }

    std::string url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key=" + apiKey;

    std::stringstream fullPrompt;
    fullPrompt << "You are an expert developer. The previous edits caused compilation or build errors.\n";
    fullPrompt << "Your task is to fix the build errors.\n\n";
    fullPrompt << "Here are the build errors/logs:\n";
    fullPrompt << "--- START BUILD LOG ---\n";
    fullPrompt << compileErrors;
    fullPrompt << "\n--- END BUILD LOG ---\n\n";
    fullPrompt << "Here is the context of the files that were modified or are relevant to the errors:\n";

    for (const auto& filepath : filepaths) {
        std::string content = readFileContent(filepath);
        fullPrompt << "\n--- START FILE: " << filepath << " ---\n";
        fullPrompt << content;
        fullPrompt << "\n--- END FILE: " << filepath << " ---\n";
    }

    fullPrompt << "\nInstructions for response:\n";
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

    std::cout << "[~] Requesting compiler error fixes from Gemini API..." << std::endl;

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
        return "Error: Unexpected response structure from Gemini API.";
    } catch (const std::exception& e) {
        return "Error parsing JSON response: " + std::string(e.what());
    }
}

