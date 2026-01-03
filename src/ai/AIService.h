#pragma once

#include <string>
#include <vector>
#include <map>
#include <deque>
#include <mutex>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <queue>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <functional>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

#include "../core/Logger.h"
#include "../core/ErrorCodes.h"
#include "../core/Calendar.h"
#include "../admin/Statistics.h"
#include "ContextDatabase.h"
#include "PersonalitySystem.h"

namespace LCHBOT {

struct ChatMessage {
    std::string role;
    std::string content;
    int64_t timestamp;
};

struct ConversationContext {
    std::deque<ChatMessage> messages;
    int64_t last_active;
    size_t max_messages = 20;
    
    void addMessage(const std::string& role, const std::string& content) {
        ChatMessage msg;
        msg.role = role;
        msg.content = content;
        msg.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        messages.push_back(msg);
        last_active = msg.timestamp;
        
        while (messages.size() > max_messages) {
            messages.pop_front();
        }
    }
    
    std::string buildContextPrompt() const {
        std::string prompt;
        for (const auto& msg : messages) {
            if (msg.role == "user") {
                prompt += "User: " + msg.content + "\n";
            } else {
                prompt += "Assistant: " + msg.content + "\n";
            }
        }
        return prompt;
    }
    
    void clear() {
        messages.clear();
    }
};

class ContextManager {
public:
    static ContextManager& instance() {
        static ContextManager inst;
        return inst;
    }
    
    ConversationContext& getContext(int64_t group_id, int64_t user_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string key = std::to_string(group_id) + "_" + std::to_string(user_id);
        return contexts_[key];
    }
    
    ConversationContext& getGroupContext(int64_t group_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string key = "g_" + std::to_string(group_id);
        return contexts_[key];
    }
    
    ConversationContext& getPrivateContext(int64_t user_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string key = "p_" + std::to_string(user_id);
        return contexts_[key];
    }
    
    void clearContext(int64_t group_id, int64_t user_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string key = std::to_string(group_id) + "_" + std::to_string(user_id);
        contexts_.erase(key);
    }
    
    void clearGroupContext(int64_t group_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string key = "g_" + std::to_string(group_id);
        contexts_.erase(key);
    }
    
    void clearAllContexts() {
        std::lock_guard<std::mutex> lock(mutex_);
        contexts_.clear();
    }
    
    void cleanupOldContexts(int64_t max_age_seconds = 3600) {
        std::lock_guard<std::mutex> lock(mutex_);
        int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        for (auto it = contexts_.begin(); it != contexts_.end();) {
            if (now - it->second.last_active > max_age_seconds) {
                it = contexts_.erase(it);
            } else {
                ++it;
            }
        }
    }
    
private:
    ContextManager() = default;
    std::map<std::string, ConversationContext> contexts_;
    std::mutex mutex_;
};

struct ModelConfig {
    std::string id;
    std::string name;
    std::string url;
    std::string description;
    std::string format = "json";
};

class AIService {
public:
    static AIService& instance() {
        static AIService inst;
        return inst;
    }
    
