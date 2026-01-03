#pragma once

#include <string>
#include <map>
#include <deque>
#include <mutex>
#include <chrono>
#include <atomic>
#include <thread>
#include <functional>
#include "Logger.h"

namespace LCHBOT {

enum class RateLimitResult {
    ALLOWED,
    RATE_LIMITED,
    CIRCUIT_BREAKER_OPEN
};

struct RateLimitConfig {
    int requests_per_second = 10;
    int requests_per_minute = 100;
    int requests_per_hour = 1000;
    int burst_size = 20;
    int circuit_breaker_threshold = 5;
    int circuit_breaker_timeout_ms = 30000;
};

struct RateLimitBucket {
    std::deque<int64_t> request_times;
    std::atomic<int> consecutive_failures{0};
    std::atomic<int64_t> circuit_breaker_open_until{0};
    std::atomic<int64_t> total_requests{0};
    std::atomic<int64_t> total_limited{0};
    int64_t last_cleanup = 0;
};

class RateLimiter {
public:
    static RateLimiter& instance() {
        static RateLimiter inst;
        return inst;
    }
    
    void initialize() {
        running_ = true;
        cleanup_thread_ = std::thread(&RateLimiter::cleanupLoop, this);
        LOG_INFO("[RateLimiter] Initialized");
    }
    
    void shutdown() {
        running_ = false;
        if (cleanup_thread_.joinable()) {
            cleanup_thread_.join();
        }
    }
    
    ~RateLimiter() {
        shutdown();
    }
    
    void setConfig(const std::string& key, const RateLimitConfig& config) {
        std::lock_guard<std::mutex> lock(config_mutex_);
        configs_[key] = config;
    }
    
    void setDefaultConfig(const RateLimitConfig& config) {
        std::lock_guard<std::mutex> lock(config_mutex_);
        default_config_ = config;
    }
    
    RateLimitResult checkLimit(const std::string& key) {
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        std::lock_guard<std::mutex> lock(buckets_mutex_);
        auto& bucket = buckets_[key];
        bucket.total_requests++;
        
        if (bucket.circuit_breaker_open_until.load() > now) {
            bucket.total_limited++;
            return RateLimitResult::CIRCUIT_BREAKER_OPEN;
        }
        
        RateLimitConfig config;
        {
            std::lock_guard<std::mutex> cfg_lock(config_mutex_);
            auto it = configs_.find(key);
            config = (it != configs_.end()) ? it->second : default_config_;
        }
        
        if (now - bucket.last_cleanup > 60000) {
            cleanupBucket(bucket, now);
            bucket.last_cleanup = now;
        }
        
        int64_t one_second_ago = now - 1000;
        int64_t one_minute_ago = now - 60000;
        int64_t one_hour_ago = now - 3600000;
        
        int count_second = 0, count_minute = 0, count_hour = 0;
        for (const auto& t : bucket.request_times) {
            if (t > one_second_ago) count_second++;
            if (t > one_minute_ago) count_minute++;
            if (t > one_hour_ago) count_hour++;
        }
        
        if (count_second >= config.requests_per_second ||
            count_minute >= config.requests_per_minute ||
            count_hour >= config.requests_per_hour) {
            bucket.total_limited++;
            return RateLimitResult::RATE_LIMITED;
        }
        
        bucket.request_times.push_back(now);
        
        if (bucket.request_times.size() > static_cast<size_t>(config.burst_size * 10)) {
            while (bucket.request_times.size() > static_cast<size_t>(config.burst_size * 5)) {
                bucket.request_times.pop_front();
            }
        }
        
        return RateLimitResult::ALLOWED;
    }
    
    void recordSuccess(const std::string& key) {
        std::lock_guard<std::mutex> lock(buckets_mutex_);
        auto& bucket = buckets_[key];
        bucket.consecutive_failures = 0;
    }
    
