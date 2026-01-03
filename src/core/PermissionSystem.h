#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <algorithm>
#include "Logger.h"
#include "JsonParser.h"

namespace LCHBOT {

enum class Permission : int {
    NONE = 0,
    USER = 1,
    VIP = 2,
    MODERATOR = 4,
    ADMIN = 8,
    SUPER_ADMIN = 16,
    OWNER = 32
};

inline Permission operator|(Permission a, Permission b) {
    return static_cast<Permission>(static_cast<int>(a) | static_cast<int>(b));
}

inline bool hasPermission(Permission user_perm, Permission required) {
    return static_cast<int>(user_perm) >= static_cast<int>(required);
}

struct UserPermissionData {
    int64_t user_id = 0;
    Permission level = Permission::USER;
    std::set<std::string> allowed_commands;
    std::set<std::string> denied_commands;
    int64_t expires_at = 0;
    std::string note;
    int64_t created_at = 0;
    int64_t updated_at = 0;
};

struct GroupPermissionData {
    int64_t group_id = 0;
    bool ai_enabled = true;
    bool commands_enabled = true;
    std::set<std::string> allowed_commands;
    std::set<std::string> denied_commands;
    int daily_limit = 1000;
    int current_usage = 0;
    int64_t last_reset = 0;
    Permission min_command_level = Permission::USER;
    Permission min_ai_level = Permission::USER;
};

class PermissionSystem {
public:
    static PermissionSystem& instance() {
        static PermissionSystem inst;
        return inst;
    }
    
    bool initialize(const std::string& config_path = "config/permissions.json") {
        std::lock_guard<std::mutex> lock(mutex_);
        config_path_ = config_path;
        
        std::filesystem::path path(config_path);
        std::filesystem::create_directories(path.parent_path());
        
        loadFromFile();
        initialized_ = true;
        LOG_INFO("[Permission] System initialized with " + std::to_string(owners_.size()) + " owners, " + 
                 std::to_string(user_permissions_.size()) + " users");
        return true;
    }
    
    void addOwner(int64_t user_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        owners_.insert(user_id);
        LOG_INFO("[Permission] Added owner: " + std::to_string(user_id));
        saveToFile();
    }
    
    void removeOwner(int64_t user_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        owners_.erase(user_id);
        LOG_INFO("[Permission] Removed owner: " + std::to_string(user_id));
        saveToFile();
    }
    
    void setUserPermission(int64_t user_id, Permission level, const std::string& note = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        auto& perm = user_permissions_[user_id];
        if (perm.user_id == 0) {
            perm.user_id = user_id;
            perm.created_at = now;
        }
        perm.level = level;
        perm.note = note;
        perm.updated_at = now;
        
        LOG_INFO("[Permission] Set user " + std::to_string(user_id) + " level: " + std::to_string(static_cast<int>(level)));
        saveToFile();
    }
    
    void removeUserPermission(int64_t user_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        user_permissions_.erase(user_id);
        LOG_INFO("[Permission] Removed user permission: " + std::to_string(user_id));
        saveToFile();
    }
    
    void setUserCommandAccess(int64_t user_id, const std::string& command, bool allowed) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& perm = user_permissions_[user_id];
        perm.user_id = user_id;
        if (allowed) {
            perm.denied_commands.erase(command);
            perm.allowed_commands.insert(command);
        } else {
            perm.allowed_commands.erase(command);
            perm.denied_commands.insert(command);
        }
        saveToFile();
    }
    
    bool isOwner(int64_t user_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return owners_.count(user_id) > 0;
    }
    
    bool isAdmin(int64_t user_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (owners_.count(user_id) > 0) return true;
        auto it = user_permissions_.find(user_id);
        if (it != user_permissions_.end()) {
            return hasPermission(it->second.level, Permission::ADMIN);
        }
        return false;
    }
    
    bool isModerator(int64_t user_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (owners_.count(user_id) > 0) return true;
        auto it = user_permissions_.find(user_id);
        if (it != user_permissions_.end()) {
            return hasPermission(it->second.level, Permission::MODERATOR);
        }
        return false;
    }
    
    Permission getUserPermission(int64_t user_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (owners_.count(user_id) > 0) return Permission::OWNER;
        auto it = user_permissions_.find(user_id);
        if (it != user_permissions_.end()) {
            if (it->second.expires_at > 0) {
                auto now = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                if (now > it->second.expires_at) {
                    return Permission::USER;
                }
            }
            return it->second.level;
        }
        return Permission::USER;
    }
    
