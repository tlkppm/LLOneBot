#pragma once
#include <string>
#include <map>
#include <vector>
#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>
#include "Logger.h"

namespace LCHBOT {

class Calendar {
public:
    static Calendar& instance() {
        static Calendar inst;
        return inst;
    }
    
    void initialize(const std::string& config_path = "config/holidays.json") {
        loadHolidays(config_path);
        LOG_INFO("[Calendar] Loaded " + std::to_string(holidays_.size()) + " holidays");
    }
    
    std::string getZodiac(int year) {
        const char* zodiac[] = {
            "鼠", "牛", "虎", "兔",
            "龙", "蛇", "马", "羊",
            "猴", "鸡", "狗", "猪"
        };
        int idx = (year - 4) % 12;
        if (idx < 0) idx += 12;
        return zodiac[idx];
    }
    
    std::string getHolidayInfo(int year, int month, int day) {
        std::string key = std::to_string(year) + "-" + std::to_string(month) + "-" + std::to_string(day);
        auto it = holidays_.find(key);
        if (it != holidays_.end()) return it->second;
        
        std::string fixed_key = std::to_string(month) + "-" + std::to_string(day);
        it = holidays_.find(fixed_key);
        if (it != holidays_.end()) return it->second;
        
        return "";
    }
    
    std::string getFullDateInfo(int offset_days = 0) {
        auto now = std::chrono::system_clock::now();
        now += std::chrono::hours(24 * offset_days);
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf;
        localtime_s(&tm_buf, &t);
        
        int year = tm_buf.tm_year + 1900;
        int month = tm_buf.tm_mon + 1;
        int day = tm_buf.tm_mday;
        int wday = tm_buf.tm_wday;
        
        const char* weekdays[] = {
            "星期日", "星期一", "星期二", "星期三",
            "星期四", "星期五", "星期六"
        };
        
        std::string holiday = getHolidayInfo(year, month, day);
        
        std::string result;
        result += std::to_string(year) + "年" + std::to_string(month) + "月" + std::to_string(day) + "日 ";
        result += weekdays[wday];
        if (!holiday.empty()) {
            result += " (" + holiday + ")";
        }
        return result;
    }
    
    std::string buildCalendarPrompt() {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf;
        localtime_s(&tm_buf, &t);
        int year = tm_buf.tm_year + 1900;
        
        std::string prompt;
        prompt += "当前年份: " + std::to_string(year) + "年(" + getZodiac(year) + "年)\n";
        prompt += "今天: " + getFullDateInfo(0) + "\n";
        auto important = getKeyHolidays(year);
        for (const auto& h : important) {
            prompt += h + "\n";
        }
        
        return prompt;
    }
    
    std::vector<std::string> getKeyHolidays(int year) {
        std::vector<std::string> result;
        std::vector<std::string> key_names = {
            "除夕", "春节", "元宵节",
            "清明节", "端午节",
            "中秋节", "国庆日"
        };
        
        std::string prefix = std::to_string(year) + "-";
        for (const auto& [key, name] : holidays_) {
            if (key.size() > prefix.size() && key.substr(0, prefix.size()) == prefix) {
                for (const auto& kn : key_names) {
                    if (name == kn) {
                        std::string date_part = key.substr(prefix.size());
                        size_t dash = date_part.find("-");
                        if (dash != std::string::npos) {
                            std::string month = date_part.substr(0, dash);
                            std::string day = date_part.substr(dash + 1);
                            result.push_back(name + ": " + month + "月" + day + "日");
                            LOG_INFO("[Calendar] Key holiday: " + name + " -> " + month + "/" + day);
                        }
                        break;
                    }
                }
            }
        }
        return result;
    }
    
