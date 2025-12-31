#pragma once

#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <fstream>
#include <sstream>
#include <chrono>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

#include "../core/Logger.h"

namespace LCHBOT {

struct ContextMessage {
    int64_t id;
    std::string context_key;
    std::string role;
    std::string content;
    int64_t timestamp;
    std::string sender_name;
    int64_t sender_id;
};

class ContextDatabase {
public:
    static ContextDatabase& instance() {
        static ContextDatabase inst;
        return inst;
    }
    
    bool initialize(const std::string& db_path = "data/context.db") {
        std::lock_guard<std::mutex> lock(mutex_);
        
        db_path_ = db_path;
        
        std::filesystem::path path(db_path);
        std::filesystem::create_directories(path.parent_path());
        
        if (!std::filesystem::exists(db_path)) {
            std::ofstream file(db_path);
            file.close();
        }
        
        loadFromFile();
        initialized_ = true;
        LOG_INFO("[ContextDB] Initialized: " + db_path);
        return true;
    }
    
    void addMessage(const std::string& context_key, const std::string& role, 
                    const std::string& content, const std::string& sender_name = "",
                    int64_t sender_id = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        ContextMessage msg;
        msg.id = next_id_++;
        msg.context_key = context_key;
        msg.role = role;
        msg.content = content;
        msg.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        msg.sender_name = sender_name;
        msg.sender_id = sender_id;
        
        messages_.push_back(msg);
        
        compressContext(context_key, 500);
        
        saveToFile();
    }
    
    std::vector<ContextMessage> getContext(const std::string& context_key, size_t limit = 20) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<ContextMessage> result;
        for (const auto& msg : messages_) {
            if (msg.context_key == context_key) {
                result.push_back(msg);
            }
        }
        
        if (result.size() > limit) {
            result.erase(result.begin(), result.begin() + (result.size() - limit));
        }
        
        return result;
    }
    
    std::string buildContextPrompt(const std::string& context_key, size_t limit = 20) {
        auto messages = getContext(context_key, limit);
        
        std::string prompt;
        for (const auto& msg : messages) {
            if (msg.role == "user") {
                if (!msg.sender_name.empty()) {
                    prompt += msg.sender_name + ": " + msg.content + "\n";
                } else {
                    prompt += "User: " + msg.content + "\n";
                }
            } else {
                prompt += "Assistant: " + msg.content + "\n";
            }
        }
        return prompt;
    }
    
    std::string buildSmartContextPrompt(const std::string& context_key, const std::string& current_query) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<ContextMessage> all_messages;
        for (const auto& msg : messages_) {
            if (msg.context_key == context_key) {
                all_messages.push_back(msg);
            }
        }
        
        if (all_messages.empty()) {
            return "";
        }
        
        size_t start = all_messages.size() > 50 ? all_messages.size() - 50 : 0;
        
        std::string prompt = "[群聊历史记录]\n";
        for (size_t i = start; i < all_messages.size(); i++) {
            prompt += formatMessage(all_messages[i]) + "\n";
        }
        
        return prompt;
    }
    
private:
    bool hasKeywordMatch(const std::string& content, const std::string& query) {
        if (query.length() < 2) return false;
        
        std::vector<std::string> keywords;
        std::string word;
        for (char c : query) {
            if (c == ' ' || c == '\n' || c == '\t') {
                if (word.length() >= 2) {
                    keywords.push_back(word);
                }
                word.clear();
            } else {
                word += c;
            }
        }
        if (word.length() >= 2) {
            keywords.push_back(word);
        }
        
        for (const auto& kw : keywords) {
            if (content.find(kw) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
    
    std::string formatMessage(const ContextMessage& msg) {
        if (msg.role == "user") {
            if (!msg.sender_name.empty()) {
                return msg.sender_name + ": " + msg.content;
            }
            return "User: " + msg.content;
        }
        return "Assistant: " + msg.content;
    }
    
public:
    
    void clearContext(const std::string& context_key) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        messages_.erase(
            std::remove_if(messages_.begin(), messages_.end(),
                [&](const ContextMessage& m) { return m.context_key == context_key; }),
            messages_.end()
        );
        
        saveToFile();
    }
    
    void cleanupOldContexts(int64_t max_age_seconds = 86400) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        messages_.erase(
            std::remove_if(messages_.begin(), messages_.end(),
                [&](const ContextMessage& m) { return (now - m.timestamp) > max_age_seconds; }),
            messages_.end()
        );
        
        saveToFile();
    }
    
    size_t getContextSize(const std::string& context_key) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        size_t count = 0;
        for (const auto& msg : messages_) {
            if (msg.context_key == context_key) {
                count++;
            }
        }
        return count;
    }
    
