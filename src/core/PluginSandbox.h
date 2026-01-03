#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>
#include <future>
#include "Logger.h"

namespace LCHBOT {

enum class SandboxPermission {
    NONE = 0,
    READ_CONFIG = 1,
    WRITE_CONFIG = 2,
    NETWORK_ACCESS = 4,
    FILE_READ = 8,
    FILE_WRITE = 16,
    EXECUTE_COMMAND = 32,
    SEND_MESSAGE = 64,
    READ_HISTORY = 128,
    ADMIN_API = 256,
    ALL = 511
};

inline SandboxPermission operator|(SandboxPermission a, SandboxPermission b) {
    return static_cast<SandboxPermission>(static_cast<int>(a) | static_cast<int>(b));
}

inline bool hasPermission(SandboxPermission granted, SandboxPermission required) {
    return (static_cast<int>(granted) & static_cast<int>(required)) == static_cast<int>(required);
}

struct ResourceLimits {
    int64_t max_memory_bytes = 100 * 1024 * 1024;
    int max_cpu_time_ms = 5000;
    int max_execution_time_ms = 30000;
    int max_network_requests = 100;
    int max_file_operations = 1000;
    int max_messages_per_minute = 60;
    std::vector<std::string> allowed_paths;
    std::vector<std::string> allowed_hosts;
};

struct PluginResourceUsage {
    std::atomic<int64_t> memory_used{0};
    std::atomic<int64_t> cpu_time_us{0};
    std::atomic<int> network_requests{0};
    std::atomic<int> file_operations{0};
    std::atomic<int> messages_sent{0};
    std::chrono::steady_clock::time_point last_reset;
    std::atomic<int> violations{0};
};

struct SandboxConfig {
    std::string plugin_name;
    SandboxPermission permissions = SandboxPermission::SEND_MESSAGE | SandboxPermission::READ_HISTORY;
    ResourceLimits limits;
    bool enabled = true;
    bool log_violations = true;
    bool kill_on_violation = false;
};

class PluginSandbox {
public:
    static PluginSandbox& instance() {
        static PluginSandbox inst;
        return inst;
    }
    
    void initialize() {
        running_ = true;
        monitor_thread_ = std::thread(&PluginSandbox::monitorLoop, this);
        LOG_INFO("[PluginSandbox] Initialized");
    }
    
    void shutdown() {
        running_ = false;
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }
    }
    
    ~PluginSandbox() {
        shutdown();
    }
    
    void registerPlugin(const std::string& plugin_name, const SandboxConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        configs_[plugin_name] = config;
        usage_[plugin_name] = std::make_shared<PluginResourceUsage>();
        usage_[plugin_name]->last_reset = std::chrono::steady_clock::now();
        LOG_INFO("[PluginSandbox] Registered plugin: " + plugin_name);
    }
    
    void unregisterPlugin(const std::string& plugin_name) {
        std::lock_guard<std::mutex> lock(mutex_);
        configs_.erase(plugin_name);
        usage_.erase(plugin_name);
    }
    
