#pragma once

#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <sstream>
#include <functional>
#include <memory>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#endif

namespace LCHBOT {

enum class MetricType {
    COUNTER,
    GAUGE,
    HISTOGRAM,
    SUMMARY
};

struct MetricLabel {
    std::string name;
    std::string value;
};

struct HistogramBucket {
    double le;
    int64_t count = 0;
};

class Counter {
public:
    Counter(const std::string& name, const std::string& help) : name_(name), help_(help), value_(0) {}
    
    void inc(int64_t delta = 1) { value_ += delta; }
    int64_t get() const { return value_.load(); }
    
    std::string getName() const { return name_; }
    std::string getHelp() const { return help_; }
    
private:
    std::string name_;
    std::string help_;
    std::atomic<int64_t> value_;
};

class Gauge {
public:
    Gauge(const std::string& name, const std::string& help) : name_(name), help_(help), value_(0) {}
    
    void set(double val) { value_ = val; }
    void inc(double delta = 1) { value_ += delta; }
    void dec(double delta = 1) { value_ -= delta; }
    double get() const { return value_.load(); }
    
    std::string getName() const { return name_; }
    std::string getHelp() const { return help_; }
    
private:
    std::string name_;
    std::string help_;
    std::atomic<double> value_;
};

class Histogram {
public:
    Histogram(const std::string& name, const std::string& help, 
              const std::vector<double>& buckets = {0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1, 2.5, 5, 10})
        : name_(name), help_(help), count_(0), sum_(0) {
        for (double le : buckets) {
            buckets_.push_back({le, {0}});
        }
    }
    
    void observe(double value) {
        count_++;
        sum_ += value;
        std::lock_guard<std::mutex> lock(bucket_mutex_);
        for (auto& bucket : buckets_) {
            if (value <= bucket.le) {
                bucket.count++;
            }
        }
    }
    
    std::string getName() const { return name_; }
    std::string getHelp() const { return help_; }
    int64_t getCount() const { return count_.load(); }
    double getSum() const { return sum_.load(); }
    const std::vector<HistogramBucket>& getBuckets() const { return buckets_; }
    
private:
    std::string name_;
    std::string help_;
    std::vector<HistogramBucket> buckets_;
    std::atomic<int64_t> count_;
    std::atomic<double> sum_;
    mutable std::mutex bucket_mutex_;
};

class LabeledCounter {
public:
    LabeledCounter(const std::string& name, const std::string& help, const std::vector<std::string>& label_names)
        : name_(name), help_(help), label_names_(label_names) {}
    
    void inc(const std::vector<std::string>& label_values, int64_t delta = 1) {
        std::string key = makeKey(label_values);
        std::lock_guard<std::mutex> lock(mutex_);
        values_[key] += delta;
        if (labels_.find(key) == labels_.end()) {
            labels_[key] = label_values;
        }
    }
    
    std::string getName() const { return name_; }
    std::string getHelp() const { return help_; }
    const std::vector<std::string>& getLabelNames() const { return label_names_; }
    
    std::map<std::string, std::pair<std::vector<std::string>, int64_t>> getAll() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::map<std::string, std::pair<std::vector<std::string>, int64_t>> result;
        for (const auto& [key, val] : values_) {
            result[key] = {labels_.at(key), val.load()};
        }
        return result;
    }
    
private:
    std::string makeKey(const std::vector<std::string>& values) const {
        std::string key;
        for (const auto& v : values) key += v + "|";
        return key;
    }
    
    std::string name_;
    std::string help_;
    std::vector<std::string> label_names_;
    std::map<std::string, std::atomic<int64_t>> values_;
    std::map<std::string, std::vector<std::string>> labels_;
    mutable std::mutex mutex_;
};

class MetricsExporter {
public:
    static MetricsExporter& instance() {
        static MetricsExporter inst;
        return inst;
    }
    
    void initialize() {
        messages_total_ = std::make_unique<LabeledCounter>(
            "lchbot_messages_total", "Total messages processed", std::vector<std::string>{"type", "group"});
        
        ai_requests_total_ = std::make_unique<LabeledCounter>(
            "lchbot_ai_requests_total", "Total AI API requests", std::vector<std::string>{"model", "status"});
        
        ai_latency_ = std::make_unique<Histogram>(
            "lchbot_ai_latency_seconds", "AI request latency",
            std::vector<double>{0.1, 0.5, 1, 2, 5, 10, 30, 60});
        
        plugin_executions_ = std::make_unique<LabeledCounter>(
            "lchbot_plugin_executions_total", "Plugin execution count", std::vector<std::string>{"plugin", "status"});
        
        active_connections_ = std::make_unique<Gauge>(
            "lchbot_active_connections", "Number of active WebSocket connections");
        
        memory_usage_ = std::make_unique<Gauge>(
            "lchbot_memory_bytes", "Memory usage in bytes");
        
        uptime_ = std::make_unique<Counter>(
            "lchbot_uptime_seconds", "Bot uptime in seconds");
        
        rate_limited_ = std::make_unique<LabeledCounter>(
            "lchbot_rate_limited_total", "Rate limited requests", std::vector<std::string>{"key"});
        
        errors_total_ = std::make_unique<LabeledCounter>(
            "lchbot_errors_total", "Total errors", std::vector<std::string>{"module", "code"});
        
        start_time_ = std::chrono::steady_clock::now();
    }
    