    std::string getPermissionName(Permission level) const {
        switch (level) {
            case Permission::OWNER: return "Owner";
            case Permission::SUPER_ADMIN: return "SuperAdmin";
            case Permission::ADMIN: return "Admin";
            case Permission::MODERATOR: return "Moderator";
            case Permission::VIP: return "VIP";
            case Permission::USER: return "User";
            default: return "None";
        }
    }
    
    bool canExecuteCommand(int64_t user_id, int64_t group_id, const std::string& command, 
                           Permission required_level = Permission::USER) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (owners_.count(user_id) > 0) return true;
        
        Permission user_level = Permission::USER;
        auto uit = user_permissions_.find(user_id);
        if (uit != user_permissions_.end()) {
            user_level = uit->second.level;
            if (uit->second.denied_commands.count(command) > 0) return false;
        }
        
        if (!hasPermission(user_level, required_level)) return false;
        
        if (group_id > 0) {
            auto git = group_permissions_.find(group_id);
            if (git != group_permissions_.end()) {
                if (!git->second.commands_enabled) return false;
                if (!hasPermission(user_level, git->second.min_command_level)) return false;
                if (git->second.denied_commands.count(command) > 0) return false;
                if (!git->second.allowed_commands.empty() && 
                    git->second.allowed_commands.count(command) == 0) return false;
            }
        }
        
        return true;
    }
    
    bool canUseAI(int64_t user_id, int64_t group_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (owners_.count(user_id) > 0) return true;
        
        Permission user_level = Permission::USER;
        auto uit = user_permissions_.find(user_id);
        if (uit != user_permissions_.end()) {
            user_level = uit->second.level;
        }
        
        if (group_id > 0) {
            auto git = group_permissions_.find(group_id);
            if (git != group_permissions_.end()) {
                if (!git->second.ai_enabled) return false;
                if (!hasPermission(user_level, git->second.min_ai_level)) return false;
            }
        }
        
        return true;
    }
    
    bool checkGroupDailyLimit(int64_t group_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& gp = group_permissions_[group_id];
        if (gp.group_id == 0) {
            gp.group_id = group_id;
            gp.daily_limit = 1000;
        }
        
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        int64_t day_start = (now / 86400) * 86400;
        
        if (gp.last_reset < day_start) {
            gp.current_usage = 0;
            gp.last_reset = now;
        }
        
        if (gp.current_usage >= gp.daily_limit) {
            return false;
        }
        gp.current_usage++;
        return true;
    }
    
    void setGroupConfig(int64_t group_id, bool ai_enabled, bool commands_enabled, int daily_limit) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& gp = group_permissions_[group_id];
        gp.group_id = group_id;
        gp.ai_enabled = ai_enabled;
        gp.commands_enabled = commands_enabled;
        gp.daily_limit = daily_limit;
        saveToFile();
    }
    
    void setGroupMinLevel(int64_t group_id, Permission min_command, Permission min_ai) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& gp = group_permissions_[group_id];
        gp.group_id = group_id;
        gp.min_command_level = min_command;
        gp.min_ai_level = min_ai;
        saveToFile();
    }
    
    GroupPermissionData getGroupPermission(int64_t group_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = group_permissions_.find(group_id);
        if (it != group_permissions_.end()) {
            return it->second;
        }
        GroupPermissionData gp;
        gp.group_id = group_id;
        return gp;
    }
    
    std::vector<int64_t> getOwners() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::vector<int64_t>(owners_.begin(), owners_.end());
    }
    
    std::vector<std::pair<int64_t, Permission>> getAdmins() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::pair<int64_t, Permission>> result;
        for (const auto& [id, perm] : user_permissions_) {
            if (hasPermission(perm.level, Permission::ADMIN)) {
                result.emplace_back(id, perm.level);
            }
        }
        return result;
    }
    
    std::string exportStats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::stringstream ss;
        ss << "Owners: " << owners_.size() << "\n";
        ss << "Users with permissions: " << user_permissions_.size() << "\n";
        ss << "Groups configured: " << group_permissions_.size() << "\n";
        
        int admins = 0, mods = 0, vips = 0;
        for (const auto& [id, perm] : user_permissions_) {
            if (hasPermission(perm.level, Permission::ADMIN)) admins++;
            else if (hasPermission(perm.level, Permission::MODERATOR)) mods++;
            else if (hasPermission(perm.level, Permission::VIP)) vips++;
        }
        ss << "Admins: " << admins << ", Moderators: " << mods << ", VIPs: " << vips;
        return ss.str();
    }

