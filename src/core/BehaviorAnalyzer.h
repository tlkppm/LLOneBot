#pragma once

#include <map>
#include <deque>
#include <set>
#include <mutex>
#include <chrono>
#include <cmath>
#include <random>
#include <string>
#include <sstream>
#include <algorithm>
#include <vector>
#include "Logger.h"

namespace LCHBOT {

class BehaviorAnalyzer {
public:
    static BehaviorAnalyzer& instance() {
        static BehaviorAnalyzer inst;
        return inst;
    }

    void recordMessage(int64_t group_id, int64_t user_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = currentTime();
        auto& gs = groups_[group_id];
        gs.recent_msgs.push_back({now, user_id});
        if (gs.recent_msgs.size() > 300) gs.recent_msgs.pop_front();
        gs.msgs_since_bot++;

        std::time_t t = static_cast<std::time_t>(now);
        std::tm tm_buf;
        localtime_s(&tm_buf, &t);
        gs.hourly_histogram[tm_buf.tm_hour]++;
        gs.total_msgs_tracked++;

        gs.user_activity[user_id].last_active = now;
        gs.user_activity[user_id].msg_count++;
    }

    void recordBotReply(int64_t group_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& gs = groups_[group_id];
        gs.last_bot_reply = currentTime();
        gs.msgs_since_bot = 0;
    }

    double computeSpeakScore(int64_t group_id, bool bot_mentioned, bool has_question) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = groups_.find(group_id);
        if (it == groups_.end()) return 0.0;
        auto& gs = it->second;
        auto now = currentTime();

        if (gs.last_bot_reply == 0) gs.last_bot_reply = now;
        int64_t silence = now - gs.last_bot_reply;
        if (silence < 60) return 0.0;
        if (gs.msgs_since_bot < 3) return 0.0;

        double score = 0.0;

        int msgs_10min = countMessagesInWindow(gs, now, 600);
        int msgs_5min_recent = countMessagesInWindow(gs, now, 300);
        int msgs_5min_prev = msgs_10min - msgs_5min_recent;
        double momentum = (std::min)(msgs_10min / 15.0, 1.0);
        score += momentum * 0.25;

        double time_factor = (std::min)(silence / 900.0, 1.0);
        score += time_factor * 0.20;

        double msg_factor = (std::min)(gs.msgs_since_bot / 25.0, 1.0);
        score += msg_factor * 0.15;

        int active_users = countActiveUsersInWindow(gs, now, 600);
        if (active_users >= 3) score += 0.05;
        if (active_users >= 5) score += 0.05;

        std::time_t t = static_cast<std::time_t>(now);
        std::tm tm_buf;
        localtime_s(&tm_buf, &t);
        if (isPeakHour(gs, tm_buf.tm_hour)) score += 0.10;

        if (bot_mentioned) score += 0.35;
        if (has_question) score += 0.12;

        if (msgs_5min_recent > msgs_5min_prev && msgs_5min_recent >= 3) score += 0.08;

        return (std::min)(score, 0.85);
    }