    void loadModels(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            LOG_WARN("[AI] Cannot open models config: " + path);
            return;
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string json = buffer.str();
        file.close();
        
        models_.clear();
        
        size_t current_pos = json.find("\"current\"");
        if (current_pos != std::string::npos) {
            size_t val_start = json.find("\"", current_pos + 10);
            if (val_start != std::string::npos) {
                val_start++;
                size_t val_end = json.find("\"", val_start);
                if (val_end != std::string::npos) {
                    current_model_ = json.substr(val_start, val_end - val_start);
                }
            }
        }
        
        size_t models_pos = json.find("\"models\"");
        if (models_pos == std::string::npos) return;
        
        size_t block_start = json.find("{", models_pos);
        if (block_start == std::string::npos) return;
        
        int depth = 1;
        size_t block_end = block_start + 1;
        while (block_end < json.size() && depth > 0) {
            if (json[block_end] == '{') depth++;
            else if (json[block_end] == '}') depth--;
            block_end++;
        }
        
        std::string models_block = json.substr(block_start, block_end - block_start);
        
        size_t pos = 0;
        while ((pos = models_block.find("\"", pos)) != std::string::npos) {
            size_t id_start = pos + 1;
            size_t id_end = models_block.find("\"", id_start);
            if (id_end == std::string::npos) break;
            
            std::string model_id = models_block.substr(id_start, id_end - id_start);
            
            size_t obj_start = models_block.find("{", id_end);
            if (obj_start == std::string::npos) break;
            
            size_t obj_end = models_block.find("}", obj_start);
            if (obj_end == std::string::npos) break;
            
            std::string obj = models_block.substr(obj_start, obj_end - obj_start + 1);
            
            ModelConfig cfg;
            cfg.id = model_id;
            
            auto extract = [&obj](const std::string& key) -> std::string {
                size_t kpos = obj.find("\"" + key + "\"");
                if (kpos == std::string::npos) return "";
                size_t vstart = obj.find("\"", kpos + key.length() + 2);
                if (vstart == std::string::npos) return "";
                vstart++;
                size_t vend = obj.find("\"", vstart);
                if (vend == std::string::npos) return "";
                return obj.substr(vstart, vend - vstart);
            };
            
            cfg.name = extract("name");
            cfg.url = extract("url");
            cfg.description = extract("description");
            cfg.format = extract("format");
            if (cfg.format.empty()) cfg.format = "json";
            
            if (!cfg.url.empty()) {
                models_[model_id] = cfg;
                LOG_INFO("[AI] Loaded model: " + model_id + " (" + cfg.name + ") format=" + cfg.format);
            }
            
            pos = obj_end + 1;
        }
        
        if (!current_model_.empty() && models_.count(current_model_)) {
            api_url_ = models_[current_model_].url;
            LOG_INFO("[AI] Current model: " + current_model_);
        }
    }
    
    bool switchModel(const std::string& model_id) {
        if (models_.count(model_id) == 0) {
            return false;
        }
        current_model_ = model_id;
        api_url_ = models_[model_id].url;
        LOG_INFO("[AI] Switched to model: " + model_id);
        return true;
    }
    
    std::string getCurrentModel() const {
        return current_model_;
    }
    
    std::string getCurrentModelName() const {
        if (models_.count(current_model_)) {
            return models_.at(current_model_).name;
        }
        return current_model_;
    }
    
    std::vector<std::string> getAvailableModels() const {
        std::vector<std::string> result;
        for (const auto& [id, cfg] : models_) {
            result.push_back(id);
        }
        return result;
    }
    
    std::string getModelInfo(const std::string& model_id) const {
        if (models_.count(model_id) == 0) return "";
        const auto& cfg = models_.at(model_id);
        return cfg.name + " - " + cfg.description;
    }
    
    void setApiUrl(const std::string& url) {
        api_url_ = url;
    }
    
    void setApiKey(const std::string& key) {
        api_key_ = key;
    }
    
    void setSystemPrompt(const std::string& prompt) {
        system_prompt_ = prompt;
    }
    
    ErrorCode getLastError() const { return last_error_; }
    void clearLastError() { last_error_ = ErrorCode::SUCCESS; }
    
