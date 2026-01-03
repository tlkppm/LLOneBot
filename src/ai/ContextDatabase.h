#pragma once

#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <fstream>
#include <sstream>
#include <chrono>
#include <filesystem>
#include <map>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

#include "../core/Logger.h"
#include "../core/Database.h"

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
        
        auto& db = Database::instance();
        db.open(db_path);
        
        db.execute(R"(
            CREATE TABLE IF NOT EXISTS messages (
                id INTEGER PRIMARY KEY,
                context_key TEXT,
                role TEXT,
                content TEXT,
                timestamp INTEGER,
                sender_name TEXT,
                sender_id INTEGER
            )
        )");
        
        db.execute("CREATE INDEX IF NOT EXISTS idx_context_key ON messages(context_key)");
        db.execute("CREATE INDEX IF NOT EXISTS idx_timestamp ON messages(timestamp)");
        db.execute("CREATE INDEX IF NOT EXISTS idx_sender ON messages(sender_name)");
        
        migrateOldData();
        initialized_ = true;
        LOG_INFO("[ContextDB] Initialized: " + db_path);
        return true;
    }
    
    void addMessage(const std::string& context_key, const std::string& role, 
                    const std::string& content, const std::string& sender_name = "",
                    int64_t sender_id = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        int64_t timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        auto& db = Database::instance();
        db.execute("INSERT INTO messages (context_key, role, content, timestamp, sender_name, sender_id) VALUES (?, ?, ?, ?, ?, ?)",
            {DbValue(context_key), DbValue(role), DbValue(content), DbValue(timestamp), DbValue(sender_name), DbValue(sender_id)});
        
        compressContext(context_key, 2000);
    }
    
    std::vector<ContextMessage> getContext(const std::string& context_key, size_t limit = 20) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto& db = Database::instance();
        auto rows = db.query("SELECT * FROM messages WHERE context_key = ? ORDER BY timestamp DESC LIMIT ?",
            {DbValue(context_key), DbValue(static_cast<int64_t>(limit))});
        
        std::vector<ContextMessage> result;
        for (const auto& row : rows) {
            result.push_back(rowToMessage(row));
        }
        
        std::reverse(result.begin(), result.end());
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
        auto all_messages = getContext(context_key, 200);
        
        if (all_messages.empty()) {
            return "";
        }
        
        size_t max_chars = 15000;
        
        std::string prompt = "[\xE7\xBE\xA4\xE8\x81\x8A\xE5\x8E\x86\xE5\x8F\xB2\xE8\xAE\xB0\xE5\xBD\x95] (\xE5\x85\xB1" + std::to_string(all_messages.size()) + "\xE6\x9D\xA1)\n";
        
        for (size_t i = 0; i < all_messages.size(); i++) {
            std::string msg_str = formatMessage(all_messages[i]) + "\n";
            if (prompt.length() + msg_str.length() > max_chars) {
                prompt = "[\xE7\xBE\xA4\xE8\x81\x8A\xE5\x8E\x86\xE5\x8F\xB2\xE8\xAE\xB0\xE5\xBD\x95] (\xE5\xB7\xB2\xE6\x88\xAA\xE6\x96\xAD)\n";
                size_t new_start = i + (all_messages.size() - i) / 2;
                for (size_t j = new_start; j < all_messages.size(); j++) {
                    prompt += formatMessage(all_messages[j]) + "\n";
                }
                break;
            }
            prompt += msg_str;
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
        auto& db = Database::instance();
        db.execute("DELETE FROM messages WHERE context_key = ?", {DbValue(context_key)});
    }
    
    void cleanupOldContexts(int64_t max_age_seconds = 604800) {
        std::lock_guard<std::mutex> lock(mutex_);
        int64_t cutoff = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() - max_age_seconds;
        auto& db = Database::instance();
        db.execute("DELETE FROM messages WHERE timestamp < ?", {DbValue(cutoff)});
    }
    
    size_t getContextSize(const std::string& context_key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& db = Database::instance();
        auto rows = db.query("SELECT * FROM messages WHERE context_key = ?", {DbValue(context_key)});
        return rows.size();
    }
    
    std::string queryByKeyword(const std::string& context_key, const std::string& keyword, size_t limit = 10) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& db = Database::instance();
        auto rows = db.query("SELECT * FROM messages WHERE context_key = ? AND content LIKE ? ORDER BY timestamp DESC LIMIT ?",
            {DbValue(context_key), DbValue("%" + keyword + "%"), DbValue(static_cast<int64_t>(limit))});
        
        if (rows.empty()) return "";
        
        std::vector<ContextMessage> matches;
        for (const auto& row : rows) {
            matches.push_back(rowToMessage(row));
        }
        std::reverse(matches.begin(), matches.end());
        
        std::string result = "[\xe6\x9f\xa5\xe8\xaf\xa2\xe7\xbb\x93\xe6\x9e\x9c: \"" + keyword + "\"] \xe5\x85\xb1" + std::to_string(matches.size()) + "\xe6\x9d\xa1\n";
        for (const auto& msg : matches) {
            result += formatMessage(msg) + "\n";
        }
        return result;
    }
    
    std::string queryBySender(const std::string& context_key, const std::string& sender_name, size_t limit = 10) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& db = Database::instance();
        auto rows = db.query("SELECT * FROM messages WHERE context_key = ? AND sender_name LIKE ? ORDER BY timestamp DESC LIMIT ?",
            {DbValue(context_key), DbValue("%" + sender_name + "%"), DbValue(static_cast<int64_t>(limit))});
        
        if (rows.empty()) return "";
        
        std::vector<ContextMessage> matches;
        for (const auto& row : rows) {
            matches.push_back(rowToMessage(row));
        }
        std::reverse(matches.begin(), matches.end());
        
        std::string result = "[\xe6\x9f\xa5\xe8\xaf\xa2\xe7\xbb\x93\xe6\x9e\x9c: \xe7\x94\xa8\xe6\x88\xb7\"" + sender_name + "\"] \xe5\x85\xb1" + std::to_string(matches.size()) + "\xe6\x9d\xa1\n";
        for (const auto& msg : matches) {
            result += formatMessage(msg) + "\n";
        }
        return result;
    }
    
    std::string queryRecent(const std::string& context_key, size_t limit = 10) {
        auto matches = getContext(context_key, limit);
        if (matches.empty()) return "";
        
        std::string result = "[\xe6\x9c\x80\xe8\xbf\x91" + std::to_string(matches.size()) + "\xe6\x9d\xa1\xe8\xae\xb0\xe5\xbd\x95]\n";
        for (const auto& msg : matches) {
            result += formatMessage(msg) + "\n";
        }
        return result;
    }
    
    std::string getContextStats(const std::string& context_key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& db = Database::instance();
        auto rows = db.query("SELECT * FROM messages WHERE context_key = ?", {DbValue(context_key)});
        
        if (rows.empty()) return "\xe6\x97\xa0\xe8\xae\xb0\xe5\xbd\x95";
        
        std::map<std::string, size_t> sender_counts;
        for (const auto& row : rows) {
            if (row.count("sender_name") && !row.at("sender_name").isNull()) {
                sender_counts[row.at("sender_name").toText()]++;
            }
        }
        
        std::string result = "[\xe7\xbb\x9f\xe8\xae\xa1] \xe5\x85\xb1" + std::to_string(rows.size()) + "\xe6\x9d\xa1\xe8\xae\xb0\xe5\xbd\x95, ";
        result += "\xe6\xb4\xbb\xe8\xb7\x83\xe7\x94\xa8\xe6\x88\xb7" + std::to_string(sender_counts.size()) + "\xe4\xba\xba";
        return result;
    }
    
    std::string queryByTimeRange(const std::string& context_key, int64_t start_time, int64_t end_time, size_t limit = 50) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& db = Database::instance();
        auto rows = db.query("SELECT * FROM messages WHERE context_key = ? AND timestamp >= ? AND timestamp <= ? ORDER BY timestamp LIMIT ?",
            {DbValue(context_key), DbValue(start_time), DbValue(end_time), DbValue(static_cast<int64_t>(limit))});
        
        if (rows.empty()) return "";
        
        std::string result = "[\xe6\x97\xb6\xe9\x97\xb4\xe8\x8c\x83\xe5\x9b\xb4\xe6\x9f\xa5\xe8\xaf\xa2] \xe5\x85\xb1" + std::to_string(rows.size()) + "\xe6\x9d\xa1\n";
        for (const auto& row : rows) {
            result += formatMessage(rowToMessage(row)) + "\n";
        }
        return result;
    }
    
    DbResult queryRaw(const std::string& sql, const std::vector<DbValue>& params = {}) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& db = Database::instance();
        return db.query(sql, params);
    }
    