    std::string getGroupAnalysis(int64_t group_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = groups_.find(group_id);
        if (it == groups_.end()) return "\xe6\x97\xa0\xe6\x95\xb0\xe6\x8d\xae";
        auto& gs = it->second;
        auto now = currentTime();

        int m10 = countMessagesInWindow(gs, now, 600);
        int m60 = countMessagesInWindow(gs, now, 3600);
        int au10 = countActiveUsersInWindow(gs, now, 600);
        int au60 = countActiveUsersInWindow(gs, now, 3600);

        std::stringstream ss;
        ss << "[\xe7\xbe\xa4\xe8\xa1\x8c\xe4\xb8\xba\xe5\x88\x86\xe6\x9e\x90]\n";
        ss << "\xe8\xbf\x91" "10\xe5\x88\x86\xe9\x92\x9f\xe6\xb6\x88\xe6\x81\xaf:" << m10
           << " \xe8\xbf\x91" "1\xe5\xb0\x8f\xe6\x97\xb6:" << m60 << "\n";
        ss << "\xe6\xb4\xbb\xe8\xb7\x83\xe7\x94\xa8\xe6\x88\xb7" "10min:" << au10
           << " 1h:" << au60 << "\n";
        ss << "\xe8\xb7\x9d\xe4\xb8\x8a\xe6\xac\xa1\xe5\x8f\x91\xe8\xa8\x80:" << gs.msgs_since_bot << "\xe6\x9d\xa1\n";

        if (gs.last_bot_reply > 0) {
            ss << "\xe6\xb2\x89\xe9\xbb\x98\xe6\x97\xb6\xe9\x95\xbf:" << (now - gs.last_bot_reply) << "\xe7\xa7\x92\n";
        }

        ss << "\xe9\xab\x98\xe5\xb3\xb0\xe6\x97\xb6\xe6\xae\xb5:";
        auto peaks = getPeakHours(gs);
        for (size_t i = 0; i < peaks.size(); i++) {
            if (i > 0) ss << ",";
            ss << peaks[i] << ":00";
        }
        ss << "\n";

        double momentum_score = (std::min)(m10 / 15.0, 1.0);
        std::string level;
        if (momentum_score > 0.7) level = "\xe9\xab\x98\xe5\xba\xa6\xe6\xb4\xbb\xe8\xb7\x83";
        else if (momentum_score > 0.3) level = "\xe4\xb8\xad\xe7\xad\x89\xe6\xb4\xbb\xe8\xb7\x83";
        else level = "\xe4\xbd\x8e\xe6\xb4\xbb\xe8\xb7\x83";
        ss << "\xe6\xb4\xbb\xe8\xb7\x83\xe7\xad\x89\xe7\xba\xa7:" << level << "\n";

        ss << "\xe6\xb4\xbb\xe8\xb7\x83\xe7\x94\xa8\xe6\x88\xb7\xe6\x8e\x92\xe8\xa1\x8c:\n";
        std::vector<std::pair<int64_t, int>> sorted_users;
        for (const auto& [uid, ua] : gs.user_activity) {
            sorted_users.push_back({uid, ua.msg_count});
        }
        std::sort(sorted_users.begin(), sorted_users.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
        int shown = 0;
        for (const auto& [uid, cnt] : sorted_users) {
            if (shown >= 10) break;
            ss << "  " << uid << ": " << cnt << "\xe6\x9d\xa1\n";
            shown++;
        }

        return ss.str();
    }

    int getMessagesSinceBot(int64_t group_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = groups_.find(group_id);
        if (it == groups_.end()) return 0;
        return it->second.msgs_since_bot;
    }

    double getGroupMomentum(int64_t group_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = groups_.find(group_id);
        if (it == groups_.end()) return 0.0;
        auto now = currentTime();
        int m10 = countMessagesInWindow(it->second, now, 600);
        return (std::min)(m10 / 15.0, 1.0);
    }

private:
    struct UserActivity {
        int64_t last_active = 0;
        int msg_count = 0;
    };

    struct GroupState {
        std::deque<std::pair<int64_t, int64_t>> recent_msgs;
        int64_t last_bot_reply = 0;
        int msgs_since_bot = 0;
        std::map<int, int> hourly_histogram;
        int total_msgs_tracked = 0;
        std::map<int64_t, UserActivity> user_activity;
    };

    int64_t currentTime() {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    int countMessagesInWindow(const GroupState& gs, int64_t now, int window_secs) {
        int count = 0;
        int64_t cutoff = now - window_secs;
        for (auto it = gs.recent_msgs.rbegin(); it != gs.recent_msgs.rend(); ++it) {
            if (it->first < cutoff) break;
            count++;
        }
        return count;
    }

    int countActiveUsersInWindow(const GroupState& gs, int64_t now, int window_secs) {
        std::set<int64_t> users;
        int64_t cutoff = now - window_secs;
        for (auto it = gs.recent_msgs.rbegin(); it != gs.recent_msgs.rend(); ++it) {
            if (it->first < cutoff) break;
            users.insert(it->second);
        }
        return (int)users.size();
    }

    bool isPeakHour(const GroupState& gs, int hour) {
        auto peaks = getPeakHours(gs);
        for (int p : peaks) {
            if (p == hour) return true;
        }
        return false;
    }

    std::vector<int> getPeakHours(const GroupState& gs) {
        if (gs.total_msgs_tracked < 50) return {20, 21, 22, 23};
        std::vector<std::pair<int, int>> hours;
        for (const auto& [h, c] : gs.hourly_histogram) {
            hours.push_back({h, c});
        }
        std::sort(hours.begin(), hours.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
        std::vector<int> result;
        for (size_t i = 0; i < 4 && i < hours.size(); i++) {
            result.push_back(hours[i].first);
        }
        return result;
    }

    std::map<int64_t, GroupState> groups_;
    std::mutex mutex_;
};

}
