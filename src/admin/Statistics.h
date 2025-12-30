#pragma once

#include <string>
#include <map>
#include <mutex>
#include <atomic>
#include <chrono>

namespace LCHBOT {

struct GroupStats {
    int64_t group_id = 0;
    std::string personality_id = "yunmeng";
    std::atomic<int64_t> call_count{0};
    std::chrono::system_clock::time_point last_active;
};

class Statistics {
public:
    static Statistics& instance() {
        static Statistics inst;
        return inst;
    }
    
    void recordApiCall(int64_t group_id = 0) {
        total_api_calls_++;
        if (group_id > 0) {
            std::lock_guard<std::mutex> lock(mutex_);
            auto& stats = group_stats_[group_id];
            stats.group_id = group_id;
            stats.call_count++;
            stats.last_active = std::chrono::system_clock::now();
        }
    }
    
    void setGroupPersonality(int64_t group_id, const std::string& personality_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        group_stats_[group_id].group_id = group_id;
        group_stats_[group_id].personality_id = personality_id;
    }
    
    int64_t getTotalApiCalls() const { return total_api_calls_; }
    
    int64_t getActiveGroupCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return group_stats_.size();
    }
    
    int64_t getGroupCallCount(int64_t group_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = group_stats_.find(group_id);
        if (it != group_stats_.end()) {
            return it->second.call_count.load();
        }
        return 0;
    }
    
    std::string getGroupPersonality(int64_t group_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = group_stats_.find(group_id);
        if (it != group_stats_.end()) {
            return it->second.personality_id;
        }
        return "yunmeng";
    }
    
    std::map<int64_t, GroupStats> getGroupStats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::map<int64_t, GroupStats> result;
        for (const auto& [id, stats] : group_stats_) {
            result[id].group_id = stats.group_id;
            result[id].personality_id = stats.personality_id;
            result[id].call_count = stats.call_count.load();
            result[id].last_active = stats.last_active;
        }
        return result;
    }
    
private:
    Statistics() = default;
    
    std::atomic<int64_t> total_api_calls_{0};
    mutable std::mutex mutex_;
    std::map<int64_t, GroupStats> group_stats_;
};

}
