#pragma once

#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <thread>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <filesystem>

namespace LCHBOT {

enum class SLogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERR = 4,
    FATAL = 5
};

struct LogField {
    std::string key;
    std::string value;
    bool is_number = false;
};

struct StructuredLogEntry {
    int64_t timestamp_ms;
    SLogLevel severity;
    std::string message;
    std::string logger_name;
    std::string trace_id;
    std::string span_id;
    std::string file;
    int line;
    std::string function;
    std::thread::id thread_id;
    std::vector<LogField> fields;
};

class StructuredLogger {
public:
    static StructuredLogger& instance() {
        static StructuredLogger inst;
        return inst;
    }
    
    void initialize(const std::string& log_dir = "logs", SLogLevel min_level = SLogLevel::INFO) {
        std::lock_guard<std::mutex> lock(mutex_);
        log_dir_ = log_dir;
        min_level_ = min_level;
        
        std::filesystem::create_directories(log_dir);
        
        running_ = true;
        writer_thread_ = std::thread(&StructuredLogger::writerLoop, this);
        
        openLogFile();
    }
    
    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            running_ = false;
        }
        queue_cv_.notify_all();
        if (writer_thread_.joinable()) {
            writer_thread_.join();
        }
        if (json_file_.is_open()) {
            json_file_.close();
        }
    }
    
    ~StructuredLogger() {
        shutdown();
    }
    
    void setMinLevel(SLogLevel level) {
        min_level_ = level;
    }
    
    void setTraceContext(const std::string& trace_id, const std::string& span_id = "") {
        thread_local_trace_id_ = trace_id;
        thread_local_span_id_ = span_id;
    }
    
    void clearTraceContext() {
        thread_local_trace_id_.clear();
        thread_local_span_id_.clear();
    }
    
    class LogBuilder {
    public:
        LogBuilder(StructuredLogger* logger, SLogLevel severity, const std::string& message)
            : logger_(logger), entry_() {
            entry_.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            entry_.severity = severity;
            entry_.message = message;
            entry_.thread_id = std::this_thread::get_id();
            entry_.trace_id = logger_->thread_local_trace_id_;
            entry_.span_id = logger_->thread_local_span_id_;
        }
        
        LogBuilder& withLogger(const std::string& name) {
            entry_.logger_name = name;
            return *this;
        }
        
        LogBuilder& withLocation(const std::string& file, int line, const std::string& func) {
            entry_.file = file;
            entry_.line = line;
            entry_.function = func;
            return *this;
        }
        
        LogBuilder& withField(const std::string& key, const std::string& value) {
            entry_.fields.push_back({key, value, false});
            return *this;
        }
        
        LogBuilder& withField(const std::string& key, int64_t value) {
            entry_.fields.push_back({key, std::to_string(value), true});
            return *this;
        }
        
        LogBuilder& withField(const std::string& key, double value) {
            entry_.fields.push_back({key, std::to_string(value), true});
            return *this;
        }
        
        LogBuilder& withTraceId(const std::string& trace_id) {
            entry_.trace_id = trace_id;
            return *this;
        }
        
        LogBuilder& withSpanId(const std::string& span_id) {
            entry_.span_id = span_id;
            return *this;
        }
        
        void emit() {
            logger_->log(std::move(entry_));
        }
        
        ~LogBuilder() {
            if (!emitted_) {
                emit();
                emitted_ = true;
            }
        }
        
    private:
        StructuredLogger* logger_;
        StructuredLogEntry entry_;
        bool emitted_ = false;
    };
    
    LogBuilder trace(const std::string& message) { return LogBuilder(this, SLogLevel::TRACE, message); }
    LogBuilder debug(const std::string& message) { return LogBuilder(this, SLogLevel::DEBUG, message); }
    LogBuilder info(const std::string& message) { return LogBuilder(this, SLogLevel::INFO, message); }
    LogBuilder warn(const std::string& message) { return LogBuilder(this, SLogLevel::WARN, message); }
    LogBuilder error(const std::string& message) { return LogBuilder(this, SLogLevel::ERR, message); }
    LogBuilder fatal(const std::string& message) { return LogBuilder(this, SLogLevel::FATAL, message); }
    
    void log(StructuredLogEntry entry) {
        if (entry.severity < min_level_) return;
        
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            log_queue_.push(std::move(entry));
        }
        queue_cv_.notify_one();
    }
    
    void addOutputHandler(std::function<void(const std::string&)> handler) {
        std::lock_guard<std::mutex> lock(mutex_);
        output_handlers_.push_back(handler);
    }
    
    std::string severityToString(SLogLevel severity) const {
        switch (severity) {
            case SLogLevel::TRACE: return "TRACE";
            case SLogLevel::DEBUG: return "DEBUG";
            case SLogLevel::INFO: return "INFO";
            case SLogLevel::WARN: return "WARN";
            case SLogLevel::ERR: return "ERROR";
            case SLogLevel::FATAL: return "FATAL";
            default: return "UNKNOWN";
        }
    }