private:
    ContextDatabase() = default;
    
    ContextMessage rowToMessage(const DbRow& row) {
        ContextMessage msg;
        if (row.count("id")) msg.id = row.at("id").toInt();
        if (row.count("context_key")) msg.context_key = row.at("context_key").toText();
        if (row.count("role")) msg.role = row.at("role").toText();
        if (row.count("content")) msg.content = row.at("content").toText();
        if (row.count("timestamp")) msg.timestamp = row.at("timestamp").toInt();
        if (row.count("sender_name")) msg.sender_name = row.at("sender_name").toText();
        if (row.count("sender_id")) msg.sender_id = row.at("sender_id").toInt();
        return msg;
    }
    
    void compressContext(const std::string& context_key, size_t max_messages) {
        auto& db = Database::instance();
        auto count_rows = db.query("SELECT * FROM messages WHERE context_key = ?", {DbValue(context_key)});
        
        if (count_rows.size() > max_messages) {
            size_t to_remove = count_rows.size() - max_messages;
            db.execute("DELETE FROM messages WHERE context_key = ? ORDER BY timestamp LIMIT ?",
                {DbValue(context_key), DbValue(static_cast<int64_t>(to_remove))});
        }
    }
    
    void migrateOldData() {
        std::string old_path = db_path_ + ".old";
        if (!std::filesystem::exists(old_path)) {
            std::string txt_path = db_path_;
            size_t dot = txt_path.rfind('.');
            if (dot != std::string::npos) {
                txt_path = txt_path.substr(0, dot) + ".txt";
            }
            if (std::filesystem::exists(txt_path)) {
                old_path = txt_path;
            } else {
                return;
            }
        }
        
        LOG_INFO("[ContextDB] Migrating old data from: " + old_path);
        
        std::ifstream file(old_path);
        if (!file.is_open()) return;
        
        auto& db = Database::instance();
        db.beginTransaction();
        
        std::string line;
        int count = 0;
        
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            
            std::istringstream iss(line);
            std::string token;
            
            int64_t id, timestamp, sender_id;
            std::string context_key, role, sender_name, content;
            
            if (!std::getline(iss, token, '\t')) continue;
            id = std::stoll(token);
            
            if (!std::getline(iss, context_key, '\t')) continue;
            if (!std::getline(iss, role, '\t')) continue;
            
            if (!std::getline(iss, token, '\t')) continue;
            timestamp = std::stoll(token);
            
            if (!std::getline(iss, token, '\t')) continue;
            sender_id = std::stoll(token);
            
            if (!std::getline(iss, sender_name, '\t')) continue;
            
            std::getline(iss, content);
            content = unescape(content);
            
            db.execute("INSERT INTO messages (id, context_key, role, content, timestamp, sender_name, sender_id) VALUES (?, ?, ?, ?, ?, ?, ?)",
                {DbValue(id), DbValue(context_key), DbValue(role), DbValue(content), DbValue(timestamp), DbValue(sender_name), DbValue(sender_id)});
            count++;
        }
        
        db.commit();
        
        std::filesystem::rename(old_path, old_path + ".migrated");
        LOG_INFO("[ContextDB] Migrated " + std::to_string(count) + " messages");
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
    mutable std::mutex mutex_;
    bool initialized_ = false;
};

}
