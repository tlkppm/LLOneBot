#pragma once
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <thread>
#include <atomic>
#include <fstream>
#include <filesystem>
#include "JsonParser.h"
#include "Logger.h"

namespace lchbot {

struct QueuedMessage {
    std::string action;
    int64_t target_id;
    std::string message;
    int64_t timestamp;
};

class MessageQueue {
public:
    using SendCallback = std::function<void(const std::string&, int64_t)>;
    
    static MessageQueue& instance() {
        static MessageQueue inst;
        return inst;
    }
    
    void setSendGroupCallback(SendCallback cb) {
        std::lock_guard<std::mutex> lock(mutex_);
        send_group_callback_ = cb;
    }
    
    void setSendPrivateCallback(SendCallback cb) {
        std::lock_guard<std::mutex> lock(mutex_);
        send_private_callback_ = cb;
    }
    
    void enqueue(const std::string& action, int64_t target_id, const std::string& message) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            QueuedMessage msg;
            msg.action = action;
            msg.target_id = target_id;
            msg.message = message;
            msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            queue_.push(msg);
            persistToFile();
        }
        cv_.notify_one();
    }
    
    void start() {
        if (running_) return;
        running_ = true;
        loadFromFile();
        worker_thread_ = std::thread(&MessageQueue::workerLoop, this);
        LOG_INFO("[MessageQueue] Started");
    }
    
    void stop() {
        running_ = false;
        cv_.notify_all();
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
        LOG_INFO("[MessageQueue] Stopped");
    }
    
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
    
private:
    MessageQueue() : running_(false) {
        queue_file_ = "data/message_queue.json";
        std::filesystem::create_directories("data");
    }
    
    ~MessageQueue() {
        stop();
    }
    
    void workerLoop() {
        while (running_) {
            QueuedMessage msg;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait_for(lock, std::chrono::milliseconds(100), [this] {
                    return !running_ || !queue_.empty();
                });
                
                if (!running_ && queue_.empty()) break;
                if (queue_.empty()) continue;
                
                msg = queue_.front();
                queue_.pop();
                persistToFile();
            }
            
            try {
                if (msg.action == "send_group_msg" && send_group_callback_) {
                    send_group_callback_(msg.message, msg.target_id);
                    LOG_INFO("[MessageQueue] Sent group msg to " + std::to_string(msg.target_id));
                } else if (msg.action == "send_private_msg" && send_private_callback_) {
                    send_private_callback_(msg.message, msg.target_id);
                    LOG_INFO("[MessageQueue] Sent private msg to " + std::to_string(msg.target_id));
                }
            } catch (const std::exception& e) {
                LOG_ERROR("[MessageQueue] Send failed: " + std::string(e.what()));
                std::lock_guard<std::mutex> lock(mutex_);
                queue_.push(msg);
                persistToFile();
            }
        }
    }
    
    void persistToFile() {
        try {
            std::ofstream file(queue_file_);
            if (!file) return;
            
            file << "[";
            std::queue<QueuedMessage> temp = queue_;
            bool first = true;
            while (!temp.empty()) {
                if (!first) file << ",";
                first = false;
                auto& m = temp.front();
                file << "{\"action\":\"" << m.action 
                     << "\",\"target_id\":" << m.target_id
                     << ",\"message\":\"";
                for (char c : m.message) {
                    if (c == '"') file << "\\\"";
                    else if (c == '\\') file << "\\\\";
                    else if (c == '\n') file << "\\n";
                    else if (c == '\r') file << "\\r";
                    else if (c == '\t') file << "\\t";
                    else if (static_cast<unsigned char>(c) < 0x20) {
                        char buf[8];
                        snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                        file << buf;
                    }
                    else file << c;
                }
                file << "\",\"timestamp\":" << m.timestamp << "}";
                temp.pop();
            }
            file << "]";
        } catch (...) {}
    }
    
    void loadFromFile() {
        try {
            std::ifstream file(queue_file_);
            if (!file) return;
            
            std::string content((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
            if (content.empty() || content == "[]") return;
            
            JsonValue arr = JsonParser::parse(content);
            if (arr.isArray()) {
                for (const auto& item : arr.asArray()) {
                    if (item.isObject()) {
                        auto& obj = item.asObject();
                        QueuedMessage msg;
                        msg.action = obj.count("action") ? obj.at("action").asString() : "";
                        msg.target_id = obj.count("target_id") ? obj.at("target_id").asInt() : 0;
                        msg.message = obj.count("message") ? obj.at("message").asString() : "";
                        msg.timestamp = obj.count("timestamp") ? obj.at("timestamp").asInt() : 0;
                        if (!msg.action.empty() && msg.target_id > 0 && !msg.message.empty()) {
                            queue_.push(msg);
                        }
                    }
                }
                LOG_INFO("[MessageQueue] Loaded " + std::to_string(queue_.size()) + " pending messages");
            }
        } catch (const std::exception& e) {
            LOG_ERROR("[MessageQueue] Load failed: " + std::string(e.what()));
        }
    }
    
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<QueuedMessage> queue_;
    std::atomic<bool> running_;
    std::thread worker_thread_;
    std::string queue_file_;
    SendCallback send_group_callback_;
    SendCallback send_private_callback_;
};

}
