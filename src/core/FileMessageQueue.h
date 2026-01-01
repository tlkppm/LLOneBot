#pragma once
#include <string>
#include <fstream>
#include <sstream>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <filesystem>
#include "../core/JsonParser.h"
#include "../core/Logger.h"

namespace lchbot {

class FileMessageQueue {
public:
    using SendCallback = std::function<void(const std::string&, int64_t)>;
    
    static FileMessageQueue& instance() {
        static FileMessageQueue inst;
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
    
    void start() {
        if (running_) return;
        running_ = true;
        worker_thread_ = std::thread(&FileMessageQueue::workerLoop, this);
        LOG_INFO("[FileMessageQueue] Started monitoring " + queue_file_);
    }
    
    void stop() {
        running_ = false;
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
        LOG_INFO("[FileMessageQueue] Stopped");
    }
    
private:
    FileMessageQueue() : running_(false), queue_file_("data/py_msg_queue.jsonl") {
        std::filesystem::create_directories("data");
    }
    
    ~FileMessageQueue() {
        stop();
    }
    
    void workerLoop() {
        while (running_) {
            try {
                processQueue();
            } catch (const std::exception& e) {
                LOG_ERROR("[FileMessageQueue] Error: " + std::string(e.what()));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    
    void processQueue() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!std::filesystem::exists(queue_file_)) return;
        
        std::ifstream file(queue_file_);
        if (!file) return;
        
        std::vector<std::string> lines;
        std::vector<std::string> failed_lines;
        std::string line;
        
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            lines.push_back(line);
        }
        file.close();
        
        if (lines.empty()) return;
        
        std::ofstream clear_file(queue_file_, std::ios::trunc);
        clear_file.close();
        
        for (const auto& json_line : lines) {
            try {
                LCHBOT::JsonValue msg = LCHBOT::JsonParser::parse(json_line);
                if (!msg.isObject()) {
                    failed_lines.push_back(json_line);
                    continue;
                }
                
                auto& obj = msg.asObject();
                std::string action = obj.count("action") ? obj.at("action").asString() : "";
                int64_t target_id = obj.count("target_id") ? obj.at("target_id").asInt() : 0;
                std::string message = obj.count("message") ? obj.at("message").asString() : "";
                
                if (action.empty() || target_id == 0 || message.empty()) {
                    continue;
                }
                
                bool success = false;
                if (action == "send_group_msg" && send_group_callback_) {
                    send_group_callback_(message, target_id);
                    success = true;
                    LOG_INFO("[FileMessageQueue] Sent group msg to " + std::to_string(target_id) + ", len=" + std::to_string(message.length()));
                } else if (action == "send_private_msg" && send_private_callback_) {
                    send_private_callback_(message, target_id);
                    success = true;
                    LOG_INFO("[FileMessageQueue] Sent private msg to " + std::to_string(target_id) + ", len=" + std::to_string(message.length()));
                }
                
                if (!success) {
                    failed_lines.push_back(json_line);
                }
                
            } catch (const std::exception& e) {
                LOG_ERROR("[FileMessageQueue] Parse error: " + std::string(e.what()) + " for: " + json_line.substr(0, 100));
                failed_lines.push_back(json_line);
            }
        }
        
        if (!failed_lines.empty()) {
            std::ofstream retry_file(queue_file_, std::ios::app);
            for (const auto& fl : failed_lines) {
                retry_file << fl << "\n";
            }
        }
    }
    
    std::mutex mutex_;
    std::atomic<bool> running_;
    std::thread worker_thread_;
    std::string queue_file_;
    SendCallback send_group_callback_;
    SendCallback send_private_callback_;
};

}
