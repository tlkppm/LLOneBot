#pragma once

#include "Types.h"
#include <string>
#include <stdexcept>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace LCHBOT {

class JsonParser {
public:
    static JsonValue parse(const std::string& json) {
        size_t pos = 0;
        return parseValue(json, pos);
    }
    
    static std::string stringify(const JsonValue& value, bool pretty = false, int indent = 0) {
        std::ostringstream oss;
        stringifyValue(oss, value, pretty, indent);
        return oss.str();
    }
    
private:
    static void skipWhitespace(const std::string& json, size_t& pos) {
        while (pos < json.size() && std::isspace(json[pos])) {
            ++pos;
        }
    }
    
    static JsonValue parseValue(const std::string& json, size_t& pos) {
        skipWhitespace(json, pos);
        
        if (pos >= json.size()) {
            throw std::runtime_error("Unexpected end of JSON");
        }
        
        char c = json[pos];
        
        if (c == 'n') return parseNull(json, pos);
        if (c == 't' || c == 'f') return parseBool(json, pos);
        if (c == '"') return parseString(json, pos);
        if (c == '[') return parseArray(json, pos);
        if (c == '{') return parseObject(json, pos);
        if (c == '-' || std::isdigit(c)) return parseNumber(json, pos);
        
        throw std::runtime_error("Invalid JSON value");
    }
    
    static JsonValue parseNull(const std::string& json, size_t& pos) {
        if (json.substr(pos, 4) == "null") {
            pos += 4;
            return JsonValue(nullptr);
        }
        throw std::runtime_error("Invalid null value");
    }
    
    static JsonValue parseBool(const std::string& json, size_t& pos) {
        if (json.substr(pos, 4) == "true") {
            pos += 4;
            return JsonValue(true);
        }
        if (json.substr(pos, 5) == "false") {
            pos += 5;
            return JsonValue(false);
        }
        throw std::runtime_error("Invalid boolean value");
    }
    
    static JsonValue parseString(const std::string& json, size_t& pos) {
        ++pos;
        std::string result;
        
        while (pos < json.size()) {
            char c = json[pos];
            
            if (c == '"') {
                ++pos;
                return JsonValue(result);
            }
            
            if (c == '\\') {
                ++pos;
                if (pos >= json.size()) break;
                
                switch (json[pos]) {
                    case '"': result += '"'; break;
                    case '\\': result += '\\'; break;
                    case '/': result += '/'; break;
                    case 'b': result += '\b'; break;
                    case 'f': result += '\f'; break;
                    case 'n': result += '\n'; break;
                    case 'r': result += '\r'; break;
                    case 't': result += '\t'; break;
                    case 'u': {
                        if (pos + 4 >= json.size()) {
                            throw std::runtime_error("Invalid unicode escape");
                        }
                        std::string hex = json.substr(pos + 1, 4);
                        int codepoint = std::stoi(hex, nullptr, 16);
                        pos += 4;
                        
                        if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                            if (pos + 3 < json.size() && json[pos + 1] == '\\' && json[pos + 2] == 'u') {
                                std::string hex2 = json.substr(pos + 3, 4);
                                int low = std::stoi(hex2, nullptr, 16);
                                if (low >= 0xDC00 && low <= 0xDFFF) {
                                    codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                                    pos += 6;
                                }
                            }
                        }
                        
                        if (codepoint < 0x80) {
                            result += static_cast<char>(codepoint);
                        } else if (codepoint < 0x800) {
                            result += static_cast<char>(0xC0 | (codepoint >> 6));
                            result += static_cast<char>(0x80 | (codepoint & 0x3F));
                        } else if (codepoint < 0x10000) {
                            result += static_cast<char>(0xE0 | (codepoint >> 12));
                            result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                            result += static_cast<char>(0x80 | (codepoint & 0x3F));
                        } else {
                            result += static_cast<char>(0xF0 | (codepoint >> 18));
                            result += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
                            result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                            result += static_cast<char>(0x80 | (codepoint & 0x3F));
                        }
                        break;
                    }
                    default:
                        result += json[pos];
                }
            } else {
                result += c;
            }
            ++pos;
        }
        
