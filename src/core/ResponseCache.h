#pragma once

#include <string>
#include <map>
#include <list>
#include <mutex>
#include <atomic>
#include <chrono>
#include <functional>
#include <fstream>
#include <sstream>
#include <filesystem>
#include "Logger.h"

namespace LCHBOT {

struct CacheEntry {
    std::string key;
    std::string value;
    int64_t created_at;
    int64_t expires_at;
    int64_t last_accessed;
    int access_count;
    size_t size_bytes;
};

struct CacheStats {
    int64_t hits = 0;
    int64_t misses = 0;
    int64_t evictions = 0;
    int64_t expirations = 0;
    int64_t total_bytes = 0;
    int64_t entry_count = 0;
};

struct CacheStatsAtomic {
    std::atomic<int64_t> hits{0};
    std::atomic<int64_t> misses{0};
    std::atomic<int64_t> evictions{0};
    std::atomic<int64_t> expirations{0};
    std::atomic<int64_t> total_bytes{0};
    std::atomic<int64_t> entry_count{0};
};

class ResponseCache {
public:
    static ResponseCache& instance() {
        static ResponseCache inst;
        return inst;
    }
    
    void initialize(size_t max_size_bytes = 100 * 1024 * 1024,
                   int default_ttl_seconds = 3600,
                   const std::string& persist_path = "") {
        max_size_bytes_ = max_size_bytes;
        default_ttl_seconds_ = default_ttl_seconds;
        persist_path_ = persist_path;
        
        if (!persist_path.empty()) {
            loadFromDisk();
        }
        
        running_ = true;
        cleanup_thread_ = std::thread(&ResponseCache::cleanupLoop, this);
        
        LOG_INFO("[ResponseCache] Initialized: max=" + std::to_string(max_size_bytes / 1024 / 1024) + 
                 "MB, ttl=" + std::to_string(default_ttl_seconds) + "s");
    }
    
    void shutdown() {
        running_ = false;
        if (cleanup_thread_.joinable()) {
            cleanup_thread_.join();
        }
        
        if (!persist_path_.empty()) {
            saveToDisk();
        }
    }
    
    ~ResponseCache() {
        shutdown();
    }
    
    void setMaxSize(size_t bytes) {
        max_size_bytes_ = bytes;
        evictIfNeeded();
    }
    
    void setDefaultTTL(int seconds) {
        default_ttl_seconds_ = seconds;
    }
    
    std::string generateKey(const std::string& prompt, const std::string& model = "", 
                           const std::string& context = "") {
        std::string combined = prompt + "|" + model + "|" + context;
        return hashString(combined);
    }
    
    bool get(const std::string& key, std::string& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = cache_.find(key);
        if (it == cache_.end()) {
            stats_.misses++;
            return false;
        }
        
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        if (it->second.expires_at > 0 && it->second.expires_at < now) {
            removeEntry(it);
            stats_.expirations++;
            stats_.misses++;
            return false;
        }
        
        it->second.last_accessed = now;
        it->second.access_count++;
        
        moveToFront(key);
        
        value = it->second.value;
        stats_.hits++;
        return true;
    }
    
    void set(const std::string& key, const std::string& value, int ttl_seconds = -1) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        int actual_ttl = ttl_seconds >= 0 ? ttl_seconds : default_ttl_seconds_;
        
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            stats_.total_bytes -= it->second.size_bytes;
            it->second.value = value;
            it->second.size_bytes = value.size();
            it->second.expires_at = actual_ttl > 0 ? now + actual_ttl : 0;
            it->second.last_accessed = now;
            stats_.total_bytes += it->second.size_bytes;
            moveToFront(key);
            return;
        }
        
        CacheEntry entry;
        entry.key = key;
        entry.value = value;
        entry.created_at = now;
        entry.expires_at = actual_ttl > 0 ? now + actual_ttl : 0;
        entry.last_accessed = now;
        entry.access_count = 0;
        entry.size_bytes = value.size();
        
