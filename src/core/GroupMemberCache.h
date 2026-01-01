#pragma once

#include <string>
#include <map>
#include <set>
#include <vector>
#include <mutex>

namespace LCHBOT {

struct GroupMemberCache {
    static GroupMemberCache& instance() {
        static GroupMemberCache inst;
        return inst;
    }
    
    void setMembers(int64_t group_id, const std::vector<std::pair<int64_t, std::string>>& members) {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_[group_id] = members;
    }
    
    bool hasGroup(int64_t group_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        return cache_.find(group_id) != cache_.end() && !cache_[group_id].empty();
    }
    
    bool isPending(int64_t group_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        return pending_.count(group_id) > 0;
    }
    
    void markPending(int64_t group_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.insert(group_id);
    }
    
    std::string toJson() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string result = "{";
        bool first_group = true;
        for (const auto& [gid, members] : cache_) {
            if (!first_group) result += ",";
            first_group = false;
            result += "\"" + std::to_string(gid) + "\":{";
            bool first_member = true;
            for (const auto& [uid, nick] : members) {
                if (!first_member) result += ",";
                first_member = false;
                std::string escaped_nick;
                for (unsigned char c : nick) {
                    if (c == '"') escaped_nick += "\\\"";
                    else if (c == '\\') escaped_nick += "\\\\";
                    else if (c == '\n') escaped_nick += "\\n";
                    else if (c == '\r') escaped_nick += "\\r";
                    else if (c == '\t') escaped_nick += "\\t";
                    else if (c < 0x20) {
                        char buf[8];
                        snprintf(buf, sizeof(buf), "\\u%04x", c);
                        escaped_nick += buf;
                    }
                    else escaped_nick += c;
                }
                result += "\"" + std::to_string(uid) + "\":\"" + escaped_nick + "\"";
            }
            result += "}";
        }
        result += "}";
        return result;
    }
    
private:
    std::map<int64_t, std::vector<std::pair<int64_t, std::string>>> cache_;
    std::set<int64_t> pending_;
    std::mutex mutex_;
};

}