    void recordFailure(const std::string& key) {
        std::lock_guard<std::mutex> lock(buckets_mutex_);
        auto& bucket = buckets_[key];
        
        RateLimitConfig config;
        {
            std::lock_guard<std::mutex> cfg_lock(config_mutex_);
            auto it = configs_.find(key);
            config = (it != configs_.end()) ? it->second : default_config_;
        }
        
        int failures = ++bucket.consecutive_failures;
        if (failures >= config.circuit_breaker_threshold) {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            bucket.circuit_breaker_open_until = now + config.circuit_breaker_timeout_ms;
            LOG_WARN("[RateLimiter] Circuit breaker opened for: " + key);
        }
    }
    
    void resetCircuitBreaker(const std::string& key) {
        std::lock_guard<std::mutex> lock(buckets_mutex_);
        auto& bucket = buckets_[key];
        bucket.circuit_breaker_open_until = 0;
        bucket.consecutive_failures = 0;
    }
    
    struct LimitStats {
        int64_t total_requests = 0;
        int64_t total_limited = 0;
        int current_rps = 0;
        bool circuit_breaker_open = false;
    };
    
    LimitStats getStats(const std::string& key) const {
        std::lock_guard<std::mutex> lock(buckets_mutex_);
        LimitStats stats;
        
        auto it = buckets_.find(key);
        if (it != buckets_.end()) {
            stats.total_requests = it->second.total_requests.load();
            stats.total_limited = it->second.total_limited.load();
            
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            stats.circuit_breaker_open = it->second.circuit_breaker_open_until.load() > now;
            
            int64_t one_second_ago = now - 1000;
            for (const auto& t : it->second.request_times) {
                if (t > one_second_ago) stats.current_rps++;
            }
        }
        
        return stats;
    }
    
    std::string exportMetrics() const {
        std::lock_guard<std::mutex> lock(buckets_mutex_);
        std::string result;
        
        for (const auto& [key, bucket] : buckets_) {
            result += "rate_limiter_total{key=\"" + key + "\"} " + 
                      std::to_string(bucket.total_requests.load()) + "\n";
            result += "rate_limiter_limited{key=\"" + key + "\"} " + 
                      std::to_string(bucket.total_limited.load()) + "\n";
        }
        
        return result;
    }
    
    class ScopedRateLimit {
    public:
        ScopedRateLimit(const std::string& key) : key_(key), allowed_(false) {
            result_ = RateLimiter::instance().checkLimit(key);
            allowed_ = (result_ == RateLimitResult::ALLOWED);
        }
        
        ~ScopedRateLimit() {
            if (allowed_ && success_) {
                RateLimiter::instance().recordSuccess(key_);
            } else if (allowed_ && !success_) {
                RateLimiter::instance().recordFailure(key_);
            }
        }
        
        bool allowed() const { return allowed_; }
        RateLimitResult result() const { return result_; }
        void markSuccess() { success_ = true; }
        void markFailure() { success_ = false; }
        
    private:
        std::string key_;
        bool allowed_;
        bool success_ = true;
        RateLimitResult result_;
    };

private:
    RateLimiter() = default;
    
    void cleanupBucket(RateLimitBucket& bucket, int64_t now) {
        int64_t cutoff = now - 3600000;
        while (!bucket.request_times.empty() && bucket.request_times.front() < cutoff) {
            bucket.request_times.pop_front();
        }
    }
    
    void cleanupLoop() {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(60));
            
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            
            std::lock_guard<std::mutex> lock(buckets_mutex_);
            for (auto& [key, bucket] : buckets_) {
                cleanupBucket(bucket, now);
            }
        }
    }
    
    std::map<std::string, RateLimitBucket> buckets_;
    std::map<std::string, RateLimitConfig> configs_;
    RateLimitConfig default_config_;
    mutable std::mutex buckets_mutex_;
    std::mutex config_mutex_;
    std::thread cleanup_thread_;
    std::atomic<bool> running_{false};
};

#define RATE_LIMIT_CHECK(key) \
    RateLimiter::ScopedRateLimit _rate_limit_##__LINE__(key); \
    if (!_rate_limit_##__LINE__.allowed())

}
