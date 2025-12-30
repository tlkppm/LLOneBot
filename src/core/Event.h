#pragma once

#include "Types.h"
#include "JsonParser.h"
#include <functional>
#include <vector>
#include <map>
#include <mutex>
#include <string>
#include <memory>

namespace LCHBOT {

class Event {
public:
    EventType type = EventType::Unknown;
    int64_t time = 0;
    int64_t self_id = 0;
    std::string post_type;
    JsonValue raw_data;
    
    virtual ~Event() = default;
};

class MessageEvent : public Event {
public:
    MessageEvent() { type = EventType::Message; }
    
    MessageType message_type = MessageType::Private;
    std::string sub_type;
    int32_t message_id = 0;
    int64_t user_id = 0;
    int64_t group_id = 0;
    std::vector<MessageSegment> message;
    std::string raw_message;
    int32_t font = 0;
    Sender sender;
    
    bool isPrivate() const { return message_type == MessageType::Private; }
    bool isGroup() const { return message_type == MessageType::Group; }
    
    std::string getText() const {
        std::string text;
        for (const auto& seg : message) {
            if (seg.type == "text") {
                auto it = seg.data.find("text");
                if (it != seg.data.end()) {
                    text += it->second;
                }
            }
        }
        return text;
    }
};

class NoticeEvent : public Event {
public:
    NoticeEvent() { type = EventType::Notice; }
    
    NoticeType notice_type = NoticeType::Unknown;
    std::string sub_type;
    int64_t group_id = 0;
    int64_t user_id = 0;
    int64_t operator_id = 0;
    int64_t target_id = 0;
    int64_t duration = 0;
    int32_t message_id = 0;
};

class RequestEvent : public Event {
public:
    RequestEvent() { type = EventType::Request; }
    
    RequestType request_type = RequestType::Unknown;
    std::string sub_type;
    int64_t user_id = 0;
    int64_t group_id = 0;
    std::string comment;
    std::string flag;
};

class MetaEvent : public Event {
public:
    MetaEvent() { type = EventType::Meta; }
    