    void setPermissions(const std::string& plugin_name, SandboxPermission perms) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (configs_.count(plugin_name)) {
            configs_[plugin_name].permissions = perms;
        }
    }
    
    void setLimits(const std::string& plugin_name, const ResourceLimits& limits) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (configs_.count(plugin_name)) {
            configs_[plugin_name].limits = limits;
        }
    }
    
    bool checkPermission(const std::string& plugin_name, SandboxPermission required) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = configs_.find(plugin_name);
        if (it == configs_.end()) return false;
        if (!it->second.enabled) return false;
        
        bool allowed = hasPermission(it->second.permissions, required);
        if (!allowed && it->second.log_violations) {
            LOG_WARN("[PluginSandbox] Permission denied for " + plugin_name + 
                     ": " + std::to_string(static_cast<int>(required)));
            recordViolation(plugin_name, "permission_denied");
        }
        return allowed;
    }
    
    bool checkAndRecordNetworkRequest(const std::string& plugin_name, const std::string& host = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        auto cfg_it = configs_.find(plugin_name);
        auto usage_it = usage_.find(plugin_name);
        if (cfg_it == configs_.end() || usage_it == usage_.end()) return false;
        
        auto& config = cfg_it->second;
        auto& usage = usage_it->second;
        
        if (!hasPermission(config.permissions, SandboxPermission::NETWORK_ACCESS)) {
            recordViolation(plugin_name, "network_not_allowed");
            return false;
        }
        
        if (!host.empty() && !config.limits.allowed_hosts.empty()) {
            bool host_allowed = false;
            for (const auto& allowed : config.limits.allowed_hosts) {
                if (host.find(allowed) != std::string::npos) {
                    host_allowed = true;
                    break;
                }
            }
            if (!host_allowed) {
                recordViolation(plugin_name, "host_not_allowed:" + host);
                return false;
            }
        }
        
        if (usage->network_requests >= config.limits.max_network_requests) {
            recordViolation(plugin_name, "network_limit_exceeded");
            return false;
        }
        
        usage->network_requests++;
        return true;
    }
    
    bool checkAndRecordFileOperation(const std::string& plugin_name, const std::string& path, bool is_write) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto cfg_it = configs_.find(plugin_name);
        auto usage_it = usage_.find(plugin_name);
        if (cfg_it == configs_.end() || usage_it == usage_.end()) return false;
        
        auto& config = cfg_it->second;
        auto& usage = usage_it->second;
        
        SandboxPermission required = is_write ? SandboxPermission::FILE_WRITE : SandboxPermission::FILE_READ;
        if (!hasPermission(config.permissions, required)) {
            recordViolation(plugin_name, is_write ? "file_write_not_allowed" : "file_read_not_allowed");
            return false;
        }
        
        if (!config.limits.allowed_paths.empty()) {
            bool path_allowed = false;
            for (const auto& allowed : config.limits.allowed_paths) {
                if (path.find(allowed) == 0) {
                    path_allowed = true;
                    break;
                }
            }
            if (!path_allowed) {
                recordViolation(plugin_name, "path_not_allowed:" + path);
                return false;
            }
        }
        
        if (usage->file_operations >= config.limits.max_file_operations) {
            recordViolation(plugin_name, "file_operation_limit_exceeded");
            return false;
        }
        
        usage->file_operations++;
        return true;
    }
    
    bool checkAndRecordMessage(const std::string& plugin_name) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto cfg_it = configs_.find(plugin_name);
        auto usage_it = usage_.find(plugin_name);
        if (cfg_it == configs_.end() || usage_it == usage_.end()) return false;
        
        auto& config = cfg_it->second;
        auto& usage = usage_it->second;
        
        if (!hasPermission(config.permissions, SandboxPermission::SEND_MESSAGE)) {
            recordViolation(plugin_name, "send_message_not_allowed");
            return false;
        }
        
        if (usage->messages_sent >= config.limits.max_messages_per_minute) {
            recordViolation(plugin_name, "message_rate_limit_exceeded");
            return false;
        }
        
        usage->messages_sent++;
        return true;
    }
    
    void recordMemoryUsage(const std::string& plugin_name, int64_t bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto usage_it = usage_.find(plugin_name);
        if (usage_it != usage_.end()) {
            usage_it->second->memory_used = bytes;
            
            auto cfg_it = configs_.find(plugin_name);
            if (cfg_it != configs_.end() && bytes > cfg_it->second.limits.max_memory_bytes) {
                recordViolation(plugin_name, "memory_limit_exceeded");
            }
        }
    }
    
    void recordCpuTime(const std::string& plugin_name, int64_t microseconds) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto usage_it = usage_.find(plugin_name);
        if (usage_it != usage_.end()) {
            usage_it->second->cpu_time_us += microseconds;
        }
    }
    
    template<typename Func>
    auto executeWithTimeout(const std::string& plugin_name, Func&& func, int timeout_ms = 0) 
        -> decltype(func()) {
        
        int actual_timeout = timeout_ms;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto cfg_it = configs_.find(plugin_name);
            if (cfg_it != configs_.end() && timeout_ms == 0) {
                actual_timeout = cfg_it->second.limits.max_execution_time_ms;
            }
        }
        
        if (actual_timeout <= 0) {
            return func();
        }
        
        auto future = std::async(std::launch::async, std::forward<Func>(func));
        
        if (future.wait_for(std::chrono::milliseconds(actual_timeout)) == std::future_status::timeout) {
            recordViolation(plugin_name, "execution_timeout");
            throw std::runtime_error("Plugin execution timeout");
        }
        
        return future.get();
    }
    
    struct PluginStats {
        std::string plugin_name;
        bool enabled;
        int64_t memory_used;
        int64_t cpu_time_us;
        int network_requests;
        int file_operations;
        int messages_sent;
        int violations;
        SandboxPermission permissions;
    };
    
    std::vector<PluginStats> getAllStats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<PluginStats> result;
        
        for (const auto& [name, config] : configs_) {
            PluginStats stats;
            stats.plugin_name = name;
            stats.enabled = config.enabled;
            stats.permissions = config.permissions;
            
            auto usage_it = usage_.find(name);
            if (usage_it != usage_.end()) {
                stats.memory_used = usage_it->second->memory_used.load();
                stats.cpu_time_us = usage_it->second->cpu_time_us.load();
                stats.network_requests = usage_it->second->network_requests.load();
                stats.file_operations = usage_it->second->file_operations.load();
                stats.messages_sent = usage_it->second->messages_sent.load();
                stats.violations = usage_it->second->violations.load();
            }
            
            result.push_back(stats);
        }
        
        return result;
    }
    
    void setPluginEnabled(const std::string& plugin_name, bool enabled) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (configs_.count(plugin_name)) {
            configs_[plugin_name].enabled = enabled;
        }
    }
    
    std::vector<std::pair<std::string, std::string>> getViolationLog(size_t limit = 100) const {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t start = violation_log_.size() > limit ? violation_log_.size() - limit : 0;
        return std::vector<std::pair<std::string, std::string>>(
            violation_log_.begin() + start, violation_log_.end());
    }

