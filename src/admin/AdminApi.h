#pragma once

#include <string>
#include <sstream>
#include "AdminServer.h"
#include "Statistics.h"
#include "../plugin/PluginManager.h"
#include "../ai/PersonalitySystem.h"
#include "../core/Logger.h"

namespace LCHBOT {

class AdminApi {
public:
    static AdminApi& instance() {
        static AdminApi inst;
        return inst;
    }
    
    void initialize() {
        auto& server = AdminServer::instance();
        
        server.registerHandler("/api/stats", [this](const std::string& method, const std::string& path, const std::string& body) {
            return handleStats(method, path, body);
        });
        
        server.registerHandler("/api/plugins", [this](const std::string& method, const std::string& path, const std::string& body) {
            return handlePlugins(method, path, body);
        });
        
        server.registerHandler("/api/personalities", [this](const std::string& method, const std::string& path, const std::string& body) {
            return handlePersonalities(method, path, body);
        });
        
        server.registerHandler("/api/groups", [this](const std::string& method, const std::string& path, const std::string& body) {
            return handleGroups(method, path, body);
        });
        
        server.registerHandler("/api/reload", [this](const std::string& method, const std::string& path, const std::string& body) {
            return handleReload(method, path, body);
        });
        
        LOG_INFO("[AdminApi] API handlers registered");
    }
    
private:
    AdminApi() = default;
    
    std::string handleStats(const std::string& method, const std::string& path, const std::string& body) {
        auto& stats = Statistics::instance();
        auto& plugins = PluginManager::instance();
        auto& personalities = PersonalitySystem::instance();
        
        std::ostringstream json;
        json << "{";
        json << "\"total_calls\":" << stats.getTotalApiCalls() << ",";
        json << "\"active_groups\":" << stats.getActiveGroupCount() << ",";
        json << "\"total_plugins\":" << plugins.getPluginList().size() << ",";
        json << "\"total_personalities\":" << personalities.listPersonalities().size();
        json << "}";
        return json.str();
    }
    
    std::string handlePlugins(const std::string& method, const std::string& path, const std::string& body) {
        auto& mgr = PluginManager::instance();
        
        if (path.find("/enable") != std::string::npos) {
            std::string name = extractPluginName(path);
            if (!name.empty() && method == "POST") {
                mgr.enablePlugin(name);
                LOG_INFO("[Admin] Plugin enabled: " + name);
                return "{\"success\":true}";
            }
        }
        
        if (path.find("/disable") != std::string::npos) {
            std::string name = extractPluginName(path);
            if (!name.empty() && method == "POST") {
                mgr.disablePlugin(name);
                LOG_INFO("[Admin] Plugin disabled: " + name);
                return "{\"success\":true}";
            }
        }
        
        if (path.find("/reload") != std::string::npos) {
            if (method == "POST") {
                mgr.reloadPythonPlugins();
                LOG_INFO("[Admin] Plugins reloaded");
                return "{\"success\":true,\"message\":\"Plugins reloaded\"}";
            }
        }
        
        auto list = mgr.getPluginList();
        std::ostringstream json;
        json << "{\"plugins\":[";
        bool first = true;
        for (const auto& info : list) {
            if (!first) json << ",";
            first = false;
            json << "{";
            json << "\"name\":\"" << escapeJson(info.name) << "\",";
            json << "\"version\":\"" << escapeJson(info.version) << "\",";
            json << "\"author\":\"" << escapeJson(info.author) << "\",";
            json << "\"description\":\"" << escapeJson(info.description) << "\",";
            json << "\"icon\":\"" << escapeJson(info.icon) << "\",";
            json << "\"enabled\":" << (mgr.isPluginEnabled(info.name) ? "true" : "false");
            json << "}";
        }
        json << "]}";
        return json.str();
    }
    
    std::string handlePersonalities(const std::string& method, const std::string& path, const std::string& body) {
        auto& ps = PersonalitySystem::instance();
        auto list = ps.listPersonalities();
        
        std::ostringstream json;
        json << "{\"personalities\":[";
        bool first = true;
        for (const auto& [id, name] : list) {
            if (!first) json << ",";
            first = false;
            json << "{";
            json << "\"id\":\"" << escapeJson(id) << "\",";
            json << "\"name\":\"" << escapeJson(name) << "\"";
            json << "}";
        }
        json << "]}";
        return json.str();
    }
    
    std::string handleGroups(const std::string& method, const std::string& path, const std::string& body) {
        auto& stats = Statistics::instance();
        auto& ps = PersonalitySystem::instance();
        auto group_stats = stats.getGroupStats();
        
        std::ostringstream json;
        json << "{\"groups\":[";
        bool first = true;
        for (const auto& [id, gs] : group_stats) {
            if (!first) json << ",";
            first = false;
            std::string personality_name = ps.getNameForGroup(id);
            json << "{";
            json << "\"id\":" << id << ",";
            json << "\"personality\":\"" << escapeJson(personality_name) << "\",";
            json << "\"calls\":" << gs.call_count.load();
            json << "}";
        }
        json << "]}";
        return json.str();
    }
    
    std::string handleReload(const std::string& method, const std::string& path, const std::string& body) {
        if (method == "POST") {
            auto& mgr = PluginManager::instance();
            mgr.reloadPythonPlugins();
            
            auto& ps = PersonalitySystem::instance();
            ps.reload();
            
            LOG_INFO("[Admin] System reloaded");
            return "{\"success\":true,\"message\":\"System reloaded\"}";
        }
        return "{\"error\":\"Method not allowed\"}";
    }
    
    std::string extractPluginName(const std::string& path) {
        size_t start = path.find("/plugins/");
        if (start == std::string::npos) return "";
        start += 9;
        size_t end = path.find('/', start);
        if (end == std::string::npos) {
            end = path.length();
        }
        return path.substr(start, end - start);
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
                default: result += c;
            }
        }
        return result;
    }
};

}