    MetaEventType meta_event_type = MetaEventType::Unknown;
    std::string sub_type;
    JsonValue status;
    int64_t interval = 0;
};

class EventParser {
public:
    static std::unique_ptr<Event> parse(const JsonValue& json) {
        if (!json.isObject()) return nullptr;
        
        const auto& obj = json.asObject();
        
        auto post_type_it = obj.find("post_type");
        if (post_type_it == obj.end()) return nullptr;
        
        std::string post_type = post_type_it->second.asString();
        
        std::unique_ptr<Event> event;
        
        if (post_type == "message" || post_type == "message_sent") {
            event = parseMessageEvent(json);
        } else if (post_type == "notice") {
            event = parseNoticeEvent(json);
        } else if (post_type == "request") {
            event = parseRequestEvent(json);
        } else if (post_type == "meta_event") {
            event = parseMetaEvent(json);
        } else {
            event = std::make_unique<Event>();
        }
        
        if (event) {
            event->raw_data = json;
            event->post_type = post_type;
            
            if (obj.find("time") != obj.end()) {
                event->time = obj.at("time").asInt();
            }
            if (obj.find("self_id") != obj.end()) {
                event->self_id = obj.at("self_id").asInt();
            }
        }
        
        return event;
    }
    
private:
    static std::unique_ptr<MessageEvent> parseMessageEvent(const JsonValue& json) {
        auto event = std::make_unique<MessageEvent>();
        const auto& obj = json.asObject();
        
        if (obj.find("message_type") != obj.end()) {
            std::string mt = obj.at("message_type").asString();
            event->message_type = (mt == "group") ? MessageType::Group : MessageType::Private;
        }
        
        if (obj.find("sub_type") != obj.end()) {
            event->sub_type = obj.at("sub_type").asString();
        }
        
        if (obj.find("message_id") != obj.end()) {
            event->message_id = static_cast<int32_t>(obj.at("message_id").asInt());
        }
        
        if (obj.find("user_id") != obj.end()) {
            event->user_id = obj.at("user_id").asInt();
        }
        
        if (obj.find("group_id") != obj.end()) {
            event->group_id = obj.at("group_id").asInt();
        }
        
        if (obj.find("raw_message") != obj.end()) {
            event->raw_message = obj.at("raw_message").asString();
        }
        
        if (obj.find("font") != obj.end()) {
            event->font = static_cast<int32_t>(obj.at("font").asInt());
        }
        
        if (obj.find("message") != obj.end()) {
            const auto& msg = obj.at("message");
            if (msg.isArray()) {
                for (const auto& seg : msg.asArray()) {
                    if (seg.isObject()) {
                        MessageSegment segment;
                        const auto& seg_obj = seg.asObject();
                        
                        if (seg_obj.find("type") != seg_obj.end()) {
                            segment.type = seg_obj.at("type").asString();
                        }
                        
                        if (seg_obj.find("data") != seg_obj.end()) {
                            const auto& data = seg_obj.at("data");
                            if (data.isObject()) {
                                for (const auto& [k, v] : data.asObject()) {
                                    if (v.isString()) {
                                        segment.data[k] = v.asString();
                                    } else if (v.isInt()) {
                                        segment.data[k] = std::to_string(v.asInt());
                                    } else if (v.isBool()) {
                                        segment.data[k] = v.asBool() ? "true" : "false";
                                    }
                                }
                            }
                        }
                        
                        event->message.push_back(std::move(segment));
                    }
                }
            } else if (msg.isString()) {
                MessageSegment seg;
                seg.type = "text";
                seg.data["text"] = msg.asString();
                event->message.push_back(std::move(seg));
            }
        }
        
        if (obj.find("sender") != obj.end()) {
            const auto& sender = obj.at("sender");
            if (sender.isObject()) {
                const auto& s = sender.asObject();
                if (s.find("user_id") != s.end()) event->sender.user_id = s.at("user_id").asInt();
                if (s.find("nickname") != s.end()) event->sender.nickname = s.at("nickname").asString();
                if (s.find("card") != s.end()) event->sender.card = s.at("card").asString();
                if (s.find("sex") != s.end()) event->sender.sex = s.at("sex").asString();
                if (s.find("age") != s.end()) event->sender.age = static_cast<int32_t>(s.at("age").asInt());
                if (s.find("area") != s.end()) event->sender.area = s.at("area").asString();
                if (s.find("level") != s.end()) event->sender.level = s.at("level").asString();
                if (s.find("role") != s.end()) event->sender.role = s.at("role").asString();
                if (s.find("title") != s.end()) event->sender.title = s.at("title").asString();
            }
        }
        
        return event;
    }
    
    static std::unique_ptr<NoticeEvent> parseNoticeEvent(const JsonValue& json) {
        auto event = std::make_unique<NoticeEvent>();
        const auto& obj = json.asObject();
        
        if (obj.find("notice_type") != obj.end()) {
            std::string nt = obj.at("notice_type").asString();
            if (nt == "group_upload") event->notice_type = NoticeType::GroupUpload;
            else if (nt == "group_admin") event->notice_type = NoticeType::GroupAdmin;
            else if (nt == "group_decrease") event->notice_type = NoticeType::GroupDecrease;
            else if (nt == "group_increase") event->notice_type = NoticeType::GroupIncrease;
            else if (nt == "group_ban") event->notice_type = NoticeType::GroupBan;
            else if (nt == "friend_add") event->notice_type = NoticeType::FriendAdd;
            else if (nt == "group_recall") event->notice_type = NoticeType::GroupRecall;
            else if (nt == "friend_recall") event->notice_type = NoticeType::FriendRecall;
            else if (nt == "notify") event->notice_type = NoticeType::Notify;
        }
        
        if (obj.find("sub_type") != obj.end()) event->sub_type = obj.at("sub_type").asString();
        if (obj.find("group_id") != obj.end()) event->group_id = obj.at("group_id").asInt();
        if (obj.find("user_id") != obj.end()) event->user_id = obj.at("user_id").asInt();
        if (obj.find("operator_id") != obj.end()) event->operator_id = obj.at("operator_id").asInt();
        if (obj.find("target_id") != obj.end()) event->target_id = obj.at("target_id").asInt();
        if (obj.find("duration") != obj.end()) event->duration = obj.at("duration").asInt();
        if (obj.find("message_id") != obj.end()) event->message_id = static_cast<int32_t>(obj.at("message_id").asInt());
        
        return event;
    }
    