private:
    StructuredLogger() = default;
    
    void openLogFile() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &time);
#else
        localtime_r(&time, &tm_buf);
#endif
        
        char filename[64];
        strftime(filename, sizeof(filename), "%Y-%m-%d.json", &tm_buf);
        
        current_date_ = filename;
        std::string path = log_dir_ + "/" + filename;
        
        if (json_file_.is_open()) {
            json_file_.close();
        }
        
        json_file_.open(path, std::ios::app);
    }
    
    void writerLoop() {
        while (running_ || !log_queue_.empty()) {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] { return !log_queue_.empty() || !running_; });
            
            std::vector<StructuredLogEntry> batch;
            while (!log_queue_.empty() && batch.size() < 100) {
                batch.push_back(std::move(log_queue_.front()));
                log_queue_.pop();
            }
            lock.unlock();
            
            for (const auto& entry : batch) {
                std::string json = formatJson(entry);
                writeToFile(json);
                
                std::lock_guard<std::mutex> handler_lock(mutex_);
                for (const auto& handler : output_handlers_) {
                    handler(json);
                }
            }
        }
    }
    
    std::string formatJson(const StructuredLogEntry& entry) const {
        std::stringstream ss;
        ss << "{";
        ss << "\"timestamp\":" << entry.timestamp_ms;
        ss << ",\"level\":\"" << severityToString(entry.severity) << "\"";
        ss << ",\"message\":\"" << escapeJson(entry.message) << "\"";
        
        if (!entry.logger_name.empty()) {
            ss << ",\"logger\":\"" << entry.logger_name << "\"";
        }
        if (!entry.trace_id.empty()) {
            ss << ",\"trace_id\":\"" << entry.trace_id << "\"";
        }
        if (!entry.span_id.empty()) {
            ss << ",\"span_id\":\"" << entry.span_id << "\"";
        }
        if (!entry.file.empty()) {
            ss << ",\"file\":\"" << entry.file << "\"";
            ss << ",\"line\":" << entry.line;
        }
        if (!entry.function.empty()) {
            ss << ",\"function\":\"" << entry.function << "\"";
        }
        
        ss << ",\"thread\":\"" << entry.thread_id << "\"";
        
        if (!entry.fields.empty()) {
            ss << ",\"fields\":{";
            bool first = true;
            for (const auto& field : entry.fields) {
                if (!first) ss << ",";
                ss << "\"" << field.key << "\":";
                if (field.is_number) {
                    ss << field.value;
                } else {
                    ss << "\"" << escapeJson(field.value) << "\"";
                }
                first = false;
            }
            ss << "}";
        }
        
        ss << "}";
        return ss.str();
    }
    
    std::string escapeJson(const std::string& str) const {
        std::string result;
        for (char c : str) {
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result += c; break;
            }
        }
        return result;
    }
    
    void writeToFile(const std::string& json) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &time);
#else
        localtime_r(&time, &tm_buf);
#endif
        char date[16];
        strftime(date, sizeof(date), "%Y-%m-%d.json", &tm_buf);
        
        if (current_date_ != date) {
            openLogFile();
        }
        
        if (json_file_.is_open()) {
            json_file_ << json << "\n";
            json_file_.flush();
        }
    }
    
    std::string log_dir_;
    SLogLevel min_level_ = SLogLevel::INFO;
    std::ofstream json_file_;
    std::string current_date_;
    
    std::queue<StructuredLogEntry> log_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::thread writer_thread_;
    std::atomic<bool> running_{false};
    
    std::vector<std::function<void(const std::string&)>> output_handlers_;
    std::mutex mutex_;
    
    inline static thread_local std::string thread_local_trace_id_;
    inline static thread_local std::string thread_local_span_id_;
};

#define SLOG_TRACE(msg) StructuredLogger::instance().trace(msg).withLocation(__FILE__, __LINE__, __FUNCTION__)
#define SLOG_DEBUG(msg) StructuredLogger::instance().debug(msg).withLocation(__FILE__, __LINE__, __FUNCTION__)
#define SLOG_INFO(msg) StructuredLogger::instance().info(msg).withLocation(__FILE__, __LINE__, __FUNCTION__)
#define SLOG_WARN(msg) StructuredLogger::instance().warn(msg).withLocation(__FILE__, __LINE__, __FUNCTION__)
#define SLOG_ERROR(msg) StructuredLogger::instance().error(msg).withLocation(__FILE__, __LINE__, __FUNCTION__)
#define SLOG_FATAL(msg) StructuredLogger::instance().fatal(msg).withLocation(__FILE__, __LINE__, __FUNCTION__)

}
