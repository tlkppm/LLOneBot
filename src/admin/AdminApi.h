#pragma once

#include <string>
#include <sstream>
#include <map>
#include <vector>
#include "AdminServer.h"
#include "Statistics.h"
#include "../plugin/PluginManager.h"
#include "../ai/PersonalitySystem.h"
#include "../core/Config.h"
#include "../core/Logger.h"
#include "../core/PermissionSystem.h"
#include "../core/RateLimiter.h"
#include "../core/MetricsExporter.h"
#include "../core/TraceSystem.h"
#include "../core/ResponseCache.h"
#include "../core/PluginSandbox.h"

namespace LCHBOT {

class AdminApi {
public:
    using HttpResponse = AdminServer::HttpResponse;

    static AdminApi& instance() {
        static AdminApi inst;
        return inst;
    }
    
    void initialize() {
        auto& server = AdminServer::instance();
        
        server.registerHandler("/api/stats", [this](const std::string& method, const std::string& path, const std::string& body) {
            return handleStats(method, path, body);
        });

        server.registerHandler("/api/auth", [this](const std::string& method, const std::string& path, const std::string& body) {
            return handleAuth(method, path, body);
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
        
        server.registerHandler("/api/metrics", [this](const std::string& method, const std::string& path, const std::string& body) {
            return handleMetrics(method, path, body);
        });
        
        server.registerHandler("/api/permissions", [this](const std::string& method, const std::string& path, const std::string& body) {
            return handlePermissions(method, path, body);
        });
        
        server.registerHandler("/api/traces", [this](const std::string& method, const std::string& path, const std::string& body) {
            return handleTraces(method, path, body);
        });
        
        server.registerHandler("/api/cache", [this](const std::string& method, const std::string& path, const std::string& body) {
            return handleCache(method, path, body);
        });
        
        server.registerHandler("/api/sandbox", [this](const std::string& method, const std::string& path, const std::string& body) {
            return handleSandbox(method, path, body);
        });
        
        server.registerHandler("/metrics", [this](const std::string& method, const std::string& path, const std::string& body) {
            if (!isReadMethod(method)) {
                return methodNotAllowed();
            }
            return plainTextResponse(MetricsExporter::instance().exportPrometheus(), "text/plain; version=0.0.4; charset=utf-8");
        });
        
        LOG_INFO("[AdminApi] API handlers registered (with enterprise features)");
    }
    
private:
    AdminApi() = default;

    struct PersonaReloadChange {
        std::string change_type;
        std::string file;
        std::string persona_id;
        std::string name;
        std::string previous_status;
        std::string current_status;
        bool previously_loaded = false;
        bool currently_loaded = false;
    };

    struct PersonaReloadDiff {
        int added_count = 0;
        int removed_count = 0;
        int changed_count = 0;
        int unchanged_count = 0;
        int previous_loaded_files = 0;
        int current_loaded_files = 0;
        std::vector<PersonaReloadChange> changes;
    };

    HttpResponse handleAuth(const std::string& method, const std::string& path, const std::string& body) {
        if (!isReadMethod(method)) {
            return methodNotAllowed();
        }

        const auto& config = ConfigManager::instance().config();
        bool token_configured = !config.admin_token.empty();
        bool public_readonly = config.admin_public_readonly;
        bool read_requires_token = token_configured && !public_readonly;
        bool write_requires_token = token_configured;

        std::ostringstream json;
        json << "{";
        json << "\"token_configured\":" << (token_configured ? "true" : "false") << ",";
        json << "\"public_readonly\":" << (public_readonly ? "true" : "false") << ",";
        json << "\"read_requires_token\":" << (read_requires_token ? "true" : "false") << ",";
        json << "\"write_requires_token\":" << (write_requires_token ? "true" : "false");
        json << "}";
        return jsonResponse(json.str());
    }
    
    HttpResponse handleStats(const std::string& method, const std::string& path, const std::string& body) {
        if (!isReadMethod(method)) {
            return methodNotAllowed();
        }

        auto& stats = Statistics::instance();
        auto& plugins = PluginManager::instance();
        auto& personalities = PersonalitySystem::instance();
        
        std::ostringstream json;
        json << "{";
        json << "\"total_calls\":" << stats.getTotalApiCalls() << ",";
        json << "\"active_groups\":" << stats.getActiveGroupCount() << ",";
        json << "\"total_plugins\":" << plugins.getPluginList().size() << ",";
        json << "\"total_persona_files\":" << personalities.listPersonalities().size();
        json << "}";
        return jsonResponse(json.str());
    }
    
    HttpResponse handlePlugins(const std::string& method, const std::string& path, const std::string& body) {
        auto& mgr = PluginManager::instance();
        
        if (path.find("/enable") != std::string::npos) {
            if (method != "POST") {
                return methodNotAllowed();
            }
            std::string name = extractPluginName(path);
            if (!name.empty()) {
                mgr.enablePlugin(name);
                LOG_INFO("[Admin] Plugin enabled: " + name);
                return jsonResponse("{\"success\":true}");
            }
            return notFound();
        }
        
        if (path.find("/disable") != std::string::npos) {
            if (method != "POST") {
                return methodNotAllowed();
            }
            std::string name = extractPluginName(path);
            if (!name.empty()) {
                mgr.disablePlugin(name);
                LOG_INFO("[Admin] Plugin disabled: " + name);
                return jsonResponse("{\"success\":true}");
            }
            return notFound();
        }
        
        if (path.find("/reload") != std::string::npos) {
            if (method != "POST") {
                return methodNotAllowed();
            }
            mgr.reloadPythonPlugins();
            LOG_INFO("[Admin] Plugins reloaded");
            return jsonResponse("{\"success\":true,\"message\":\"Plugins reloaded\"}");
        }

        if (!isReadMethod(method)) {
            return methodNotAllowed();
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
        return jsonResponse(json.str());
    }
    
    HttpResponse handlePersonalities(const std::string& method, const std::string& path, const std::string& body) {
        if (!isReadMethod(method)) {
            return methodNotAllowed();
        }

        auto& ps = PersonalitySystem::instance();
        auto report = ps.getPersonaLoadReport();
        
        std::ostringstream json;
        json << "{";
        json << "\"persona_directory\":\"" << escapeJson(report.directory.empty() ? ps.getPersonaDirectoryPath() : report.directory) << "\",";
        json << "\"persona_pattern\":\"" << escapeJson(ps.getPersonaFilePattern()) << "\",";
        json << "\"validation\":{";
        json << "\"discovered_files\":" << report.discovered_files << ",";
        json << "\"loaded_files\":" << report.loaded_files << ",";
        json << "\"errors\":" << report.error_count << ",";
        json << "\"warnings\":" << report.warning_count;
        json << "},";
        json << "\"persona_files\":[";
        bool first = true;
        for (const auto& file : report.files) {
            if (!first) json << ",";
            first = false;
            json << "{";
            std::string status = "ok";
            if (file.error_count > 0) {
                status = "error";
            } else if (file.warning_count > 0) {
                status = "warning";
            }
            json << "\"id\":\"" << escapeJson(file.persona_id) << "\",";
            json << "\"name\":\"" << escapeJson(file.name) << "\",";
            json << "\"file\":\"" << escapeJson(file.relative_path) << "\",";
            json << "\"loaded\":" << (file.loaded ? "true" : "false") << ",";
            json << "\"status\":\"" << status << "\",";
            json << "\"errors\":" << file.error_count << ",";
            json << "\"warnings\":" << file.warning_count;
            json << "}";
        }
        json << "],";
        json << "\"issues\":[";
        first = true;
        for (const auto& issue : report.issues) {
            if (!first) json << ",";
            first = false;
            json << "{";
            json << "\"file\":\"" << escapeJson(issue.relative_path) << "\",";
            json << "\"file_name\":\"" << escapeJson(issue.file_name) << "\",";
            json << "\"persona_id\":\"" << escapeJson(issue.persona_id) << "\",";
            json << "\"severity\":\"" << escapeJson(issue.severity) << "\",";
            json << "\"code\":\"" << escapeJson(issue.code) << "\",";
            json << "\"message\":\"" << escapeJson(issue.message) << "\"";
            json << "}";
        }
        json << "]}";
        return jsonResponse(json.str());
    }
    
    HttpResponse handleGroups(const std::string& method, const std::string& path, const std::string& body) {
        if (!isReadMethod(method)) {
            return methodNotAllowed();
        }

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
        return jsonResponse(json.str());
    }
    
    HttpResponse handleReload(const std::string& method, const std::string& path, const std::string& body) {
        if (method != "POST") {
            return methodNotAllowed();
        }

        auto& mgr = PluginManager::instance();
        mgr.reloadPythonPlugins();
        
        auto& ps = PersonalitySystem::instance();
        auto previous_report = ps.getPersonaLoadReport();
        ps.reload();
        auto current_report = ps.getPersonaLoadReport();
        auto persona_diff = buildPersonaReloadDiff(previous_report, current_report);
        
        LOG_INFO("[Admin] Plugins and persona directory reloaded");
        std::ostringstream json;
        json << "{";
        json << "\"success\":true,";
        json << "\"message\":\"Plugins and persona directory reloaded\",";
        json << "\"persona_diff\":{";
        json << "\"added\":" << persona_diff.added_count << ",";
        json << "\"removed\":" << persona_diff.removed_count << ",";
        json << "\"changed\":" << persona_diff.changed_count << ",";
        json << "\"unchanged\":" << persona_diff.unchanged_count << ",";
        json << "\"previous_loaded_files\":" << persona_diff.previous_loaded_files << ",";
        json << "\"current_loaded_files\":" << persona_diff.current_loaded_files << ",";
        json << "\"has_changes\":" << (!persona_diff.changes.empty() ? "true" : "false") << ",";
        json << "\"changes\":[";
        bool first = true;
        for (const auto& change : persona_diff.changes) {
            if (!first) json << ",";
            first = false;
            json << "{";
            json << "\"change_type\":\"" << escapeJson(change.change_type) << "\",";
            json << "\"file\":\"" << escapeJson(change.file) << "\",";
            json << "\"persona_id\":\"" << escapeJson(change.persona_id) << "\",";
            json << "\"name\":\"" << escapeJson(change.name) << "\",";
            json << "\"previous_status\":\"" << escapeJson(change.previous_status) << "\",";
            json << "\"current_status\":\"" << escapeJson(change.current_status) << "\",";
            json << "\"previously_loaded\":" << (change.previously_loaded ? "true" : "false") << ",";
            json << "\"currently_loaded\":" << (change.currently_loaded ? "true" : "false");
            json << "}";
        }
        json << "]";
        json << "}}";
        return jsonResponse(json.str());
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

    PersonaReloadDiff buildPersonaReloadDiff(const PersonaLoadReport& previous, const PersonaLoadReport& current) const {
        PersonaReloadDiff diff;
        diff.previous_loaded_files = previous.loaded_files;
        diff.current_loaded_files = current.loaded_files;

        std::map<std::string, PersonaFileReport> previous_files;
        std::map<std::string, PersonaFileReport> current_files;

        for (const auto& file : previous.files) {
            previous_files[buildPersonaFileKey(file)] = file;
        }
        for (const auto& file : current.files) {
            current_files[buildPersonaFileKey(file)] = file;
        }

        for (const auto& [key, current_file] : current_files) {
            auto previous_it = previous_files.find(key);
            if (previous_it == previous_files.end()) {
                diff.added_count++;
                diff.changes.push_back({
                    "added",
                    current_file.relative_path,
                    current_file.persona_id,
                    current_file.name,
                    "",
                    getPersonaFileStatus(current_file),
                    false,
                    current_file.loaded
                });
                continue;
            }

            if (hasPersonaFileChanged(previous_it->second, current_file)) {
                diff.changed_count++;
                diff.changes.push_back({
                    "changed",
                    current_file.relative_path,
                    current_file.persona_id,
                    current_file.name,
                    getPersonaFileStatus(previous_it->second),
                    getPersonaFileStatus(current_file),
                    previous_it->second.loaded,
                    current_file.loaded
                });
                continue;
            }

            diff.unchanged_count++;
        }

        for (const auto& [key, previous_file] : previous_files) {
            if (current_files.find(key) != current_files.end()) {
                continue;
            }

            diff.removed_count++;
            diff.changes.push_back({
                "removed",
                previous_file.relative_path,
                previous_file.persona_id,
                previous_file.name,
                getPersonaFileStatus(previous_file),
                "",
                previous_file.loaded,
                false
            });
        }

        return diff;
    }

    std::string buildPersonaFileKey(const PersonaFileReport& file) const {
        if (!file.relative_path.empty()) {
            return file.relative_path;
        }
        if (!file.file_name.empty()) {
            return file.file_name;
        }
        if (!file.persona_id.empty()) {
            return file.persona_id;
        }
        return file.name;
    }

    std::string getPersonaFileStatus(const PersonaFileReport& file) const {
        if (file.error_count > 0) {
            return "error";
        }
        if (file.warning_count > 0) {
            return "warning";
        }
        return "ok";
    }

    bool hasPersonaFileChanged(const PersonaFileReport& previous, const PersonaFileReport& current) const {
        return previous.persona_id != current.persona_id ||
            previous.name != current.name ||
            previous.loaded != current.loaded ||
            previous.error_count != current.error_count ||
            previous.warning_count != current.warning_count ||
            previous.content_signature != current.content_signature;
    }
    
    HttpResponse handleMetrics(const std::string& method, const std::string& path, const std::string& body) {
        if (!isReadMethod(method)) {
            return methodNotAllowed();
        }

        auto& metrics = MetricsExporter::instance();
        auto& cache = ResponseCache::instance();
        auto& trace = TraceSystem::instance();
        
        std::ostringstream json;
        json << "{";
        json << "\"cache\":{";
        auto cache_stats = cache.getStats();
        json << "\"hits\":" << cache_stats.hits << ",";
        json << "\"misses\":" << cache_stats.misses << ",";
        json << "\"hit_rate\":" << cache.getHitRate() << ",";
        json << "\"size_bytes\":" << cache_stats.total_bytes << ",";
        json << "\"entries\":" << cache_stats.entry_count;
        json << "},";
        
        auto trace_stats = trace.getStats();
        json << "\"trace\":{";
        json << "\"total_spans\":" << trace_stats.total_spans << ",";
        json << "\"avg_duration_ms\":" << trace_stats.avg_duration_ms << ",";
        json << "\"errors\":" << trace_stats.errors;
        json << "}";
        json << "}";
        return jsonResponse(json.str());
    }
    
    HttpResponse handlePermissions(const std::string& method, const std::string& path, const std::string& body) {
        auto& perms = PermissionSystem::instance();
        
        if (path.find("/add") != std::string::npos) {
            if (method != "POST") {
                return methodNotAllowed();
            }
            return jsonResponse("{\"error\":\"Not implemented\"}", 501);
        }

        if (!isReadMethod(method)) {
            return methodNotAllowed();
        }
        
        std::ostringstream json;
        json << "{";
        json << "\"owners\":[";
        auto owners = perms.getOwners();
        for (size_t i = 0; i < owners.size(); i++) {
            if (i > 0) json << ",";
            json << owners[i];
        }
        json << "],";
        
        json << "\"admins\":[";
        auto admins = perms.getAdmins();
        for (size_t i = 0; i < admins.size(); i++) {
            if (i > 0) json << ",";
            json << "{\"id\":" << admins[i].first << ",\"level\":" << static_cast<int>(admins[i].second) << "}";
        }
        json << "],";
        json << "\"stats\":\"" << escapeJson(perms.exportStats()) << "\"";
        json << "}";
        return jsonResponse(json.str());
    }
    
    HttpResponse handleTraces(const std::string& method, const std::string& path, const std::string& body) {
        if (!isReadMethod(method)) {
            return methodNotAllowed();
        }

        auto& trace = TraceSystem::instance();
        
        if (path.find("/jaeger") != std::string::npos) {
            return jsonResponse(trace.exportJaegerFormat());
        }
        
        auto spans = trace.getRecentSpans(50);
        std::ostringstream json;
        json << "{\"spans\":[";
        for (size_t i = 0; i < spans.size(); i++) {
            if (i > 0) json << ",";
            json << trace.formatSpanJson(spans[i]);
        }
        json << "]}";
        return jsonResponse(json.str());
    }
    
    HttpResponse handleCache(const std::string& method, const std::string& path, const std::string& body) {
        auto& cache = ResponseCache::instance();
        
        if (path.find("/clear") != std::string::npos) {
            if (method != "POST") {
                return methodNotAllowed();
            }
            cache.clear();
            return jsonResponse("{\"success\":true,\"message\":\"Cache cleared\"}");
        }

        if (!isReadMethod(method)) {
            return methodNotAllowed();
        }
        
        auto stats = cache.getStats();
        std::ostringstream json;
        json << "{";
        json << "\"hits\":" << stats.hits << ",";
        json << "\"misses\":" << stats.misses << ",";
        json << "\"evictions\":" << stats.evictions << ",";
        json << "\"hit_rate\":" << cache.getHitRate() << ",";
        json << "\"size_bytes\":" << stats.total_bytes << ",";
        json << "\"entries\":" << stats.entry_count;
        json << "}";
        return jsonResponse(json.str());
    }
    
    HttpResponse handleSandbox(const std::string& method, const std::string& path, const std::string& body) {
        if (!isReadMethod(method)) {
            return methodNotAllowed();
        }

        auto& sandbox = PluginSandbox::instance();
        auto stats = sandbox.getAllStats();
        
        std::ostringstream json;
        json << "{\"plugins\":[";
        for (size_t i = 0; i < stats.size(); i++) {
            if (i > 0) json << ",";
            json << "{";
            json << "\"name\":\"" << escapeJson(stats[i].plugin_name) << "\",";
            json << "\"enabled\":" << (stats[i].enabled ? "true" : "false") << ",";
            json << "\"memory\":" << stats[i].memory_used << ",";
            json << "\"cpu_us\":" << stats[i].cpu_time_us << ",";
            json << "\"violations\":" << stats[i].violations;
            json << "}";
        }
        json << "]}";
        return jsonResponse(json.str());
    }

    HttpResponse jsonResponse(const std::string& body, int status = 200) const {
        return HttpResponse{status, "application/json; charset=utf-8", body};
    }

    HttpResponse plainTextResponse(const std::string& body, const std::string& content_type) const {
        return HttpResponse{200, content_type, body};
    }

    HttpResponse methodNotAllowed() const {
        return jsonResponse("{\"error\":\"Method not allowed\"}", 405);
    }

    HttpResponse notFound() const {
        return jsonResponse("{\"error\":\"Not found\"}", 404);
    }

    bool isReadMethod(const std::string& method) const {
        return method == "GET" || method == "HEAD";
    }
};

}
