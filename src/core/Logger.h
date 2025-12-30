#pragma once

#include <string>
#include <fstream>
#include <iostream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <queue>
#include <thread>
#include <atomic>
#include <condition_variable>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace LCHBOT {

enum class LogLevel {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    Fatal = 5,
    Message = 6
};

class Logger {
public:
    static Logger& instance() {
        static Logger inst;
        return inst;
    }
    
    void init(const std::string& log_dir, const std::string& level, 
              bool console_output, bool file_output,
              uint32_t max_file_size, uint32_t max_files) {
        log_dir_ = log_dir;
        console_output_ = console_output;
        file_output_ = file_output;
        max_file_size_ = max_file_size;
        max_files_ = max_files;
        
        if (level == "trace") level_ = LogLevel::Trace;
        else if (level == "debug") level_ = LogLevel::Debug;
        else if (level == "info") level_ = LogLevel::Info;
        else if (level == "warn") level_ = LogLevel::Warn;
        else if (level == "error") level_ = LogLevel::Error;
        else if (level == "fatal") level_ = LogLevel::Fatal;
        else level_ = LogLevel::Info;
        
        if (file_output_) {
            std::filesystem::create_directories(log_dir_);
            openLogFile();
        }
        
        running_ = true;
        worker_ = std::thread(&Logger::processLogs, this);
    }
    
    void shutdown() {
        running_ = false;
        cv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
        if (file_.is_open()) {
            file_.close();
        }
    }
    
    ~Logger() {
        shutdown();
    }
    
    template<typename... Args>
    void log(LogLevel level, const char* file, int line, const std::string& fmt, Args&&... args) {
        if (level < level_) return;
        
        std::string message = format(fmt, std::forward<Args>(args)...);
        
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        std::ostringstream oss;
        struct tm time_info;
#ifdef _WIN32
        localtime_s(&time_info, &time);
#else
        localtime_r(&time, &time_info);
#endif
        oss << std::put_time(&time_info, "%Y-%m-%d %H:%M:%S");
        oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
        oss << " [" << levelToString(level) << "] ";
        oss << "[" << extractFilename(file) << ":" << line << "] ";
        oss << message;
        
        std::lock_guard<std::mutex> lock(queue_mutex_);
        log_queue_.push({level, oss.str()});
        cv_.notify_one();
    }
    
    void trace(const char* file, int line, const std::string& msg) { log(LogLevel::Trace, file, line, msg); }
    void debug(const char* file, int line, const std::string& msg) { log(LogLevel::Debug, file, line, msg); }
    void info(const char* file, int line, const std::string& msg) { log(LogLevel::Info, file, line, msg); }
    void warn(const char* file, int line, const std::string& msg) { log(LogLevel::Warn, file, line, msg); }
    void error(const char* file, int line, const std::string& msg) { log(LogLevel::Error, file, line, msg); }
    void fatal(const char* file, int line, const std::string& msg) { log(LogLevel::Fatal, file, line, msg); }
    void message(const std::string& msg) { logMessage(msg); }
    
    void logMessage(const std::string& message) {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        std::ostringstream oss;
        struct tm time_info;
#ifdef _WIN32
        localtime_s(&time_info, &time);
#else
        localtime_r(&time, &time_info);
#endif
        oss << std::put_time(&time_info, "%Y-%m-%d %H:%M:%S");
        oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
        oss << " [MSG  ] " << message;
        
        std::lock_guard<std::mutex> lock(queue_mutex_);
        log_queue_.push({LogLevel::Message, oss.str()});
        cv_.notify_one();
    }
    
private:
    Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    
    struct LogEntry {
        LogLevel level;
        std::string message;
    };
    
    void processLogs() {
        while (running_ || !log_queue_.empty()) {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            cv_.wait(lock, [this] { return !log_queue_.empty() || !running_; });
            
            while (!log_queue_.empty()) {
                auto entry = std::move(log_queue_.front());
                log_queue_.pop();
                lock.unlock();
                
                writeLog(entry);
                
                lock.lock();
            }
        }
    }
    
    void writeLog(const LogEntry& entry) {
        if (console_output_) {
#ifdef _WIN32
            HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
            WORD color = getWindowsColor(entry.level);
            SetConsoleTextAttribute(hConsole, color);
            
            int wlen = MultiByteToWideChar(CP_UTF8, 0, entry.message.c_str(), -1, nullptr, 0);
            std::wstring wmsg(wlen, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, entry.message.c_str(), -1, wmsg.data(), wlen);
            
            DWORD written;
            WriteConsoleW(hConsole, wmsg.c_str(), (DWORD)wmsg.size() - 1, &written, nullptr);
            WriteConsoleW(hConsole, L"\n", 1, &written, nullptr);
            
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
#else
            std::cout << getColorCode(entry.level) << entry.message << "\033[0m" << std::endl;
#endif
        }
        
        if (file_output_ && file_.is_open()) {
            file_ << entry.message << std::endl;
            current_file_size_ += entry.message.size() + 1;
            
            if (current_file_size_ >= max_file_size_) {
                rotateLogFile();
            }
        }
    }
    