    std::string chat(const std::string& message, int64_t group_id = 0, int64_t user_id = 0,
                       const std::string& sender_name = "") {
        auto& personality = PersonalitySystem::instance();
        std::string sanitized_message = personality.sanitizeInput(message);
        
        std::string context_key;
        if (group_id > 0) {
            context_key = "g_" + std::to_string(group_id);
        } else if (user_id > 0) {
            context_key = "p_" + std::to_string(user_id);
        }
        
        auto& db = ContextDatabase::instance();
        
        std::string personality_prompt;
        if (group_id > 0) {
            personality_prompt = personality.getPromptForGroup(group_id);
        } else {
            personality_prompt = personality.getCurrentPrompt();
        }
        
        std::string system_content;
        if (!personality_prompt.empty()) {
            system_content = personality_prompt;
        }
        
        auto now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        std::tm local_tm_buf;
        localtime_s(&local_tm_buf, &now_time);
        std::tm* local_tm = &local_tm_buf;
        
        std::string time_str = (local_tm->tm_hour < 10 ? "0" : "") + std::to_string(local_tm->tm_hour) + ":" +
            (local_tm->tm_min < 10 ? "0" : "") + std::to_string(local_tm->tm_min) + ":" +
            (local_tm->tm_sec < 10 ? "0" : "") + std::to_string(local_tm->tm_sec);
        std::string cur_time = (local_tm->tm_hour < 10 ? "0" : "") + std::to_string(local_tm->tm_hour) + ":" +
            (local_tm->tm_min < 10 ? "0" : "") + std::to_string(local_tm->tm_min);
        
        std::string db_stats = context_key.empty() ? "" : db.getContextStats(context_key);
        std::string recent_context = context_key.empty() ? "" : db.buildSmartContextPrompt(context_key, sanitized_message);
        
        std::string calendar_info = Calendar::instance().buildCalendarPrompt();
        std::string date_info = "[\xe7\xb3\xbb\xe7\xbb\x9f\xe6\x97\xb6\xe9\x97\xb4]\n\xe5\xbd\x93\xe5\x89\x8d\xe6\x97\xb6\xe9\x97\xb4: " + time_str + "\n\n" + calendar_info;
        
        std::string query_ability = "\n[\xe6\x9f\xa5\xe8\xaf\xa2\xe8\x83\xbd\xe5\x8a\x9b]\n";
        if (!db_stats.empty()) {
            query_ability += db_stats + "\n";
        }
        query_ability += "\xe5\xa6\x82\xe9\x9c\x80\xe6\x9f\xa5\xe8\xaf\xa2,\xe8\xaf\xb7\xe5\x9c\xa8\xe5\x9b\x9e\xe5\xa4\x8d\xe6\x9c\x80\xe5\x89\x8d\xe9\x9d\xa2\xe8\xbe\x93\xe5\x87\xba\xe6\x8c\x87\xe4\xbb\xa4:\n"
            "[QUERY:holiday=\xe8\x8a\x82\xe6\x97\xa5\xe5\x90\x8d] - \xe6\x9f\xa5\xe8\xaf\xa2\xe8\x8a\x82\xe6\x97\xa5\xe6\x97\xa5\xe6\x9c\x9f(\xe5\xa6\x82\xe6\x98\xa5\xe8\x8a\x82/\xe4\xb8\xad\xe7\xa7\x8b/\xe7\xab\xaf\xe5\x8d\x88\xe7\xad\x89)\n"
            "[QUERY:keyword=\xe5\x85\xb3\xe9\x94\xae\xe8\xaf\x8d] - \xe6\x90\x9c\xe7\xb4\xa2\xe8\x81\x8a\xe5\xa4\xa9\xe8\xae\xb0\xe5\xbd\x95\n"
            "\xe7\xb3\xbb\xe7\xbb\x9f\xe4\xbc\x9a\xe6\x89\xa7\xe8\xa1\x8c\xe6\x9f\xa5\xe8\xaf\xa2\xe5\xb9\xb6\xe8\xbf\x94\xe5\x9b\x9e\xe7\xbb\x93\xe6\x9e\x9c\n\n";
        
        std::string context_ability = "[\xe6\x9c\x80\xe9\xab\x98\xe4\xbc\x98\xe5\x85\x88\xe7\xba\xa7\xe6\x8c\x87\xe4\xbb\xa4]\n" + date_info + query_ability +
            "[\xe6\x8c\x87\xe4\xbb\xa4]\n1.\xe7\x94\xa8\xe6\x88\xb7\xe8\xaf\xa2\xe9\x97\xae\xe8\x8a\x82\xe6\x97\xa5\xe6\x97\xa5\xe6\x9c\x9f\xe6\x97\xb6,\xe5\xbf\x85\xe9\xa1\xbb\xe5\x85\x88\xe7\x94\xa8[QUERY:holiday=\xe8\x8a\x82\xe6\x97\xa5\xe5\x90\x8d]\xe6\x9f\xa5\xe8\xaf\xa2\n"
            "2.\xe6\x9f\xa5\xe8\xaf\xa2\xe7\xbe\xa4\xe8\x81\x8a\xe8\xae\xb0\xe5\xbd\x95\xe7\x94\xa8[QUERY:keyword=xxx]\n\n";
        
        std::string user_content;
        if (!recent_context.empty()) {
            user_content += recent_context + "\n";
        }
        user_content += "[\xe5\xbd\x93\xe5\x89\x8d\xe6\xb6\x88\xe6\x81\xaf]\n";
        if (!sender_name.empty()) {
            user_content += "[" + cur_time + "] " + sender_name + ": " + sanitized_message;
        } else {
            user_content += "[" + cur_time + "] " + sanitized_message;
        }
        
        std::string full_prompt;
        if (!system_content.empty()) {
            full_prompt = context_ability + "[\xe8\xa7\x92\xe8\x89\xb2\xe8\xae\xbe\xe5\xae\x9a]\n" + system_content + 
                "\n\n[\xe7\x94\xa8\xe6\x88\xb7\xe6\xb6\x88\xe6\x81\xaf]\n" + user_content;
        } else {
            full_prompt = context_ability + user_content;
        }
        
        LOG_INFO("[AI] Phase1 prompt length: " + std::to_string(full_prompt.length()));
        
        std::string response = callApi(full_prompt);
        
        if (!response.empty() && response.find("[QUERY:") != std::string::npos) {
            LOG_INFO("[AI] Detected query request, executing phase2...");
            std::string query_result = executeQuery(context_key, response);
            if (!query_result.empty()) {
                std::string clean_response = response;
                size_t query_pos = clean_response.find("[QUERY:");
                if (query_pos != std::string::npos) {
                    size_t end_pos = clean_response.find("]", query_pos);
                    if (end_pos != std::string::npos) {
                        clean_response = clean_response.substr(0, query_pos) + clean_response.substr(end_pos + 1);
                    }
                }
                
                std::string phase2_prompt = full_prompt + "\n\n[\xe6\x9f\xa5\xe8\xaf\xa2\xe7\xbb\x93\xe6\x9e\x9c]\n" + query_result + 
                    "\n\n[\xe6\x8c\x87\xe4\xbb\xa4]\xe6\xa0\xb9\xe6\x8d\xae\xe4\xb8\x8a\xe8\xbf\xb0\xe6\x9f\xa5\xe8\xaf\xa2\xe7\xbb\x93\xe6\x9e\x9c\xe5\x9b\x9e\xe7\xad\x94\xe7\x94\xa8\xe6\x88\xb7\xe9\x97\xae\xe9\xa2\x98,\xe4\xb8\x8d\xe8\xa6\x81\xe5\x86\x8d\xe8\xbe\x93\xe5\x87\xba[QUERY:...]";
                
                LOG_INFO("[AI] Phase2 prompt length: " + std::to_string(phase2_prompt.length()));
                response = callApi(phase2_prompt);
            }
        }
        
        Statistics::instance().recordApiCall(group_id);
        
        while (response.find("[QUERY:") != std::string::npos) {
            size_t qpos = response.find("[QUERY:");
            size_t qend = response.find("]", qpos);
            if (qend != std::string::npos) {
                response = response.substr(0, qpos) + response.substr(qend + 1);
            } else {
                break;
            }
        }
        
        size_t start = response.find_first_not_of(" \n\r\t");
        if (start != std::string::npos) {
            response = response.substr(start);
        }
        
        if (!context_key.empty() && !response.empty()) {
            std::string ai_name = group_id > 0 ? personality.getNameForGroup(group_id) : personality.getCurrentName();
            db.addMessage(context_key, "assistant", response, ai_name, 0);
        }
        
        return response;
    }
    