    void recordMessage(const std::string& type, int64_t group_id) {
        messages_total_->inc({type, std::to_string(group_id)});
    }
    
    void recordAIRequest(const std::string& model, bool success, double latency_seconds) {
        ai_requests_total_->inc({model, success ? "success" : "failure"});
        ai_latency_->observe(latency_seconds);
    }
    
    void recordPluginExecution(const std::string& plugin, bool success) {
        plugin_executions_->inc({plugin, success ? "success" : "failure"});
    }
    
    void recordError(const std::string& module, int code) {
        errors_total_->inc({module, std::to_string(code)});
    }
    
    void recordRateLimited(const std::string& key) {
        rate_limited_->inc({key});
    }
    
    void setActiveConnections(int count) {
        active_connections_->set(count);
    }
    
    void updateMemoryUsage() {
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS_EX pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
            memory_usage_->set(static_cast<double>(pmc.WorkingSetSize));
        }
#endif
    }
    
    std::string exportPrometheus() {
        std::stringstream ss;
        
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_time_).count();
        
        ss << "# HELP lchbot_uptime_seconds Bot uptime in seconds\n";
        ss << "# TYPE lchbot_uptime_seconds counter\n";
        ss << "lchbot_uptime_seconds " << elapsed << "\n\n";
        
        ss << formatGauge(*active_connections_);
        
        updateMemoryUsage();
        ss << formatGauge(*memory_usage_);
        
        ss << formatLabeledCounter(*messages_total_);
        ss << formatLabeledCounter(*ai_requests_total_);
        ss << formatHistogram(*ai_latency_);
        ss << formatLabeledCounter(*plugin_executions_);
        ss << formatLabeledCounter(*rate_limited_);
        ss << formatLabeledCounter(*errors_total_);
        
        for (const auto& [name, collector] : custom_collectors_) {
            ss << collector();
        }
        
        return ss.str();
    }
    
    void addCustomCollector(const std::string& name, std::function<std::string()> collector) {
        std::lock_guard<std::mutex> lock(mutex_);
        custom_collectors_[name] = collector;
    }
    
    class Timer {
    public:
        Timer(Histogram* histogram) : histogram_(histogram), 
            start_(std::chrono::steady_clock::now()) {}
        
        ~Timer() {
            auto elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - start_).count();
            histogram_->observe(elapsed);
        }
        
        double elapsed() const {
            return std::chrono::duration<double>(
                std::chrono::steady_clock::now() - start_).count();
        }
        
    private:
        Histogram* histogram_;
        std::chrono::steady_clock::time_point start_;
    };
    
    Timer timeAIRequest() {
        return Timer(ai_latency_.get());
    }

private:
    MetricsExporter() = default;
    
    std::string formatGauge(const Gauge& gauge) {
        std::stringstream ss;
        ss << "# HELP " << gauge.getName() << " " << gauge.getHelp() << "\n";
        ss << "# TYPE " << gauge.getName() << " gauge\n";
        ss << gauge.getName() << " " << gauge.get() << "\n\n";
        return ss.str();
    }
    
    std::string formatLabeledCounter(const LabeledCounter& counter) {
        std::stringstream ss;
        ss << "# HELP " << counter.getName() << " " << counter.getHelp() << "\n";
        ss << "# TYPE " << counter.getName() << " counter\n";
        
        auto values = counter.getAll();
        auto& label_names = counter.getLabelNames();
        
        for (const auto& [key, data] : values) {
            ss << counter.getName() << "{";
            for (size_t i = 0; i < label_names.size(); i++) {
                if (i > 0) ss << ",";
                ss << label_names[i] << "=\"" << data.first[i] << "\"";
            }
            ss << "} " << data.second << "\n";
        }
        ss << "\n";
        return ss.str();
    }
    
    std::string formatHistogram(const Histogram& histogram) {
        std::stringstream ss;
        ss << "# HELP " << histogram.getName() << " " << histogram.getHelp() << "\n";
        ss << "# TYPE " << histogram.getName() << " histogram\n";
        
        int64_t cumulative = 0;
        for (const auto& bucket : histogram.getBuckets()) {
            cumulative += bucket.count;
            ss << histogram.getName() << "_bucket{le=\"" << bucket.le << "\"} " << cumulative << "\n";
        }
        ss << histogram.getName() << "_bucket{le=\"+Inf\"} " << histogram.getCount() << "\n";
        ss << histogram.getName() << "_sum " << histogram.getSum() << "\n";
        ss << histogram.getName() << "_count " << histogram.getCount() << "\n\n";
        
        return ss.str();
    }
    
    std::unique_ptr<LabeledCounter> messages_total_;
    std::unique_ptr<LabeledCounter> ai_requests_total_;
    std::unique_ptr<Histogram> ai_latency_;
    std::unique_ptr<LabeledCounter> plugin_executions_;
    std::unique_ptr<Gauge> active_connections_;
    std::unique_ptr<Gauge> memory_usage_;
    std::unique_ptr<Counter> uptime_;
    std::unique_ptr<LabeledCounter> rate_limited_;
    std::unique_ptr<LabeledCounter> errors_total_;
    
    std::chrono::steady_clock::time_point start_time_;
    std::map<std::string, std::function<std::string()>> custom_collectors_;
    std::mutex mutex_;
};

#define METRICS_TIMER(name) MetricsExporter::Timer _metrics_timer_##name = MetricsExporter::instance().timeAIRequest()

}
