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
        db.execute("CREATE INDEX IF NOT EXISTS idx_ctx_ts ON messages(context_key, timestamp)");
        db.execute("CREATE INDEX IF NOT EXISTS idx_ctx_sender ON messages(context_key, sender_name)");
        
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
        
        LOG_INFO("[ContextDB] buildSmartContextPrompt: key=" + context_key + ", messages=" + std::to_string(all_messages.size()));
        
        if (all_messages.empty()) {
            return "";
        }
        
        size_t max_chars = 15000;
        
        std::string prompt = "[群聊历史记录] (共" + std::to_string(all_messages.size()) + "条)\n";
        
        for (size_t i = 0; i < all_messages.size(); i++) {
            std::string msg_str = formatMessage(all_messages[i]) + "\n";
            if (prompt.length() + msg_str.length() > max_chars) {
                prompt = "[群聊历史记录] (已截断,显示最近部分)\n";
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
        std::string time_prefix;
        if (msg.timestamp > 0) {
            std::time_t t = static_cast<std::time_t>(msg.timestamp);
            std::tm tm_buf;
            localtime_s(&tm_buf, &t);
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%02d-%02d %02d:%02d",
                tm_buf.tm_mon + 1, tm_buf.tm_mday, tm_buf.tm_hour, tm_buf.tm_min);
            time_prefix = "[" + std::string(buf) + "] ";
        }
        if (msg.role == "user") {
            if (!msg.sender_name.empty()) {
                return time_prefix + msg.sender_name + ": " + msg.content;
            }
            return time_prefix + "User: " + msg.content;
        }
        return time_prefix + "Assistant: " + msg.content;
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
        
        if (rows.empty()) return "未找到包含\"" + keyword + "\"的记录";
        
        std::vector<ContextMessage> matches;
        for (const auto& row : rows) {
            matches.push_back(rowToMessage(row));
        }
        std::reverse(matches.begin(), matches.end());
        
        std::string result = "[关键词\"" + keyword + "\"查询结果] 共" + std::to_string(matches.size()) + "条\n";
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
        
        if (rows.empty()) return "未找到用户\"" + sender_name + "\"的记录";
        
        std::vector<ContextMessage> matches;
        for (const auto& row : rows) {
            matches.push_back(rowToMessage(row));
        }
        std::reverse(matches.begin(), matches.end());
        
        std::string result = "[用户\"" + sender_name + "\"的消息] 共" + std::to_string(matches.size()) + "条\n";
        for (const auto& msg : matches) {
            result += formatMessage(msg) + "\n";
        }
        return result;
    }
    
    std::string queryByDate(const std::string& context_key, const std::string& date_str, size_t limit = 50) {
        int year = 0, month = 0, day = 0;
        if (sscanf_s(date_str.c_str(), "%d-%d-%d", &year, &month, &day) != 3) {
            if (sscanf_s(date_str.c_str(), "%d/%d/%d", &year, &month, &day) != 3) {
                if (sscanf_s(date_str.c_str(), "%d月%d日", &month, &day) == 2) {
                    auto now = std::chrono::system_clock::now();
                    std::time_t now_t = std::chrono::system_clock::to_time_t(now);
                    std::tm tm_buf;
                    localtime_s(&tm_buf, &now_t);
                    year = tm_buf.tm_year + 1900;
                } else {
                    return "日期格式错误,支持: YYYY-MM-DD 或 M月D日";
                }
            }
        }
        if (year < 100) year += 2000;
        
        std::tm day_start = {};
        day_start.tm_year = year - 1900;
        day_start.tm_mon = month - 1;
        day_start.tm_mday = day;
        day_start.tm_hour = 0; day_start.tm_min = 0; day_start.tm_sec = 0;
        std::time_t t_start = std::mktime(&day_start);
        std::time_t t_end = t_start + 86400;
        
        std::lock_guard<std::mutex> lock(mutex_);
        auto& db = Database::instance();
        auto rows = db.query(
            "SELECT * FROM messages WHERE context_key = ? AND timestamp >= ? AND timestamp < ? ORDER BY timestamp ASC LIMIT ?",
            {DbValue(context_key), DbValue(static_cast<int64_t>(t_start)), DbValue(static_cast<int64_t>(t_end)), DbValue(static_cast<int64_t>(limit))});
        
        if (rows.empty()) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, month, day);
            return "未找到 " + std::string(buf) + " 的聊天记录";
        }
        
        std::vector<ContextMessage> matches;
        for (const auto& row : rows) {
            matches.push_back(rowToMessage(row));
        }
        
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, month, day);
        std::string result = "[" + std::string(buf) + " 的聊天记录] 共" + std::to_string(matches.size()) + "条\n";
        for (const auto& msg : matches) {
            result += formatMessage(msg) + "\n";
        }
        return result;
    }
    
    std::string queryRecent(const std::string& context_key, size_t limit = 10) {
        auto matches = getContext(context_key, limit);
        if (matches.empty()) return "暂无聊天记录";
        
        std::string result = "[最近" + std::to_string(matches.size()) + "条记录]\n";
        for (const auto& msg : matches) {
            result += formatMessage(msg) + "\n";
        }
        return result;
    }
    
    std::string getContextStats(const std::string& context_key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& db = Database::instance();
        
        auto count_rows = db.query("SELECT COUNT(*) as cnt FROM messages WHERE context_key = ?", {DbValue(context_key)});
        int64_t total = 0;
        if (!count_rows.empty() && count_rows[0].count("cnt")) {
            total = count_rows[0].at("cnt").toInt();
        }
        if (total == 0) return "数据库: 无记录";
        
        auto user_rows = db.query("SELECT COUNT(DISTINCT sender_name) as cnt FROM messages WHERE context_key = ? AND sender_name != ''", {DbValue(context_key)});
        int64_t user_count = 0;
        if (!user_rows.empty() && user_rows[0].count("cnt")) {
            user_count = user_rows[0].at("cnt").toInt();
        }
        
        return "数据库: 共" + std::to_string(total) + "条记录, " + std::to_string(user_count) + "位活跃用户";
    }
    
    std::string queryActiveUsers(const std::string& context_key, size_t limit = 20) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& db = Database::instance();
        auto rows = db.query(
            "SELECT * FROM messages WHERE context_key = ?",
            {DbValue(context_key)});
        
        std::map<std::string, int64_t> user_counts;
        std::map<std::string, int64_t> user_ids;
        for (const auto& row : rows) {
            std::string role = row.count("role") ? row.at("role").toText() : "";
            std::string name = row.count("sender_name") ? row.at("sender_name").toText() : "";
            if (role == "user" && !name.empty()) {
                user_counts[name]++;
                if (row.count("sender_id") && row.at("sender_id").toInt() != 0)
                    user_ids[name] = row.at("sender_id").toInt();
            }
        }
        
        if (user_counts.empty()) return "暂无活跃用户数据";
        
        std::vector<std::pair<std::string, int64_t>> sorted(user_counts.begin(), user_counts.end());
        std::sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
        
        std::string result = "[活跃用户排行] 共" + std::to_string(sorted.size()) + "位\n";
        size_t shown = 0;
        for (const auto& [name, cnt] : sorted) {
            if (shown >= limit) break;
            result += name;
            if (user_ids.count(name)) result += "(QQ:" + std::to_string(user_ids[name]) + ")";
            result += ": " + std::to_string(cnt) + "条消息\n";
            shown++;
        }
        return result;
    }
    
    int64_t findUserIdByName(const std::string& context_key, const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& db = Database::instance();
        auto rows = db.query(
            "SELECT * FROM messages WHERE context_key = ? ORDER BY timestamp DESC",
            {DbValue(context_key)});
        for (const auto& row : rows) {
            std::string sname = row.count("sender_name") ? row.at("sender_name").toText() : "";
            int64_t sid = row.count("sender_id") ? row.at("sender_id").toInt() : 0;
            if (sid != 0 && !sname.empty() && sname.find(name) != std::string::npos)
                return sid;
        }
        return 0;
    }
    
    std::string queryByTimeRange(const std::string& context_key, int64_t start_time, int64_t end_time, size_t limit = 50) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& db = Database::instance();
        auto rows = db.query("SELECT * FROM messages WHERE context_key = ? AND timestamp >= ? AND timestamp <= ? ORDER BY timestamp LIMIT ?",
            {DbValue(context_key), DbValue(start_time), DbValue(end_time), DbValue(static_cast<int64_t>(limit))});
        
        if (rows.empty()) return "该时间范围内无记录";
        
        std::string result = "[时间范围查询] 共" + std::to_string(rows.size()) + "条\n";
        for (const auto& row : rows) {
            result += formatMessage(rowToMessage(row)) + "\n";
        }
        return result;
    }
    
    std::string queryYearSummary(const std::string& context_key, int year = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& db = Database::instance();
        
        if (year == 0) {
            auto now = std::chrono::system_clock::now();
            std::time_t t = std::chrono::system_clock::to_time_t(now);
            std::tm tm_buf;
            localtime_s(&tm_buf, &t);
            year = tm_buf.tm_year + 1900;
        }
        
        std::tm start_tm = {};
        start_tm.tm_year = year - 1900;
        start_tm.tm_mon = 0;
        start_tm.tm_mday = 1;
        int64_t start_ts = static_cast<int64_t>(mktime(&start_tm));
        
        std::tm end_tm = {};
        end_tm.tm_year = year - 1900 + 1;
        end_tm.tm_mon = 0;
        end_tm.tm_mday = 1;
        int64_t end_ts = static_cast<int64_t>(mktime(&end_tm));
        
        auto all_rows = db.query(
            "SELECT * FROM messages WHERE context_key = ? ORDER BY timestamp ASC",
            {DbValue(context_key)});
        
        std::vector<DbRow> year_rows;
        for (const auto& row : all_rows) {
            if (!row.count("timestamp")) continue;
            int64_t ts = row.at("timestamp").toInt();
            if (ts >= start_ts && ts < end_ts)
                year_rows.push_back(row);
        }
        
        if (year_rows.empty())
            return std::to_string(year) + "年无聊天记录";
        
        std::string result = "=== " + std::to_string(year) + "年度总结数据 ===\n";
        result += "总消息数: " + std::to_string(year_rows.size()) + "条\n\n";
        
        std::map<std::string, int64_t> sender_counts;
        std::map<int, int64_t> month_counts;
        std::map<int, int64_t> hour_counts;
        
        for (const auto& row : year_rows) {
            std::string role = row.count("role") ? row.at("role").toText() : "";
            std::string sender = row.count("sender_name") ? row.at("sender_name").toText() : "";
            if (role == "user" && !sender.empty()) {
                sender_counts[sender]++;
            }
            
            int64_t ts = row.at("timestamp").toInt();
            std::time_t t = static_cast<std::time_t>(ts);
            std::tm tm_buf;
            localtime_s(&tm_buf, &t);
            month_counts[tm_buf.tm_mon + 1]++;
            hour_counts[tm_buf.tm_hour]++;
        }
        
        if (!sender_counts.empty()) {
            std::vector<std::pair<std::string, int64_t>> sorted_senders(sender_counts.begin(), sender_counts.end());
            std::sort(sorted_senders.begin(), sorted_senders.end(),
                [](const auto& a, const auto& b) { return a.second > b.second; });
            
            result += "[发言排行榜]\n";
            int rank = 1;
            for (const auto& [name, cnt] : sorted_senders) {
                if (rank > 15) break;
                result += std::to_string(rank++) + ". " + name + ": " + std::to_string(cnt) + "条\n";
            }
            result += "\n";
        }
        
        if (!month_counts.empty()) {
            result += "[月度消息量]\n";
            for (int m = 1; m <= 12; m++) {
                if (month_counts.count(m) && month_counts[m] > 0)
                    result += std::to_string(m) + "月: " + std::to_string(month_counts[m]) + "条\n";
            }
            result += "\n";
        }
        
        if (!hour_counts.empty()) {
            std::vector<std::pair<int, int64_t>> sorted_hours(hour_counts.begin(), hour_counts.end());
            std::sort(sorted_hours.begin(), sorted_hours.end(),
                [](const auto& a, const auto& b) { return a.second > b.second; });
            
            result += "[最活跃时段]\n";
            int shown = 0;
            for (const auto& [hour, cnt] : sorted_hours) {
                if (shown >= 5) break;
                result += std::to_string(hour) + ":00-" + std::to_string(hour + 1) + ":00: " + std::to_string(cnt) + "条\n";
                shown++;
            }
            result += "\n";
        }
        
        auto first_msg = rowToMessage(year_rows.front());
        result += "[第一条消息] " + formatMessage(first_msg) + "\n";
        auto last_msg = rowToMessage(year_rows.back());
        result += "[最新消息] " + formatMessage(last_msg) + "\n";
        
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
        auto count_rows = db.query("SELECT COUNT(*) as cnt FROM messages WHERE context_key = ?", {DbValue(context_key)});
        
        int64_t total = 0;
        if (!count_rows.empty() && count_rows[0].count("cnt")) {
            total = count_rows[0].at("cnt").toInt();
        }
        
        if (static_cast<size_t>(total) > max_messages) {
            int64_t to_remove = total - static_cast<int64_t>(max_messages);
            db.execute("DELETE FROM messages WHERE context_key = ? AND id IN "
                "(SELECT id FROM messages WHERE context_key = ? ORDER BY timestamp ASC LIMIT ?)",
                {DbValue(context_key), DbValue(context_key), DbValue(to_remove)});
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