    WORD getWindowsColor(LogLevel level) {
#ifdef _WIN32
        switch (level) {
            case LogLevel::Trace: return FOREGROUND_INTENSITY;
            case LogLevel::Debug: return FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
            case LogLevel::Info:  return FOREGROUND_GREEN | FOREGROUND_INTENSITY;
            case LogLevel::Warn:  return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
            case LogLevel::Error: return FOREGROUND_RED | FOREGROUND_INTENSITY;
            case LogLevel::Fatal: return FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
            case LogLevel::Message: return FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
            default: return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
        }
#else
        return 0;
#endif
    }
    
    void openLogFile() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        
        std::ostringstream oss;
        struct tm time_info;
#ifdef _WIN32
        localtime_s(&time_info, &time);
#else
        localtime_r(&time, &time_info);
#endif
        oss << log_dir_ << "/lchbot_" << std::put_time(&time_info, "%Y%m%d_%H%M%S") << ".log";
        
        current_log_file_ = oss.str();
        file_.open(current_log_file_, std::ios::app);
        current_file_size_ = std::filesystem::exists(current_log_file_) ? 
            std::filesystem::file_size(current_log_file_) : 0;
    }
    
    void rotateLogFile() {
        if (file_.is_open()) {
            file_.close();
        }
        
        std::vector<std::filesystem::path> log_files;
        for (const auto& entry : std::filesystem::directory_iterator(log_dir_)) {
            if (entry.path().extension() == ".log") {
                log_files.push_back(entry.path());
            }
        }
        
        std::sort(log_files.begin(), log_files.end());
        
        while (log_files.size() >= max_files_) {
            std::filesystem::remove(log_files.front());
            log_files.erase(log_files.begin());
        }
        
        openLogFile();
    }
    
    const char* levelToString(LogLevel level) {
        switch (level) {
            case LogLevel::Trace: return "TRACE";
            case LogLevel::Debug: return "DEBUG";
            case LogLevel::Info:  return "INFO ";
            case LogLevel::Warn:  return "WARN ";
            case LogLevel::Error: return "ERROR";
            case LogLevel::Fatal: return "FATAL";
            case LogLevel::Message: return "MSG  ";
            default: return "UNKN ";
        }
    }
    
    const char* getColorCode(LogLevel level) {
        switch (level) {
            case LogLevel::Trace: return "\033[90m";
            case LogLevel::Debug: return "\033[36m";
            case LogLevel::Info:  return "\033[32m";
            case LogLevel::Warn:  return "\033[33m";
            case LogLevel::Error: return "\033[31m";
            case LogLevel::Fatal: return "\033[35m";
            default: return "\033[0m";
        }
    }
    
    std::string extractFilename(const char* path) {
        std::string p(path);
        auto pos = p.find_last_of("/\\");
        return pos != std::string::npos ? p.substr(pos + 1) : p;
    }
    
    template<typename... Args>
    std::string format(const std::string& fmt, Args&&... args) {
        if constexpr (sizeof...(args) == 0) {
            return fmt;
        } else {
            size_t size = std::snprintf(nullptr, 0, fmt.c_str(), std::forward<Args>(args)...) + 1;
            std::string result(size, '\0');
            std::snprintf(result.data(), size, fmt.c_str(), std::forward<Args>(args)...);
            result.pop_back();
            return result;
        }
    }
    
    std::string log_dir_;
    LogLevel level_ = LogLevel::Info;
    bool console_output_ = true;
    bool file_output_ = true;
    uint32_t max_file_size_ = 10485760;
    uint32_t max_files_ = 10;
    
    std::ofstream file_;
    std::string current_log_file_;
    size_t current_file_size_ = 0;
    
    std::queue<LogEntry> log_queue_;
    std::mutex queue_mutex_;
    std::condition_variable cv_;
    std::thread worker_;
    std::atomic<bool> running_{false};
};

#define LOG_TRACE(msg) LCHBOT::Logger::instance().trace(__FILE__, __LINE__, msg)
#define LOG_DEBUG(msg) LCHBOT::Logger::instance().debug(__FILE__, __LINE__, msg)
#define LOG_INFO(msg) LCHBOT::Logger::instance().info(__FILE__, __LINE__, msg)
#define LOG_WARN(msg) LCHBOT::Logger::instance().warn(__FILE__, __LINE__, msg)
#define LOG_ERROR(msg) LCHBOT::Logger::instance().error(__FILE__, __LINE__, msg)
#define LOG_FATAL(msg) LCHBOT::Logger::instance().fatal(__FILE__, __LINE__, msg)
#define LOG_MSG(msg) LCHBOT::Logger::instance().message(msg)

}
