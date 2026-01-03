#pragma once

#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <filesystem>
#include <chrono>
#include <fstream>
#include <sstream>
#include "Logger.h"

namespace LCHBOT {

struct WatchedFile {
    std::string path;
    std::filesystem::file_time_type last_modified;
    std::function<void(const std::string&)> callback;
    bool enabled = true;
};

class ConfigWatcher {
public:
    static ConfigWatcher& instance() {
        static ConfigWatcher inst;
        return inst;
    }
    
    void initialize(int check_interval_ms = 5000) {
        check_interval_ms_ = check_interval_ms;
        running_ = true;
        watch_thread_ = std::thread(&ConfigWatcher::watchLoop, this);
        LOG_INFO("[ConfigWatcher] Initialized with " + std::to_string(check_interval_ms) + "ms interval");
    }
    
    void shutdown() {
        running_ = false;
        if (watch_thread_.joinable()) {
            watch_thread_.join();
        }
    }
    
    ~ConfigWatcher() {
        shutdown();
    }
    
    void watchFile(const std::string& path, std::function<void(const std::string&)> callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        WatchedFile wf;
        wf.path = path;
        wf.callback = callback;
        
        if (std::filesystem::exists(path)) {
            wf.last_modified = std::filesystem::last_write_time(path);
        }
        
        watched_files_[path] = wf;
        LOG_INFO("[ConfigWatcher] Watching: " + path);
    }
    
    void unwatchFile(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        watched_files_.erase(path);
    }
    
    void setEnabled(const std::string& path, bool enabled) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = watched_files_.find(path);
        if (it != watched_files_.end()) {
            it->second.enabled = enabled;
        }
    }
    
    void triggerReload(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = watched_files_.find(path);
        if (it != watched_files_.end() && it->second.enabled) {
            std::string content = readFile(path);
            if (!content.empty()) {
                it->second.callback(content);
                LOG_INFO("[ConfigWatcher] Manual reload triggered: " + path);
            }
        }
    }
    
    void triggerReloadAll() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [path, wf] : watched_files_) {
            if (wf.enabled) {
                std::string content = readFile(path);
                if (!content.empty()) {
                    wf.callback(content);
                }
            }
        }
        LOG_INFO("[ConfigWatcher] All configs reloaded");
    }
    
    std::vector<std::string> getWatchedFiles() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> result;
        for (const auto& [path, wf] : watched_files_) {
            result.push_back(path);
        }
        return result;
    }
    
    void setCheckInterval(int ms) {
        check_interval_ms_ = ms;
    }
    
    struct FileStatus {
        std::string path;
        bool exists;
        bool enabled;
        int64_t last_modified;
        int64_t size;
    };
    
    std::vector<FileStatus> getStatus() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<FileStatus> result;
        
        for (const auto& [path, wf] : watched_files_) {
            FileStatus status;
            status.path = path;
            status.enabled = wf.enabled;
            status.exists = std::filesystem::exists(path);
            
            if (status.exists) {
                auto ftime = std::filesystem::last_write_time(path);
                auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(
                    ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
                status.last_modified = sctp.time_since_epoch().count();
                status.size = std::filesystem::file_size(path);
            } else {
                status.last_modified = 0;
                status.size = 0;
            }
            
            result.push_back(status);
        }
        
        return result;
    }

private:
    ConfigWatcher() = default;
    
    void watchLoop() {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(check_interval_ms_));
            
            std::lock_guard<std::mutex> lock(mutex_);
            
            for (auto& [path, wf] : watched_files_) {
                if (!wf.enabled) continue;
                
                if (!std::filesystem::exists(path)) continue;
                
                auto current_time = std::filesystem::last_write_time(path);
                
                if (current_time != wf.last_modified) {
                    wf.last_modified = current_time;
                    
                    std::string content = readFile(path);
                    if (!content.empty()) {
                        LOG_INFO("[ConfigWatcher] File changed, reloading: " + path);
                        
                        try {
                            wf.callback(content);
                            reload_count_++;
                        } catch (const std::exception& e) {
                            LOG_ERROR("[ConfigWatcher] Reload failed for " + path + ": " + e.what());
                        }
                    }
                }
            }
        }
    }
    
    std::string readFile(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) return "";
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }
    
    std::map<std::string, WatchedFile> watched_files_;
    mutable std::mutex mutex_;
    std::thread watch_thread_;
    std::atomic<bool> running_{false};
    std::atomic<int> check_interval_ms_{5000};
    std::atomic<int64_t> reload_count_{0};
};

template<typename T>
class HotReloadableConfig {
public:
    HotReloadableConfig(const std::string& path, std::function<T(const std::string&)> parser)
        : path_(path), parser_(parser) {
        
        if (std::filesystem::exists(path)) {
            std::ifstream file(path);
            if (file.is_open()) {
                std::stringstream buffer;
                buffer << file.rdbuf();
                config_ = parser_(buffer.str());
            }
        }
        
        ConfigWatcher::instance().watchFile(path, [this](const std::string& content) {
            std::lock_guard<std::mutex> lock(mutex_);
            try {
                T new_config = parser_(content);
                config_ = new_config;
                version_++;
                
                for (const auto& callback : change_callbacks_) {
                    callback(config_);
                }
            } catch (const std::exception& e) {
                LOG_ERROR("[HotReloadableConfig] Parse failed: " + std::string(e.what()));
            }
        });
    }
    
    ~HotReloadableConfig() {
        ConfigWatcher::instance().unwatchFile(path_);
    }
    
    T get() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return config_;
    }
    
    int64_t version() const {
        return version_.load();
    }
    
    void onChange(std::function<void(const T&)> callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        change_callbacks_.push_back(callback);
    }
    
    void reload() {
        ConfigWatcher::instance().triggerReload(path_);
    }

private:
    std::string path_;
    std::function<T(const std::string&)> parser_;
    T config_;
    mutable std::mutex mutex_;
    std::atomic<int64_t> version_{0};
    std::vector<std::function<void(const T&)>> change_callbacks_;
};

}