        size_t new_size = stats_.total_bytes + entry.size_bytes;
        while (new_size > max_size_bytes_ && !lru_list_.empty()) {
            evictOldest();
            new_size = stats_.total_bytes + entry.size_bytes;
        }
        
        cache_[key] = entry;
        lru_list_.push_front(key);
        lru_map_[key] = lru_list_.begin();
        
        stats_.total_bytes += entry.size_bytes;
        stats_.entry_count++;
    }
    
    void remove(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            removeEntry(it);
        }
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.clear();
        lru_list_.clear();
        lru_map_.clear();
        stats_.total_bytes = 0;
        stats_.entry_count = 0;
        LOG_INFO("[ResponseCache] Cache cleared");
    }
    
    void clearExpired() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        std::vector<std::string> expired_keys;
        for (const auto& [key, entry] : cache_) {
            if (entry.expires_at > 0 && entry.expires_at < now) {
                expired_keys.push_back(key);
            }
        }
        
        for (const auto& key : expired_keys) {
            auto it = cache_.find(key);
            if (it != cache_.end()) {
                removeEntry(it);
                stats_.expirations++;
            }
        }
    }
    
    bool exists(const std::string& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(key);
        if (it == cache_.end()) return false;
        
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        return it->second.expires_at == 0 || it->second.expires_at >= now;
    }
    
    CacheStats getStats() const {
        CacheStats copy;
        copy.hits = stats_.hits;
        copy.misses = stats_.misses;
        copy.evictions = stats_.evictions;
        copy.expirations = stats_.expirations;
        copy.total_bytes = stats_.total_bytes;
        copy.entry_count = stats_.entry_count;
        return copy;
    }
    
    double getHitRate() const {
        int64_t hits = stats_.hits.load();
        int64_t total = hits + stats_.misses.load();
        return total > 0 ? static_cast<double>(hits) / total : 0.0;
    }
    
    std::string exportMetrics() const {
        std::stringstream ss;
        ss << "cache_hits_total " << stats_.hits.load() << "\n";
        ss << "cache_misses_total " << stats_.misses.load() << "\n";
        ss << "cache_evictions_total " << stats_.evictions.load() << "\n";
        ss << "cache_expirations_total " << stats_.expirations.load() << "\n";
        ss << "cache_size_bytes " << stats_.total_bytes.load() << "\n";
        ss << "cache_entries " << stats_.entry_count.load() << "\n";
        ss << "cache_hit_rate " << getHitRate() << "\n";
        return ss.str();
    }
    
    template<typename Func>
    std::string getOrCompute(const std::string& key, Func&& compute_func, int ttl_seconds = -1) {
        std::string value;
        if (get(key, value)) {
            return value;
        }
        
        value = compute_func();
        set(key, value, ttl_seconds);
        return value;
    }
    
    void setNamespace(const std::string& ns) {
        current_namespace_ = ns;
    }
    
    std::string namespacedKey(const std::string& key) const {
        if (current_namespace_.empty()) return key;
        return current_namespace_ + ":" + key;
    }