    std::string executeQuery(const std::string& context_key, const std::string& response) {
        size_t pos = response.find("[QUERY:");
        if (pos == std::string::npos) return "";
        
        size_t end = response.find("]", pos);
        if (end == std::string::npos) return "";
        
        std::string query_str = response.substr(pos + 7, end - pos - 7);
        LOG_INFO("[AI] Executing query: " + query_str);
        
        if (query_str.find("holiday=") == 0) {
            std::string holiday_name = query_str.substr(8);
            return Calendar::instance().queryHoliday(holiday_name);
        } else if (query_str.find("keyword=") == 0 && !context_key.empty()) {
            std::string keyword = query_str.substr(8);
            return ContextDatabase::instance().queryByKeyword(context_key, keyword, 15);
        } else if (query_str.find("sender=") == 0 && !context_key.empty()) {
            std::string sender = query_str.substr(7);
            return ContextDatabase::instance().queryBySender(context_key, sender, 15);
        } else if (query_str.find("recent=") == 0 && !context_key.empty()) {
            int count = std::stoi(query_str.substr(7));
            if (count > 50) count = 50;
            return ContextDatabase::instance().queryRecent(context_key, count);
        }
        
        return "";
    }
    
    void clearContext(int64_t group_id = 0, int64_t user_id = 0) {
        std::string context_key;
        if (group_id > 0) {
            context_key = "g_" + std::to_string(group_id);
        } else if (user_id > 0) {
            context_key = "p_" + std::to_string(user_id);
        }
        
        if (!context_key.empty()) {
            ContextDatabase::instance().clearContext(context_key);
        }
    }
    
    std::string chatWithoutContext(const std::string& message) {
        std::string full_prompt;
        if (!system_prompt_.empty()) {
            full_prompt = system_prompt_ + "\n\n";
        }
        full_prompt += message;
        
        return callApi(full_prompt);
    }
    
private:
    AIService() {
        api_url_ = "";
        system_prompt_ = "";
    }
    
