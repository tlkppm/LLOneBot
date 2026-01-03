#pragma once

#include <string>
#include <vector>
#include <map>
#include <stack>
#include <mutex>
#include <atomic>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <functional>

namespace LCHBOT {

struct SpanContext {
    std::string trace_id;
    std::string span_id;
    std::string parent_span_id;
    std::string operation_name;
    int64_t start_time_us;
    int64_t end_time_us;
    std::map<std::string, std::string> tags;
    std::vector<std::pair<int64_t, std::string>> logs;
    bool sampled = true;
};

class TraceSystem {
public:
    static TraceSystem& instance() {
        static TraceSystem inst;
        return inst;
    }
    
    void initialize(double sample_rate = 1.0, const std::string& service_name = "lchbot") {
        sample_rate_ = sample_rate;
        service_name_ = service_name;
        
        std::random_device rd;
        rng_.seed(rd());
        
        initialized_ = true;
    }
    
    void setSampleRate(double rate) {
        sample_rate_ = rate;
    }
    
    void setExporter(std::function<void(const SpanContext&)> exporter) {
        std::lock_guard<std::mutex> lock(mutex_);
        exporter_ = exporter;
    }
    
    std::string generateTraceId() {
        return generateId(32);
    }
    
    std::string generateSpanId() {
        return generateId(16);
    }
    
    class Span {
    public:
        Span(TraceSystem* system, const std::string& operation_name, 
             const std::string& trace_id = "", const std::string& parent_span_id = "")
            : system_(system), finished_(false) {
            
            ctx_.operation_name = operation_name;
            ctx_.start_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            
            if (trace_id.empty()) {
                ctx_.trace_id = system_->generateTraceId();
            } else {
                ctx_.trace_id = trace_id;
            }
            
            ctx_.span_id = system_->generateSpanId();
            ctx_.parent_span_id = parent_span_id;
            
            std::uniform_real_distribution<> dist(0.0, 1.0);
            ctx_.sampled = dist(system_->rng_) < system_->sample_rate_;
            
            ctx_.tags["service.name"] = system_->service_name_;
        }
        
        ~Span() {
            if (!finished_) {
                finish();
            }
        }
        
        Span& setTag(const std::string& key, const std::string& value) {
            ctx_.tags[key] = value;
            return *this;
        }
        
        Span& setTag(const std::string& key, int64_t value) {
            ctx_.tags[key] = std::to_string(value);
            return *this;
        }
        
        Span& log(const std::string& message) {
            int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            ctx_.logs.emplace_back(now, message);
            return *this;
        }
        
        Span& setError(bool is_error = true) {
            ctx_.tags["error"] = is_error ? "true" : "false";
            return *this;
        }
        
        Span& setErrorMessage(const std::string& message) {
            ctx_.tags["error"] = "true";
            ctx_.tags["error.message"] = message;
            return *this;
        }
        
        void finish() {
            if (finished_) return;
            finished_ = true;
            
            ctx_.end_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            
            if (ctx_.sampled) {
                system_->recordSpan(ctx_);
            }
        }
        
        std::string getTraceId() const { return ctx_.trace_id; }
        std::string getSpanId() const { return ctx_.span_id; }
        const SpanContext& context() const { return ctx_; }
        
        Span createChild(const std::string& operation_name) {
            return Span(system_, operation_name, ctx_.trace_id, ctx_.span_id);
        }
        
        double elapsedMs() const {
            int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            return (now - ctx_.start_time_us) / 1000.0;
        }
        
    private:
        TraceSystem* system_;
        SpanContext ctx_;
        bool finished_;
    };
    
    Span startSpan(const std::string& operation_name) {
        return Span(this, operation_name);
    }
    
    Span startSpan(const std::string& operation_name, const std::string& trace_id) {
        return Span(this, operation_name, trace_id);
    }
    
    Span continueSpan(const std::string& operation_name, 
                      const std::string& trace_id, 
                      const std::string& parent_span_id) {
        return Span(this, operation_name, trace_id, parent_span_id);
    }
    
    void recordSpan(const SpanContext& ctx) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        recent_spans_.push_back(ctx);
        if (recent_spans_.size() > max_recent_spans_) {
            recent_spans_.erase(recent_spans_.begin());
        }
        
        if (exporter_) {
            exporter_(ctx);
        }
        
        total_spans_++;
        total_duration_us_ += (ctx.end_time_us - ctx.start_time_us);
    }
    
