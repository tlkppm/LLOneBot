#pragma once

#include <string>
#include <vector>
#include <map>
#include <variant>
#include <optional>
#include <cstdint>
#include <memory>
#include <functional>
#include <chrono>

namespace LCHBOT {

using Json = std::map<std::string, std::variant<
    std::nullptr_t,
    bool,
    int64_t,
    double,
    std::string,
    std::vector<struct JsonValue>,
    std::map<std::string, struct JsonValue>
>>;

struct JsonValue {
    std::variant<
        std::nullptr_t,
        bool,
        int64_t,
        double,
        std::string,
        std::vector<JsonValue>,
        std::map<std::string, JsonValue>
    > value;
    
    JsonValue() : value(nullptr) {}
    JsonValue(std::nullptr_t) : value(nullptr) {}
    JsonValue(bool v) : value(v) {}
    JsonValue(int v) : value(static_cast<int64_t>(v)) {}
    JsonValue(int64_t v) : value(v) {}
    JsonValue(double v) : value(v) {}
    JsonValue(const std::string& v) : value(v) {}
    JsonValue(const char* v) : value(std::string(v)) {}
    JsonValue(std::vector<JsonValue> v) : value(std::move(v)) {}
    JsonValue(std::map<std::string, JsonValue> v) : value(std::move(v)) {}
    
    bool isNull() const { return std::holds_alternative<std::nullptr_t>(value); }
    bool isBool() const { return std::holds_alternative<bool>(value); }
    bool isInt() const { return std::holds_alternative<int64_t>(value); }
    bool isDouble() const { return std::holds_alternative<double>(value); }
    bool isString() const { return std::holds_alternative<std::string>(value); }
    bool isArray() const { return std::holds_alternative<std::vector<JsonValue>>(value); }
    bool isObject() const { return std::holds_alternative<std::map<std::string, JsonValue>>(value); }
    
    bool asBool() const { return std::get<bool>(value); }
    int64_t asInt() const { return std::get<int64_t>(value); }
    double asDouble() const { return std::get<double>(value); }
    const std::string& asString() const { return std::get<std::string>(value); }
    std::vector<JsonValue>& asArray() { return std::get<std::vector<JsonValue>>(value); }
    const std::vector<JsonValue>& asArray() const { return std::get<std::vector<JsonValue>>(value); }
    std::map<std::string, JsonValue>& asObject() { return std::get<std::map<std::string, JsonValue>>(value); }
    const std::map<std::string, JsonValue>& asObject() const { return std::get<std::map<std::string, JsonValue>>(value); }
    
    JsonValue& operator[](const std::string& key) {
        return std::get<std::map<std::string, JsonValue>>(value)[key];
    }
    
    JsonValue& operator[](size_t index) {
        return std::get<std::vector<JsonValue>>(value)[index];
    }
    
    bool contains(const std::string& key) const {
        if (!isObject()) return false;
        const auto& obj = asObject();
        return obj.find(key) != obj.end();
    }
};

enum class MessageType {
    Private,
    Group
};

enum class EventType {
    Message,
    Notice,
    Request,
    Meta,
    Unknown
};

enum class NoticeType {
    GroupUpload,
    GroupAdmin,
    GroupDecrease,
    GroupIncrease,
    GroupBan,
    FriendAdd,
    GroupRecall,
    FriendRecall,
    Notify,
    Unknown
};

enum class RequestType {
    Friend,
    Group,
    Unknown
};

enum class MetaEventType {
    Lifecycle,
    Heartbeat,
    Unknown
};

struct MessageSegment {
    std::string type;
    std::map<std::string, std::string> data;
};

struct Sender {
    int64_t user_id = 0;
    std::string nickname;
    std::string card;
    std::string sex;
    int32_t age = 0;
    std::string area;
    std::string level;
    std::string role;
    std::string title;
};

struct Message {
    int32_t message_id = 0;
    MessageType message_type = MessageType::Private;
    int64_t user_id = 0;
    int64_t group_id = 0;
    std::vector<MessageSegment> segments;
    std::string raw_message;
    Sender sender;
    int64_t time = 0;
    int64_t self_id = 0;
};

struct GroupInfo {
    int64_t group_id = 0;
    std::string group_name;
    int32_t member_count = 0;
    int32_t max_member_count = 0;
};

struct UserInfo {
    int64_t user_id = 0;
    std::string nickname;
    std::string sex;
    int32_t age = 0;
};

struct GroupMember {
    int64_t group_id = 0;
    int64_t user_id = 0;
    std::string nickname;
    std::string card;
    std::string sex;
    int32_t age = 0;
    std::string area;
    int64_t join_time = 0;
    int64_t last_sent_time = 0;
    std::string level;
    std::string role;
    bool unfriendly = false;
    std::string title;
    int64_t title_expire_time = 0;
    bool card_changeable = false;
};

struct ApiResponse {
    std::string status;
    int32_t retcode = 0;
    JsonValue data;
    std::string echo;
};

struct ApiRequest {
    std::string action;
    JsonValue params;
    std::string echo;
};

using MessageHandler = std::function<void(const Message&)>;
using EventHandler = std::function<void(const JsonValue&)>;
using ApiCallback = std::function<void(const ApiResponse&)>;

}