        throw std::runtime_error("Unterminated string");
    }
    
    static JsonValue parseNumber(const std::string& json, size_t& pos) {
        size_t start = pos;
        bool is_float = false;
        
        if (json[pos] == '-') ++pos;
        
        while (pos < json.size() && std::isdigit(json[pos])) ++pos;
        
        if (pos < json.size() && json[pos] == '.') {
            is_float = true;
            ++pos;
            while (pos < json.size() && std::isdigit(json[pos])) ++pos;
        }
        
        if (pos < json.size() && (json[pos] == 'e' || json[pos] == 'E')) {
            is_float = true;
            ++pos;
            if (pos < json.size() && (json[pos] == '+' || json[pos] == '-')) ++pos;
            while (pos < json.size() && std::isdigit(json[pos])) ++pos;
        }
        
        std::string num_str = json.substr(start, pos - start);
        
        if (is_float) {
            return JsonValue(std::stod(num_str));
        } else {
            return JsonValue(std::stoll(num_str));
        }
    }
    
    static JsonValue parseArray(const std::string& json, size_t& pos) {
        ++pos;
        std::vector<JsonValue> arr;
        
        skipWhitespace(json, pos);
        
        if (pos < json.size() && json[pos] == ']') {
            ++pos;
            return JsonValue(arr);
        }
        
        while (true) {
            arr.push_back(parseValue(json, pos));
            skipWhitespace(json, pos);
            
            if (pos >= json.size()) {
                throw std::runtime_error("Unterminated array");
            }
            
            if (json[pos] == ']') {
                ++pos;
                return JsonValue(arr);
            }
            
            if (json[pos] != ',') {
                throw std::runtime_error("Expected ',' in array");
            }
            ++pos;
        }
    }
    
    static JsonValue parseObject(const std::string& json, size_t& pos) {
        ++pos;
        std::map<std::string, JsonValue> obj;
        
        skipWhitespace(json, pos);
        
        if (pos < json.size() && json[pos] == '}') {
            ++pos;
            return JsonValue(obj);
        }
        
        while (true) {
            skipWhitespace(json, pos);
            
            if (pos >= json.size() || json[pos] != '"') {
                throw std::runtime_error("Expected string key in object");
            }
            
            JsonValue key_val = parseString(json, pos);
            std::string key = key_val.asString();
            
            skipWhitespace(json, pos);
            
            if (pos >= json.size() || json[pos] != ':') {
                throw std::runtime_error("Expected ':' in object");
            }
            ++pos;
            
            obj[key] = parseValue(json, pos);
            skipWhitespace(json, pos);
            
            if (pos >= json.size()) {
                throw std::runtime_error("Unterminated object");
            }
            
            if (json[pos] == '}') {
                ++pos;
                return JsonValue(obj);
            }
            
            if (json[pos] != ',') {
                throw std::runtime_error("Expected ',' in object");
            }
            ++pos;
        }
    }
    
    static void stringifyValue(std::ostringstream& oss, const JsonValue& value, 
                               bool pretty, int indent) {
        if (value.isNull()) {
            oss << "null";
        } else if (value.isBool()) {
            oss << (value.asBool() ? "true" : "false");
        } else if (value.isInt()) {
            oss << value.asInt();
        } else if (value.isDouble()) {
            oss << value.asDouble();
        } else if (value.isString()) {
            oss << '"' << escapeString(value.asString()) << '"';
        } else if (value.isArray()) {
            const auto& arr = value.asArray();
            oss << '[';
            for (size_t i = 0; i < arr.size(); ++i) {
                if (i > 0) oss << ',';
                if (pretty) {
                    oss << '\n' << std::string((indent + 1) * 2, ' ');
                }
                stringifyValue(oss, arr[i], pretty, indent + 1);
            }
            if (pretty && !arr.empty()) {
                oss << '\n' << std::string(indent * 2, ' ');
            }
            oss << ']';
        } else if (value.isObject()) {
            const auto& obj = value.asObject();
            oss << '{';
            bool first = true;
            for (const auto& [k, v] : obj) {
                if (!first) oss << ',';
                first = false;
                if (pretty) {
                    oss << '\n' << std::string((indent + 1) * 2, ' ');
                }
                oss << '"' << escapeString(k) << '"' << ':';
                if (pretty) oss << ' ';
                stringifyValue(oss, v, pretty, indent + 1);
            }
            if (pretty && !obj.empty()) {
                oss << '\n' << std::string(indent * 2, ' ');
            }
            oss << '}';
        }
    }
    
    static std::string escapeString(const std::string& s) {
        std::string result;
        result.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\b': result += "\\b"; break;
                case '\f': result += "\\f"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default:
                    if (c < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                        result += buf;
                    } else {
                        result += s[i];
                    }
            }
        }
        return result;
    }
};

}