private:
    ResponseCache() = default;
    
    std::string hashString(const std::string& str) const {
        uint64_t hash = 14695981039346656037ULL;
        for (char c : str) {
            hash ^= static_cast<uint64_t>(c);
            hash *= 1099511628211ULL;
        }
        
        std::stringstream ss;
        ss << std::hex << hash;
        return ss.str();
    }
    
    void removeEntry(std::map<std::string, CacheEntry>::iterator it) {
        stats_.total_bytes -= it->second.size_bytes;
        stats_.entry_count--;
        
        auto lru_it = lru_map_.find(it->first);
        if (lru_it != lru_map_.end()) {
            lru_list_.erase(lru_it->second);
            lru_map_.erase(lru_it);
        }
        
        cache_.erase(it);
    }
    
    void moveToFront(const std::string& key) {
        auto lru_it = lru_map_.find(key);
        if (lru_it != lru_map_.end()) {
            lru_list_.erase(lru_it->second);
            lru_list_.push_front(key);
            lru_map_[key] = lru_list_.begin();
        }
    }
    
    void evictOldest() {
        if (lru_list_.empty()) return;
        
        std::string oldest_key = lru_list_.back();
        auto it = cache_.find(oldest_key);
        if (it != cache_.end()) {
            removeEntry(it);
            stats_.evictions++;
        }
    }
    
    void evictIfNeeded() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (stats_.total_bytes > max_size_bytes_ && !lru_list_.empty()) {
            evictOldest();
        }
    }
    
    void cleanupLoop() {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::minutes(5));
            clearExpired();
            
            if (!persist_path_.empty()) {
                saveToDisk();
            }
        }
    }
    
    void saveToDisk() {
        if (persist_path_.empty()) return;
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::filesystem::path path(persist_path_);
        std::filesystem::create_directories(path.parent_path());
        
        std::ofstream file(persist_path_);
        if (!file.is_open()) return;
        
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        for (const auto& [key, entry] : cache_) {
            if (entry.expires_at > 0 && entry.expires_at < now) continue;
            
            file << entry.key << "\t"
                 << entry.created_at << "\t"
                 << entry.expires_at << "\t"
                 << entry.access_count << "\t"
                 << encodeValue(entry.value) << "\n";
        }
    }
    
    void loadFromDisk() {
        if (persist_path_.empty()) return;
        if (!std::filesystem::exists(persist_path_)) return;
        
        std::ifstream file(persist_path_);
        if (!file.is_open()) return;
        
        std::string line;
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            
            std::istringstream iss(line);
            std::string key, value;
            int64_t created_at, expires_at;
            int access_count;
            
            if (!(std::getline(iss, key, '\t') &&
                  iss >> created_at &&
                  iss.ignore() &&
                  iss >> expires_at &&
                  iss.ignore() &&
                  iss >> access_count &&
                  iss.ignore() &&
                  std::getline(iss, value))) {
                continue;
            }
            
            if (expires_at > 0 && expires_at < now) continue;
            
            CacheEntry entry;
            entry.key = key;
            entry.value = decodeValue(value);
            entry.created_at = created_at;
            entry.expires_at = expires_at;
            entry.last_accessed = now;
            entry.access_count = access_count;
            entry.size_bytes = entry.value.size();
            
            cache_[key] = entry;
            lru_list_.push_back(key);
            lru_map_[key] = --lru_list_.end();
            
            stats_.total_bytes += entry.size_bytes;
            stats_.entry_count++;
        }
        
        LOG_INFO("[ResponseCache] Loaded " + std::to_string(stats_.entry_count.load()) + " entries from disk");
    }
    
    std::string encodeValue(const std::string& value) const {
        std::string result;
        for (char c : value) {
            if (c == '\n') result += "\\n";
            else if (c == '\t') result += "\\t";
            else if (c == '\\') result += "\\\\";
            else result += c;
        }
        return result;
    }
    
    std::string decodeValue(const std::string& value) const {
        std::string result;
        for (size_t i = 0; i < value.size(); i++) {
            if (value[i] == '\\' && i + 1 < value.size()) {
                if (value[i+1] == 'n') { result += '\n'; i++; }
                else if (value[i+1] == 't') { result += '\t'; i++; }
                else if (value[i+1] == '\\') { result += '\\'; i++; }
                else result += value[i];
            } else {
                result += value[i];
            }
        }
        return result;
    }
    
    std::map<std::string, CacheEntry> cache_;
    std::list<std::string> lru_list_;
    std::map<std::string, std::list<std::string>::iterator> lru_map_;
    
    CacheStatsAtomic stats_;
    
    size_t max_size_bytes_ = 100 * 1024 * 1024;
    int default_ttl_seconds_ = 3600;
    std::string persist_path_;
    std::string current_namespace_;
    
    mutable std::mutex mutex_;
    std::thread cleanup_thread_;
    std::atomic<bool> running_{false};
};

}