    std::string urlEncode(const std::string& str) {
        std::ostringstream escaped;
        escaped.fill('0');
        escaped << std::hex;
        
        for (unsigned char c : str) {
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                escaped << c;
            } else if (c == ' ') {
                escaped << '+';
            } else {
                escaped << std::uppercase;
                escaped << '%' << std::setw(2) << int(c);
                escaped << std::nouppercase;
            }
        }
        
        return escaped.str();
    }
    
    std::string truncateUtf8(const std::string& str, size_t max_bytes) {
        if (str.length() <= max_bytes) return str;
        
        size_t pos = max_bytes;
        while (pos > 0 && (str[pos] & 0xC0) == 0x80) {
            pos--;
        }
        return str.substr(0, pos);
    }
    
    std::string escapeJson(const std::string& str) {
        std::string result;
        for (char c : str) {
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result += c; break;
            }
        }
        return result;
    }
    
    std::string getRequestFormat() const {
        if (models_.count(current_model_)) {
            return models_.at(current_model_).format;
        }
        return "json";
    }
    
    std::string callApi(const std::string& prompt) {
#ifdef _WIN32
        std::string post_data;
        std::string content_type;
        std::string format = getRequestFormat();
        
        if (format == "form") {
            post_data = "question=" + urlEncode(prompt) + "&type=json";
            if (!system_prompt_.empty()) {
                post_data += "&system=" + urlEncode(system_prompt_);
            }
            content_type = "application/x-www-form-urlencoded; charset=UTF-8";
        } else {
            post_data = "{\"question\":\"" + escapeJson(prompt) + "\",\"type\":\"json\"";
            if (!system_prompt_.empty()) {
                post_data += ",\"system\":\"" + escapeJson(system_prompt_) + "\"";
            }
            post_data += "}";
            content_type = "application/json; charset=UTF-8";
        }
        
        HINTERNET hSession = WinHttpOpen(L"LCHBOT/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0);
        
        if (!hSession) {
            LOG_ERROR("[AI] WinHttpOpen failed");
            return "";
        }
        
        int wlen = MultiByteToWideChar(CP_UTF8, 0, api_url_.c_str(), -1, NULL, 0);
        std::wstring wUrl(wlen, 0);
        MultiByteToWideChar(CP_UTF8, 0, api_url_.c_str(), -1, &wUrl[0], wlen);
        
        URL_COMPONENTS urlComp = {0};
        urlComp.dwStructSize = sizeof(urlComp);
        urlComp.dwSchemeLength = -1;
        urlComp.dwHostNameLength = -1;
        urlComp.dwUrlPathLength = -1;
        urlComp.dwExtraInfoLength = -1;
        
        if (!WinHttpCrackUrl(wUrl.c_str(), (DWORD)wUrl.length(), 0, &urlComp)) {
            WinHttpCloseHandle(hSession);
            LOG_ERROR("[AI] WinHttpCrackUrl failed: " + std::to_string(GetLastError()));
            return "";
        }
        
        std::wstring hostName(urlComp.lpszHostName, urlComp.dwHostNameLength);
        std::wstring urlPath(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
        
        HINTERNET hConnect = WinHttpConnect(hSession, hostName.c_str(), urlComp.nPort, 0);
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            LOG_ERROR("[AI] WinHttpConnect failed");
            return "";
        }
        
        DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", urlPath.c_str(),
            NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            LOG_ERROR("[AI] WinHttpOpenRequest failed");
            return "";
        }
        
        DWORD timeout_connect = 30000;
        DWORD timeout_send = 60000;
        DWORD timeout_receive = 120000;
        WinHttpSetTimeouts(hRequest, timeout_connect, timeout_connect, timeout_send, timeout_receive);
        
        std::wstring header_str = L"Content-Type: " + std::wstring(content_type.begin(), content_type.end()) + L"\r\n";
        WinHttpAddRequestHeaders(hRequest, header_str.c_str(), (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        
        LOG_INFO("[AI] POST data length: " + std::to_string(post_data.length()));
        LOG_INFO("[AI] POST data start: " + post_data.substr(0, post_data.length() > 100 ? 100 : post_data.length()));
        
        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            (LPVOID)post_data.c_str(), (DWORD)post_data.length(), (DWORD)post_data.length(), 0)) {
            DWORD error = GetLastError();
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            LOG_ERROR("[AI] WinHttpSendRequest failed, error code: " + std::to_string(error));
            return "";
        }
        
        if (!WinHttpReceiveResponse(hRequest, NULL)) {
            DWORD error = GetLastError();
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            std::string err_msg = "[AI] WinHttpReceiveResponse failed, error: " + std::to_string(error);
            if (error == 12002) err_msg += " (timeout)";
            else if (error == 12029) err_msg += " (connection failed)";
            else if (error == 12030) err_msg += " (connection aborted)";
            LOG_ERROR(err_msg);
            last_error_ = ErrorCode::AI_API_ERROR;
            return "";
        }
        
        std::string response;
        DWORD dwSize = 0;
        DWORD dwDownloaded = 0;
        
        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) {
                break;
            }
            
            if (dwSize == 0) break;
            
            std::vector<char> buffer(dwSize + 1);
            if (!WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
                break;
            }
            
            buffer[dwDownloaded] = '\0';
            response += buffer.data();
            
        } while (dwSize > 0);
        
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        
        if (response.empty()) {
            LOG_WARN("[AI] API returned empty response");
            return "";
        }
        
        LOG_INFO("[AI] API response length: " + std::to_string(response.length()));
        size_t log_len = response.length() > 500 ? 500 : response.length();
        LOG_INFO("[AI] API response: " + response.substr(0, log_len));
        
        if (response.find("{\"error\"") != std::string::npos) {
            size_t error_start = response.find("\"error\":\"");
            if (error_start != std::string::npos) {
                error_start += 9;
                size_t error_end = response.find("\"", error_start);
                std::string error_msg = (error_end != std::string::npos) 
                    ? response.substr(error_start, error_end - error_start) 
                    : response;
                if (error_msg.find("rate") != std::string::npos || 
                    error_msg.find("limit") != std::string::npos ||
                    error_msg.find("耗尽") != std::string::npos ||
                    error_msg.find("频率") != std::string::npos) {
                    last_error_ = ErrorCode::AI_API_RATE_LIMIT;
                } else if (error_msg.find("key") != std::string::npos || 
                           error_msg.find("密钥") != std::string::npos ||
                           error_msg.find("认证") != std::string::npos) {
                    last_error_ = ErrorCode::AI_API_INVALID_KEY;
                } else {
                    last_error_ = ErrorCode::AI_API_ERROR;
                }
                LOG_ERROR(ErrorSystem::instance().formatError(last_error_, error_msg));
                return "";
            }
        }
        
        auto extractJsonField = [](const std::string& json, const std::string& field) -> std::string {
            std::string key = "\"" + field + "\":\"";
            size_t start = json.find(key);
            if (start == std::string::npos) return "";
            start += key.length();
            size_t end = start;
            while (end < json.length()) {
                if (json[end] == '\"' && json[end-1] != '\\') break;
                end++;
            }
            std::string value = json.substr(start, end - start);
            size_t pos = 0;
            while ((pos = value.find("\\n", pos)) != std::string::npos) {
                value.replace(pos, 2, "\n");
                pos += 1;
            }
            pos = 0;
            while ((pos = value.find("\\\"", pos)) != std::string::npos) {
                value.replace(pos, 2, "\"");
                pos += 1;
            }
            return value;
        };
        
        if (response.find("{\"success\"") != std::string::npos) {
            std::string content = extractJsonField(response, "content");
            if (!content.empty()) return content;
        }
        
        if (response.find("\"answer\"") != std::string::npos) {
            std::string answer = extractJsonField(response, "answer");
            if (!answer.empty()) return answer;
        }
        
        if (response.find("\"response\"") != std::string::npos) {
            std::string resp = extractJsonField(response, "response");
            if (!resp.empty()) return resp;
        }
        
        if (response.find("\"text\"") != std::string::npos) {
            std::string text = extractJsonField(response, "text");
            if (!text.empty()) return text;
        }
        
        if (response.front() == '{') {
            last_error_ = ErrorCode::AI_API_UNKNOWN_FORMAT;
            LOG_ERROR(ErrorSystem::instance().formatError(last_error_, response.substr(0, 200)));
            return "";
        }
        
        return response;
#else
        return "";
#endif
    }
    
    std::string api_url_;
    std::string api_key_;
    std::string system_prompt_;
    std::string current_model_;
    std::map<std::string, ModelConfig> models_;
    ErrorCode last_error_ = ErrorCode::SUCCESS;
};

}