    std::string queryHoliday(const std::string& name) {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf;
        localtime_s(&tm_buf, &t);
        int year = tm_buf.tm_year + 1900;
        std::string prefix = std::to_string(year) + "-";
        
        std::map<std::string, int> month_map = {
            {"一月", 1}, {"二月", 2}, {"三月", 3},
            {"四月", 4}, {"五月", 5}, {"六月", 6},
            {"七月", 7}, {"八月", 8}, {"九月", 9},
            {"十月", 10}, {"十一月", 11}, {"十二月", 12},
            {"1月", 1}, {"2月", 2}, {"3月", 3}, {"4月", 4},
            {"5月", 5}, {"6月", 6}, {"7月", 7}, {"8月", 8},
            {"9月", 9}, {"10月", 10}, {"11月", 11}, {"12月", 12}
        };
        
        int query_month = 0;
        for (const auto& [mname, mnum] : month_map) {
            if (name.find(mname) != std::string::npos) {
                query_month = mnum;
                break;
            }
        }
        
        if (query_month > 0) {
            std::string result;
            for (const auto& [key, holiday_name] : holidays_) {
                if (key.size() > prefix.size() && key.substr(0, prefix.size()) == prefix) {
                    std::string date_part = key.substr(prefix.size());
                    size_t dash = date_part.find("-");
                    if (dash != std::string::npos) {
                        int m = std::stoi(date_part.substr(0, dash));
                        if (m == query_month) {
                            std::string day = date_part.substr(dash + 1);
                            result += std::to_string(m) + "月" + day + "日: " + holiday_name + "\n";
                        }
                    }
                }
            }
            if (!result.empty()) return result;
        }
        
        for (const auto& [key, holiday_name] : holidays_) {
            if (holiday_name.find(name) != std::string::npos || name.find(holiday_name) != std::string::npos) {
                if (key.size() > prefix.size() && key.substr(0, prefix.size()) == prefix) {
                    std::string date_part = key.substr(prefix.size());
                    size_t dash = date_part.find("-");
                    if (dash != std::string::npos) {
                        std::string month = date_part.substr(0, dash);
                        std::string day = date_part.substr(dash + 1);
                        return holiday_name + ": " + std::to_string(year) + "年" + month + "月" + day + "日";
                    }
                }
            }
        }
        return "未找到" + name + "的日期信息";
    }
    
private:
    Calendar() {}
    
    void loadHolidays(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            LOG_WARN("[Calendar] Cannot open " + path);
            return;
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string json = buffer.str();
        file.close();
        
        parseSection(json, "fixed", "");
        parseSection(json, "2025", "2025-");
        parseSection(json, "2026", "2026-");
        parseSection(json, "solar_terms_2026", "2026-");
        
        for (const auto& [k, v] : holidays_) {
            if (v.find("春节") != std::string::npos || 
                v.find("除夕") != std::string::npos) {
                LOG_INFO("[Calendar] Loaded: " + k + " -> " + v);
            }
        }
    }
    
    void parseSection(const std::string& json, const std::string& section, const std::string& prefix) {
        std::string search_key = "\"" + section + "\":";
        size_t pos = json.find(search_key);
        if (pos == std::string::npos) {
            search_key = "\"" + section + "\" :";
            pos = json.find(search_key);
        }
        if (pos == std::string::npos) return;
        
        size_t start = json.find("{", pos);
        if (start == std::string::npos) return;
        
        int depth = 1;
        size_t end = start + 1;
        while (end < json.size() && depth > 0) {
            if (json[end] == '{') depth++;
            else if (json[end] == '}') depth--;
            end++;
        }
        
        std::string block = json.substr(start, end - start);
        
        size_t p = 0;
        while ((p = block.find("\"", p)) != std::string::npos) {
            size_t key_start = p + 1;
            size_t key_end = block.find("\"", key_start);
            if (key_end == std::string::npos) break;
            
            std::string key = block.substr(key_start, key_end - key_start);
            
            size_t val_start = block.find("\"", key_end + 1);
            if (val_start == std::string::npos) break;
            val_start++;
            size_t val_end = block.find("\"", val_start);
            if (val_end == std::string::npos) break;
            
            std::string val = block.substr(val_start, val_end - val_start);
            
            if (key.find("-") != std::string::npos && !val.empty()) {
                holidays_[prefix + key] = val;
            }
            
            p = val_end + 1;
        }
    }
    
    void parseSolarTerms(const std::string& json, const std::string& year) {
        size_t pos = json.find("\"solar_terms\"");
        if (pos == std::string::npos) return;
        
        size_t year_pos = json.find("\"" + year + "\"", pos);
        if (year_pos == std::string::npos) return;
        
        size_t start = json.find("{", year_pos);
        if (start == std::string::npos) return;
        
        int depth = 1;
        size_t end = start + 1;
        while (end < json.size() && depth > 0) {
            if (json[end] == '{') depth++;
            else if (json[end] == '}') depth--;
            end++;
        }
        
        std::string block = json.substr(start, end - start);
        
        size_t p = 0;
        while ((p = block.find("\"", p)) != std::string::npos) {
            size_t key_start = p + 1;
            size_t key_end = block.find("\"", key_start);
            if (key_end == std::string::npos) break;
            
            std::string key = block.substr(key_start, key_end - key_start);
            
            size_t val_start = block.find("\"", key_end + 1);
            if (val_start == std::string::npos) break;
            val_start++;
            size_t val_end = block.find("\"", val_start);
            if (val_end == std::string::npos) break;
            
            std::string val = block.substr(val_start, val_end - val_start);
            
            if (key.find("-") != std::string::npos && !val.empty()) {
                holidays_[year + "-" + key] = val;
            }
            
            p = val_end + 1;
        }
    }
    
    std::map<std::string, std::string> holidays_;
};

}