private:
    ContextDatabase() = default;
    
    void compressContext(const std::string& context_key, size_t max_messages) {
        std::vector<size_t> indices;
        for (size_t i = 0; i < messages_.size(); i++) {
            if (messages_[i].context_key == context_key) {
                indices.push_back(i);
            }
        }
        
        if (indices.size() > max_messages) {
            size_t to_remove = indices.size() - max_messages;
            std::vector<size_t> remove_indices(indices.begin(), indices.begin() + to_remove);
            
            std::sort(remove_indices.rbegin(), remove_indices.rend());
            for (size_t idx : remove_indices) {
                messages_.erase(messages_.begin() + idx);
            }
        }
    }
    
    void loadFromFile() {
        std::ifstream file(db_path_);
        if (!file.is_open()) return;
        
        messages_.clear();
        std::string line;
        
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            
            ContextMessage msg;
            std::istringstream iss(line);
            std::string token;
            
            if (!std::getline(iss, token, '\t')) continue;
            msg.id = std::stoll(token);
            
            if (!std::getline(iss, msg.context_key, '\t')) continue;
            if (!std::getline(iss, msg.role, '\t')) continue;
            
            if (!std::getline(iss, token, '\t')) continue;
            msg.timestamp = std::stoll(token);
            
            if (!std::getline(iss, token, '\t')) continue;
            msg.sender_id = std::stoll(token);
            
            if (!std::getline(iss, msg.sender_name, '\t')) continue;
            
            std::getline(iss, msg.content);
            msg.content = unescape(msg.content);
            
            messages_.push_back(msg);
            
            if (msg.id >= next_id_) {
                next_id_ = msg.id + 1;
            }
        }
    }
    
    void saveToFile() {
        std::ofstream file(db_path_);
        if (!file.is_open()) return;
        
        for (const auto& msg : messages_) {
            file << msg.id << "\t"
                 << msg.context_key << "\t"
                 << msg.role << "\t"
                 << msg.timestamp << "\t"
                 << msg.sender_id << "\t"
                 << msg.sender_name << "\t"
                 << escape(msg.content) << "\n";
        }
    }
    
    std::string escape(const std::string& str) {
        std::string result;
        for (char c : str) {
            if (c == '\n') result += "\\n";
            else if (c == '\t') result += "\\t";
            else if (c == '\\') result += "\\\\";
            else result += c;
        }
        return result;
    }
    
    std::string unescape(const std::string& str) {
        std::string result;
        for (size_t i = 0; i < str.size(); i++) {
            if (str[i] == '\\' && i + 1 < str.size()) {
                if (str[i+1] == 'n') { result += '\n'; i++; }
                else if (str[i+1] == 't') { result += '\t'; i++; }
                else if (str[i+1] == '\\') { result += '\\'; i++; }
                else result += str[i];
            } else {
                result += str[i];
            }
        }
        return result;
    }
    
    std::string db_path_;
    std::vector<ContextMessage> messages_;
    std::mutex mutex_;
    int64_t next_id_ = 1;
    bool initialized_ = false;
};

}