    static std::unique_ptr<RequestEvent> parseRequestEvent(const JsonValue& json) {
        auto event = std::make_unique<RequestEvent>();
        const auto& obj = json.asObject();
        
        if (obj.find("request_type") != obj.end()) {
            std::string rt = obj.at("request_type").asString();
            if (rt == "friend") event->request_type = RequestType::Friend;
            else if (rt == "group") event->request_type = RequestType::Group;
        }
        
        if (obj.find("sub_type") != obj.end()) event->sub_type = obj.at("sub_type").asString();
        if (obj.find("user_id") != obj.end()) event->user_id = obj.at("user_id").asInt();
        if (obj.find("group_id") != obj.end()) event->group_id = obj.at("group_id").asInt();
        if (obj.find("comment") != obj.end()) event->comment = obj.at("comment").asString();
        if (obj.find("flag") != obj.end()) event->flag = obj.at("flag").asString();
        
        return event;
    }
    
    static std::unique_ptr<MetaEvent> parseMetaEvent(const JsonValue& json) {
        auto event = std::make_unique<MetaEvent>();
        const auto& obj = json.asObject();
        
        if (obj.find("meta_event_type") != obj.end()) {
            std::string met = obj.at("meta_event_type").asString();
            if (met == "lifecycle") event->meta_event_type = MetaEventType::Lifecycle;
            else if (met == "heartbeat") event->meta_event_type = MetaEventType::Heartbeat;
        }
        
        if (obj.find("sub_type") != obj.end()) event->sub_type = obj.at("sub_type").asString();
        if (obj.find("status") != obj.end()) event->status = obj.at("status");
        if (obj.find("interval") != obj.end()) event->interval = obj.at("interval").asInt();
        
        return event;
    }
};

using EventCallback = std::function<bool(const Event&)>;
using MessageCallback = std::function<bool(const MessageEvent&)>;
using NoticeCallback = std::function<bool(const NoticeEvent&)>;
using RequestCallback = std::function<bool(const RequestEvent&)>;

class EventDispatcher {
public:
    static EventDispatcher& instance() {
        static EventDispatcher inst;
        return inst;
    }
    
    void registerHandler(const std::string& name, EventCallback callback, int priority = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        handlers_.push_back({name, std::move(callback), priority});
        sortHandlers();
    }
    
    void registerMessageHandler(const std::string& name, MessageCallback callback, int priority = 0) {
        registerHandler(name, [cb = std::move(callback)](const Event& e) -> bool {
            if (e.type == EventType::Message) {
                return cb(static_cast<const MessageEvent&>(e));
            }
            return false;
        }, priority);
    }
    
    void registerNoticeHandler(const std::string& name, NoticeCallback callback, int priority = 0) {
        registerHandler(name, [cb = std::move(callback)](const Event& e) -> bool {
            if (e.type == EventType::Notice) {
                return cb(static_cast<const NoticeEvent&>(e));
            }
            return false;
        }, priority);
    }
    
    void registerRequestHandler(const std::string& name, RequestCallback callback, int priority = 0) {
        registerHandler(name, [cb = std::move(callback)](const Event& e) -> bool {
            if (e.type == EventType::Request) {
                return cb(static_cast<const RequestEvent&>(e));
            }
            return false;
        }, priority);
    }
    
    void unregisterHandler(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        handlers_.erase(
            std::remove_if(handlers_.begin(), handlers_.end(),
                [&name](const Handler& h) { return h.name == name; }),
            handlers_.end()
        );
    }
    
    void dispatch(const Event& event) {
        std::vector<Handler> handlers_copy;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            handlers_copy = handlers_;
        }
        
        for (const auto& handler : handlers_copy) {
            try {
                if (handler.callback(event)) {
                    break;
                }
            } catch (...) {
            }
        }
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        handlers_.clear();
    }
    
private:
    EventDispatcher() = default;
    
    struct Handler {
        std::string name;
        EventCallback callback;
        int priority;
    };
    
    void sortHandlers() {
        std::sort(handlers_.begin(), handlers_.end(),
            [](const Handler& a, const Handler& b) {
                return a.priority > b.priority;
            });
    }
    
    std::vector<Handler> handlers_;
    std::mutex mutex_;
};

}
