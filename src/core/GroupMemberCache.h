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
    
    void setMemberRole(int64_t group_id, int64_t user_id, const std::string& role) {
        std::lock_guard<std::mutex> lock(mutex_);
        roles_[group_id][user_id] = role;
    }
    
    std::string getMemberRole(int64_t group_id, int64_t user_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto git = roles_.find(group_id);
        if (git == roles_.end()) return "";
        auto uit = git->second.find(user_id);
        if (uit == git->second.end()) return "member";
        return uit->second;
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
    
    std::vector<std::pair<int64_t, std::string>> getMembers(int64_t group_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(group_id);
        if (it != cache_.end()) return it->second;
        return {};
    }
    
    bool isMember(int64_t group_id, int64_t user_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(group_id);
        if (it == cache_.end()) return true;
        for (const auto& [uid, nick] : it->second) {
            if (uid == user_id) return true;
        }
        return false;
    }

    std::string getMemberName(int64_t group_id, int64_t user_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(group_id);
        if (it == cache_.end()) return "";
        for (const auto& [uid, nick] : it->second) {
            if (uid == user_id) return nick;
        }
        return "";
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
    
    std::vector<int64_t> getAllGroups() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::set<int64_t> all;
        for (const auto& [gid, _] : cache_) all.insert(gid);
        for (const auto& [gid, _] : group_names_) all.insert(gid);
        return std::vector<int64_t>(all.begin(), all.end());
    }
    
    void addGroupId(int64_t group_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (group_names_.find(group_id) == group_names_.end())
            group_names_[group_id] = "";
    }
    
    void setGroupName(int64_t group_id, const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        group_names_[group_id] = name;
    }
    
    std::string getGroupName(int64_t group_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = group_names_.find(group_id);
        if (it != group_names_.end()) return it->second;
        return "";
    }
    
private:
    std::map<int64_t, std::vector<std::pair<int64_t, std::string>>> cache_;
    std::map<int64_t, std::map<int64_t, std::string>> roles_;
    std::map<int64_t, std::string> group_names_;
    std::set<int64_t> pending_;
    std::mutex mutex_;
};

}