private:
    PermissionSystem() = default;
    
    void loadFromFile() {
        std::ifstream file(config_path_);
        if (!file.is_open()) {
            owners_.insert(2643518036);
            return;
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        
        size_t owners_pos = content.find("\"owners\"");
        if (owners_pos != std::string::npos) {
            size_t arr_start = content.find("[", owners_pos);
            size_t arr_end = content.find("]", arr_start);
            if (arr_start != std::string::npos && arr_end != std::string::npos) {
                std::string arr = content.substr(arr_start + 1, arr_end - arr_start - 1);
                std::istringstream iss(arr);
                std::string token;
                while (std::getline(iss, token, ',')) {
                    token.erase(std::remove_if(token.begin(), token.end(), ::isspace), token.end());
                    if (!token.empty()) {
                        owners_.insert(std::stoll(token));
                    }
                }
            }
        }
        
        size_t users_pos = content.find("\"users\"");
        if (users_pos != std::string::npos) {
            size_t obj_start = content.find("{", users_pos);
            if (obj_start != std::string::npos) {
                int brace_count = 1;
                size_t pos = obj_start + 1;
                while (pos < content.size() && brace_count > 0) {
                    if (content[pos] == '{') brace_count++;
                    else if (content[pos] == '}') brace_count--;
                    pos++;
                }
                std::string users_obj = content.substr(obj_start, pos - obj_start);
                parseUsers(users_obj);
            }
        }
        
        if (owners_.empty()) {
            owners_.insert(2643518036);
        }
    }
    
    void parseUsers(const std::string& obj) {
        size_t pos = 0;
        while ((pos = obj.find("\"", pos)) != std::string::npos) {
            size_t key_end = obj.find("\"", pos + 1);
            if (key_end == std::string::npos) break;
            
            std::string key = obj.substr(pos + 1, key_end - pos - 1);
            if (key.empty() || !std::all_of(key.begin(), key.end(), ::isdigit)) {
                pos = key_end + 1;
                continue;
            }
            
            int64_t user_id = std::stoll(key);
            
            size_t val_start = obj.find("{", key_end);
            if (val_start == std::string::npos) break;
            
            size_t val_end = obj.find("}", val_start);
            if (val_end == std::string::npos) break;
            
            std::string val = obj.substr(val_start, val_end - val_start + 1);
            
            UserPermissionData perm;
            perm.user_id = user_id;
            
            size_t level_pos = val.find("\"level\"");
            if (level_pos != std::string::npos) {
                size_t colon = val.find(":", level_pos);
                size_t comma = val.find_first_of(",}", colon);
                std::string level_str = val.substr(colon + 1, comma - colon - 1);
                level_str.erase(std::remove_if(level_str.begin(), level_str.end(), ::isspace), level_str.end());
                perm.level = static_cast<Permission>(std::stoi(level_str));
            }
            
            user_permissions_[user_id] = perm;
            pos = val_end + 1;
        }
    }
    
    void saveToFile() {
        std::ofstream file(config_path_);
        if (!file.is_open()) return;
        
        file << "{\n";
        file << "  \"owners\": [";
        bool first = true;
        for (int64_t id : owners_) {
            if (!first) file << ", ";
            file << id;
            first = false;
        }
        file << "],\n";
        
        file << "  \"users\": {\n";
        first = true;
        for (const auto& [id, perm] : user_permissions_) {
            if (!first) file << ",\n";
            file << "    \"" << id << "\": {\"level\": " << static_cast<int>(perm.level);
            if (!perm.note.empty()) file << ", \"note\": \"" << perm.note << "\"";
            if (perm.expires_at > 0) file << ", \"expires\": " << perm.expires_at;
            file << "}";
            first = false;
        }
        file << "\n  },\n";
        
        file << "  \"groups\": {\n";
        first = true;
        for (const auto& [id, gp] : group_permissions_) {
            if (!first) file << ",\n";
            file << "    \"" << id << "\": {"
                 << "\"ai_enabled\": " << (gp.ai_enabled ? "true" : "false")
                 << ", \"commands_enabled\": " << (gp.commands_enabled ? "true" : "false")
                 << ", \"daily_limit\": " << gp.daily_limit
                 << "}";
            first = false;
        }
        file << "\n  }\n";
        file << "}\n";
    }
    
    std::string config_path_;
    std::set<int64_t> owners_;
    std::map<int64_t, UserPermissionData> user_permissions_;
    std::map<int64_t, GroupPermissionData> group_permissions_;
    mutable std::mutex mutex_;
    bool initialized_ = false;
};

}