    std::vector<SpanContext> getRecentSpans(size_t limit = 100) const {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t count = (limit < recent_spans_.size()) ? limit : recent_spans_.size();
        return std::vector<SpanContext>(recent_spans_.end() - count, recent_spans_.end());
    }
    
    std::vector<SpanContext> getSpansByTraceId(const std::string& trace_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<SpanContext> result;
        for (const auto& span : recent_spans_) {
            if (span.trace_id == trace_id) {
                result.push_back(span);
            }
        }
        return result;
    }
    
    std::string formatSpanJson(const SpanContext& ctx) const {
        std::stringstream ss;
        ss << "{";
        ss << "\"traceId\":\"" << ctx.trace_id << "\"";
        ss << ",\"spanId\":\"" << ctx.span_id << "\"";
        if (!ctx.parent_span_id.empty()) {
            ss << ",\"parentSpanId\":\"" << ctx.parent_span_id << "\"";
        }
        ss << ",\"operationName\":\"" << ctx.operation_name << "\"";
        ss << ",\"startTime\":" << ctx.start_time_us;
        ss << ",\"duration\":" << (ctx.end_time_us - ctx.start_time_us);
        
        ss << ",\"tags\":{";
        bool first = true;
        for (const auto& [k, v] : ctx.tags) {
            if (!first) ss << ",";
            ss << "\"" << k << "\":\"" << v << "\"";
            first = false;
        }
        ss << "}";
        
        if (!ctx.logs.empty()) {
            ss << ",\"logs\":[";
            first = true;
            for (const auto& [ts, msg] : ctx.logs) {
                if (!first) ss << ",";
                ss << "{\"timestamp\":" << ts << ",\"message\":\"" << msg << "\"}";
                first = false;
            }
            ss << "]";
        }
        
        ss << "}";
        return ss.str();
    }
    
    std::string exportJaegerFormat() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::stringstream ss;
        ss << "{\"data\":[{\"traceID\":\"mixed\",\"spans\":[";
        
        bool first = true;
        for (const auto& span : recent_spans_) {
            if (!first) ss << ",";
            ss << formatSpanJson(span);
            first = false;
        }
        
        ss << "],\"processes\":{\"p1\":{\"serviceName\":\"" << service_name_ << "\"}}}]}";
        return ss.str();
    }
    
    struct TraceStats {
        int64_t total_spans;
        double avg_duration_ms;
        int64_t errors;
    };
    
    TraceStats getStats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        TraceStats stats;
        stats.total_spans = total_spans_;
        stats.avg_duration_ms = total_spans_ > 0 ? (total_duration_us_ / total_spans_) / 1000.0 : 0;
        
        stats.errors = 0;
        for (const auto& span : recent_spans_) {
            auto it = span.tags.find("error");
            if (it != span.tags.end() && it->second == "true") {
                stats.errors++;
            }
        }
        return stats;
    }

private:
    TraceSystem() = default;
    
    std::string generateId(size_t length) {
        static const char hex[] = "0123456789abcdef";
        std::string result;
        result.reserve(length);
        
        std::uniform_int_distribution<> dist(0, 15);
        for (size_t i = 0; i < length; i++) {
            result += hex[dist(rng_)];
        }
        return result;
    }
    
    std::string service_name_ = "lchbot";
    double sample_rate_ = 1.0;
    std::mt19937 rng_;
    
    std::vector<SpanContext> recent_spans_;
    size_t max_recent_spans_ = 10000;
    
    std::function<void(const SpanContext&)> exporter_;
    
    std::atomic<int64_t> total_spans_{0};
    std::atomic<int64_t> total_duration_us_{0};
    
    mutable std::mutex mutex_;
    bool initialized_ = false;
};

class ScopedSpan {
public:
    ScopedSpan(const std::string& operation_name) 
        : span_(TraceSystem::instance().startSpan(operation_name)) {}
    
    ScopedSpan(const std::string& operation_name, const std::string& trace_id)
        : span_(TraceSystem::instance().startSpan(operation_name, trace_id)) {}
    
    TraceSystem::Span& span() { return span_; }
    std::string traceId() const { return span_.getTraceId(); }
    std::string spanId() const { return span_.getSpanId(); }
    
private:
    TraceSystem::Span span_;
};

#define TRACE_SPAN(name) ScopedSpan _trace_span_##__LINE__(name)
#define TRACE_SPAN_WITH_ID(name, trace_id) ScopedSpan _trace_span_##__LINE__(name, trace_id)

}
