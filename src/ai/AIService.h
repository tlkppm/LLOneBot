#pragma once

#include <string>
#include <vector>
#include <map>
#include <deque>
#include <mutex>
#include <chrono>
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

class AIService {
public:
    static AIService& instance() {
        static AIService inst;
        return inst;
    }
    
    void setApiUrl(const std::string& url) {
        api_url_ = url;
    }
    
    void setSystemPrompt(const std::string& prompt) {
        system_prompt_ = prompt;
    }
    
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
        
        std::string user_content;
        if (!context_key.empty()) {
            std::string context_prompt = db.buildContextPrompt(context_key, 10);
            if (!context_prompt.empty()) {
                user_content += context_prompt + "\n";
            }
        }
        
        if (!sender_name.empty()) {
            user_content += sender_name + ": " + sanitized_message;
        } else {
            user_content += sanitized_message;
        }
        
        std::string full_prompt;
        if (!system_content.empty()) {
            full_prompt = "[\xE7\xB3\xBB\xE7\xBB\x9F\xE6\x8C\x87\xE4\xBB\xA4]\n" + system_content + "\n\n[\xE7\x94\xA8\xE6\x88\xB7\xE6\xB6\x88\xE6\x81\xAF]\n" + user_content;
        } else {
            full_prompt = user_content;
        }
        
        LOG_INFO("[AI] Full prompt length: " + std::to_string(full_prompt.length()) + 
                 ", system: " + std::to_string(system_content.length()) +
                 ", user: " + std::to_string(user_content.length()));
        
        std::string response = callApi(full_prompt);
        
        Statistics::instance().recordApiCall(group_id);
        
        if (!context_key.empty() && !response.empty()) {
            std::string ai_name = group_id > 0 ? personality.getNameForGroup(group_id) : personality.getCurrentName();
            db.addMessage(context_key, "assistant", response, ai_name, 0);
        }
        
        return response;
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
        api_url_ = "https://api.jkyai.top/API/gemini2.5/index.php";
        system_prompt_ = "You are YunMeng AI Assistant, a friendly and intelligent chatbot. Please reply in concise and natural Chinese.";
    }
    
    std::string urlEncode(const std::string& str) {
        std::ostringstream escaped;
        escaped.fill('0');
        escaped << std::hex;
        
        for (char c : str) {
            if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
                escaped << c;
            } else {
                escaped << std::uppercase;
                escaped << '%' << std::setw(2) << int((unsigned char)c);
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
    
    std::string callApi(const std::string& prompt) {
#ifdef _WIN32
        std::string encoded_prompt = urlEncode(prompt);
        std::string full_url = api_url_ + "?question=" + encoded_prompt + "&type=json";
        
        HINTERNET hSession = WinHttpOpen(L"LCHBOT/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0);
        
        if (!hSession) {
            LOG_ERROR("[AI] WinHttpOpen failed");
            return "";
        }
        
        int wlen = MultiByteToWideChar(CP_UTF8, 0, full_url.c_str(), -1, NULL, 0);
        std::wstring wUrl(wlen, 0);
        MultiByteToWideChar(CP_UTF8, 0, full_url.c_str(), -1, &wUrl[0], wlen);
        
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
        std::wstring urlPath(urlComp.lpszUrlPath, urlComp.dwUrlPathLength + urlComp.dwExtraInfoLength);
        
        HINTERNET hConnect = WinHttpConnect(hSession, hostName.c_str(), urlComp.nPort, 0);
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            LOG_ERROR("[AI] WinHttpConnect failed");
            return "";
        }
        
        DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath.c_str(),
            NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            LOG_ERROR("[AI] WinHttpOpenRequest failed");
            return "";
        }
        
        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            LOG_ERROR("[AI] WinHttpSendRequest failed");
            return "";
        }
        
        if (!WinHttpReceiveResponse(hRequest, NULL)) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            LOG_ERROR("[AI] WinHttpReceiveResponse failed");
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
        
        if (response.find("{\"success\"") != std::string::npos) {
            size_t content_start = response.find("\"content\":\"");
            if (content_start != std::string::npos) {
                content_start += 11;
                size_t content_end = response.find("\",\"uid\"", content_start);
                if (content_end == std::string::npos) {
                    content_end = response.find("\",", content_start);
                }
                if (content_end != std::string::npos) {
                    std::string content = response.substr(content_start, content_end - content_start);
                    size_t pos = 0;
                    while ((pos = content.find("\\n", pos)) != std::string::npos) {
                        content.replace(pos, 2, "\n");
                        pos += 1;
                    }
                    pos = 0;
                    while ((pos = content.find("\\\"", pos)) != std::string::npos) {
                        content.replace(pos, 2, "\"");
                        pos += 1;
                    }
                    return content;
                }
            }
        }
        
        return response;
#else
        return "";
#endif
    }
    
    std::string api_url_;
    std::string system_prompt_;
};

}
