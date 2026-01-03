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
            "\xe9\xbc\xa0", "\xe7\x89\x9b", "\xe8\x99\x8e", "\xe5\x85\x94",
            "\xe9\xbe\x99", "\xe8\x9b\x87", "\xe9\xa9\xac", "\xe7\xbe\x8a",
            "\xe7\x8c\xb4", "\xe9\xb8\xa1", "\xe7\x8b\x97", "\xe7\x8c\xaa"
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
            "\xe6\x98\x9f\xe6\x9c\x9f\xe6\x97\xa5",
            "\xe6\x98\x9f\xe6\x9c\x9f\xe4\xb8\x80",
            "\xe6\x98\x9f\xe6\x9c\x9f\xe4\xba\x8c",
            "\xe6\x98\x9f\xe6\x9c\x9f\xe4\xb8\x89",
            "\xe6\x98\x9f\xe6\x9c\x9f\xe5\x9b\x9b",
            "\xe6\x98\x9f\xe6\x9c\x9f\xe4\xba\x94",
            "\xe6\x98\x9f\xe6\x9c\x9f\xe5\x85\xad"
        };
        
        std::string holiday = getHolidayInfo(year, month, day);
        
        std::string result;
        result += std::to_string(year) + "\xe5\xb9\xb4" + std::to_string(month) + "\xe6\x9c\x88" + std::to_string(day) + "\xe6\x97\xa5 ";
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
        prompt += "\xe5\xbd\x93\xe5\x89\x8d\xe5\xb9\xb4\xe4\xbb\xbd: " + std::to_string(year) + "\xe5\xb9\xb4(" + getZodiac(year) + "\xe5\xb9\xb4)\n";
        prompt += "\xe4\xbb\x8a\xe5\xa4\xa9: " + getFullDateInfo(0) + "\n";
        prompt += std::to_string(year) + "\xe5\xb9\xb4\xe6\x98\xa5\xe8\x8a\x82: " "2" "\xe6\x9c\x88" "17" "\xe6\x97\xa5\n";
        auto important = getKeyHolidays(year);
        for (const auto& h : important) {
            prompt += h + "\n";
        }
        
        return prompt;
    }
    
    std::vector<std::string> getKeyHolidays(int year) {
        std::vector<std::string> result;
        std::vector<std::string> key_names = {
            "\xe9\x99\xa4\xe5\xa4\x95", "\xe6\x98\xa5\xe8\x8a\x82", "\xe5\x85\x83\xe5\xae\xb5\xe8\x8a\x82",
            "\xe6\xb8\x85\xe6\x98\x8e\xe8\x8a\x82", "\xe7\xab\xaf\xe5\x8d\x88\xe8\x8a\x82",
            "\xe4\xb8\xad\xe7\xa7\x8b\xe8\x8a\x82", "\xe5\x9b\xbd\xe5\xba\x86\xe6\x97\xa5"
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
                            result.push_back(name + ": " + month + "\xe6\x9c\x88" + day + "\xe6\x97\xa5");
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
            {"\xe4\xb8\x80\xe6\x9c\x88", 1}, {"\xe4\xba\x8c\xe6\x9c\x88", 2}, {"\xe4\xb8\x89\xe6\x9c\x88", 3},
            {"\xe5\x9b\x9b\xe6\x9c\x88", 4}, {"\xe4\xba\x94\xe6\x9c\x88", 5}, {"\xe5\x85\xad\xe6\x9c\x88", 6},
            {"\xe4\xb8\x83\xe6\x9c\x88", 7}, {"\xe5\x85\xab\xe6\x9c\x88", 8}, {"\xe4\xb9\x9d\xe6\x9c\x88", 9},
            {"\xe5\x8d\x81\xe6\x9c\x88", 10}, {"\xe5\x8d\x81\xe4\xb8\x80\xe6\x9c\x88", 11}, {"\xe5\x8d\x81\xe4\xba\x8c\xe6\x9c\x88", 12},
            {"1\xe6\x9c\x88", 1}, {"2\xe6\x9c\x88", 2}, {"3\xe6\x9c\x88", 3}, {"4\xe6\x9c\x88", 4},
            {"5\xe6\x9c\x88", 5}, {"6\xe6\x9c\x88", 6}, {"7\xe6\x9c\x88", 7}, {"8\xe6\x9c\x88", 8},
            {"9\xe6\x9c\x88", 9}, {"10\xe6\x9c\x88", 10}, {"11\xe6\x9c\x88", 11}, {"12\xe6\x9c\x88", 12}
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
                            result += std::to_string(m) + "\xe6\x9c\x88" + day + "\xe6\x97\xa5: " + holiday_name + "\n";
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
                        return holiday_name + ": " + std::to_string(year) + "\xe5\xb9\xb4" + month + "\xe6\x9c\x88" + day + "\xe6\x97\xa5";
                    }
                }
            }
        }
        return "\xe6\x9c\xaa\xe6\x89\xbe\xe5\x88\xb0" + name + "\xe7\x9a\x84\xe6\x97\xa5\xe6\x9c\x9f\xe4\xbf\xa1\xe6\x81\xaf";
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
            if (v.find("\xe6\x98\xa5\xe8\x8a\x82") != std::string::npos || 
                v.find("\xe9\x99\xa4\xe5\xa4\x95") != std::string::npos) {
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