private:
    PluginSandbox() = default;
    
    void recordViolation(const std::string& plugin_name, const std::string& type) {
        auto usage_it = usage_.find(plugin_name);
        if (usage_it != usage_.end()) {
            usage_it->second->violations++;
        }
        
        violation_log_.emplace_back(plugin_name, type);
        if (violation_log_.size() > 10000) {
            violation_log_.erase(violation_log_.begin(), violation_log_.begin() + 1000);
        }
        
        auto cfg_it = configs_.find(plugin_name);
        if (cfg_it != configs_.end() && cfg_it->second.kill_on_violation) {
            cfg_it->second.enabled = false;
            LOG_WARN("[PluginSandbox] Plugin disabled due to violation: " + plugin_name);
        }
    }
    
    void monitorLoop() {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(60));
            
            std::lock_guard<std::mutex> lock(mutex_);
            auto now = std::chrono::steady_clock::now();
            
            for (auto& [name, usage] : usage_) {
                auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(
                    now - usage->last_reset).count();
                
                if (elapsed >= 1) {
                    usage->messages_sent = 0;
                    usage->network_requests = 0;
                    usage->file_operations = 0;
                    usage->last_reset = now;
                }
            }
        }
    }
    
    std::map<std::string, SandboxConfig> configs_;
    std::map<std::string, std::shared_ptr<PluginResourceUsage>> usage_;
    std::vector<std::pair<std::string, std::string>> violation_log_;
    mutable std::mutex mutex_;
    std::thread monitor_thread_;
    std::atomic<bool> running_{false};
};

class SandboxGuard {
public:
    SandboxGuard(const std::string& plugin_name, SandboxPermission required)
        : plugin_name_(plugin_name), allowed_(false) {
        allowed_ = PluginSandbox::instance().checkPermission(plugin_name, required);
    }
    
    bool allowed() const { return allowed_; }
    operator bool() const { return allowed_; }
    
private:
    std::string plugin_name_;
    bool allowed_;
};

#define SANDBOX_CHECK(plugin, perm) SandboxGuard _sandbox_guard_##__LINE__(plugin, perm); if (!_sandbox_guard_##__LINE__)

}
