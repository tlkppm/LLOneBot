#pragma once

#include "../core/Types.h"
#include "../core/JsonParser.h"
#include "../core/Logger.h"
#include <string>
#include <map>
#include <functional>
#include <mutex>
#include <atomic>
#include <queue>
#include <condition_variable>

namespace LCHBOT {

class OneBotApi {
public:
    using SendFunc = std::function<void(const std::string&)>;
    using ResponseCallback = std::function<void(const ApiResponse&)>;
    
    void setSendFunction(SendFunc func) {
        send_func_ = std::move(func);
    }
    
    void handleResponse(const JsonValue& json) {
        if (!json.isObject()) return;
        const auto& obj = json.asObject();
        
        auto echo_it = obj.find("echo");
        if (echo_it == obj.end()) return;
        
        std::string echo = echo_it->second.asString();
        
        ResponseCallback callback;
        {
            std::lock_guard<std::mutex> lock(callbacks_mutex_);
            auto it = callbacks_.find(echo);
            if (it != callbacks_.end()) {
                callback = std::move(it->second);
                callbacks_.erase(it);
            }
        }
        
        if (callback) {
            ApiResponse response;
            if (obj.find("status") != obj.end()) response.status = obj.at("status").asString();
            if (obj.find("retcode") != obj.end()) response.retcode = static_cast<int32_t>(obj.at("retcode").asInt());
            if (obj.find("data") != obj.end()) response.data = obj.at("data");
            response.echo = echo;
            callback(response);
        }
    }
    
    std::string sendPrivateMsg(int64_t user_id, const std::string& message, bool auto_escape = false) {
        std::map<std::string, JsonValue> params;
        params["user_id"] = JsonValue(user_id);
        params["message"] = JsonValue(message);
        params["auto_escape"] = JsonValue(auto_escape);
        return callApi("send_private_msg", JsonValue(params));
    }
    
    std::string sendPrivateMsg(int64_t user_id, const std::vector<MessageSegment>& message) {
        std::map<std::string, JsonValue> params;
        params["user_id"] = JsonValue(user_id);
        params["message"] = serializeMessage(message);
        return callApi("send_private_msg", JsonValue(params));
    }
    
    std::string sendGroupMsg(int64_t group_id, const std::string& message, bool auto_escape = false) {
        std::map<std::string, JsonValue> params;
        params["group_id"] = JsonValue(group_id);
        params["message"] = JsonValue(message);
        params["auto_escape"] = JsonValue(auto_escape);
        std::string echo = callApi("send_group_msg", JsonValue(params));
        return echo;
    }
    
    std::string sendGroupMsg(int64_t group_id, const std::vector<MessageSegment>& message) {
        std::map<std::string, JsonValue> params;
        params["group_id"] = JsonValue(group_id);
        params["message"] = serializeMessage(message);
        return callApi("send_group_msg", JsonValue(params));
    }
    
    std::string sendGroupMsgReply(int64_t group_id, int32_t reply_msg_id, const std::string& message) {
        std::vector<MessageSegment> segments;
        segments.push_back(reply(reply_msg_id));
        segments.push_back(text(message));
        return sendGroupMsg(group_id, segments);
    }
    
    std::string sendPrivateMsgReply(int64_t user_id, int32_t reply_msg_id, const std::string& message) {
        std::vector<MessageSegment> segments;
        segments.push_back(reply(reply_msg_id));
        segments.push_back(text(message));
        return sendPrivateMsg(user_id, segments);
    }
    
    std::string sendMsg(MessageType type, int64_t id, const std::string& message, bool auto_escape = false) {
        std::map<std::string, JsonValue> params;
        params["message_type"] = JsonValue(type == MessageType::Group ? "group" : "private");
        if (type == MessageType::Group) {
            params["group_id"] = JsonValue(id);
        } else {
            params["user_id"] = JsonValue(id);
        }
        params["message"] = JsonValue(message);
        params["auto_escape"] = JsonValue(auto_escape);
        return callApi("send_msg", JsonValue(params));
    }
    
    std::string deleteMsg(int32_t message_id) {
        std::map<std::string, JsonValue> params;
        params["message_id"] = JsonValue(static_cast<int64_t>(message_id));
        return callApi("delete_msg", JsonValue(params));
    }
    
    std::string getMsg(int32_t message_id) {
        std::map<std::string, JsonValue> params;
        params["message_id"] = JsonValue(static_cast<int64_t>(message_id));
        return callApi("get_msg", JsonValue(params));
    }
    
    std::string getForwardMsg(const std::string& id) {
        std::map<std::string, JsonValue> params;
        params["id"] = JsonValue(id);
        return callApi("get_forward_msg", JsonValue(params));
    }
    
    std::string sendLike(int64_t user_id, int32_t times = 1) {
        std::map<std::string, JsonValue> params;
        params["user_id"] = JsonValue(user_id);
        params["times"] = JsonValue(static_cast<int64_t>(times));
        return callApi("send_like", JsonValue(params));
    }
    
    std::string setGroupKick(int64_t group_id, int64_t user_id, bool reject_add_request = false) {
        std::map<std::string, JsonValue> params;
        params["group_id"] = JsonValue(group_id);
        params["user_id"] = JsonValue(user_id);
        params["reject_add_request"] = JsonValue(reject_add_request);
        return callApi("set_group_kick", JsonValue(params));
    }
    
    std::string setGroupBan(int64_t group_id, int64_t user_id, int64_t duration = 1800) {
        std::map<std::string, JsonValue> params;
        params["group_id"] = JsonValue(group_id);
        params["user_id"] = JsonValue(user_id);
        params["duration"] = JsonValue(duration);
        return callApi("set_group_ban", JsonValue(params));
    }
    
    std::string setGroupWholeBan(int64_t group_id, bool enable = true) {
        std::map<std::string, JsonValue> params;
        params["group_id"] = JsonValue(group_id);
        params["enable"] = JsonValue(enable);
        return callApi("set_group_whole_ban", JsonValue(params));
    }
    
    std::string setGroupAdmin(int64_t group_id, int64_t user_id, bool enable = true) {
        std::map<std::string, JsonValue> params;
        params["group_id"] = JsonValue(group_id);
        params["user_id"] = JsonValue(user_id);
        params["enable"] = JsonValue(enable);
        return callApi("set_group_admin", JsonValue(params));
    }
    
    std::string setGroupCard(int64_t group_id, int64_t user_id, const std::string& card) {
        std::map<std::string, JsonValue> params;
        params["group_id"] = JsonValue(group_id);
        params["user_id"] = JsonValue(user_id);
        params["card"] = JsonValue(card);
        return callApi("set_group_card", JsonValue(params));
    }
    
    std::string setGroupName(int64_t group_id, const std::string& group_name) {
        std::map<std::string, JsonValue> params;
        params["group_id"] = JsonValue(group_id);
        params["group_name"] = JsonValue(group_name);
        return callApi("set_group_name", JsonValue(params));
    }
    
    std::string setGroupLeave(int64_t group_id, bool is_dismiss = false) {
        std::map<std::string, JsonValue> params;
        params["group_id"] = JsonValue(group_id);
        params["is_dismiss"] = JsonValue(is_dismiss);
        return callApi("set_group_leave", JsonValue(params));
    }
    
    std::string setGroupSpecialTitle(int64_t group_id, int64_t user_id, const std::string& title, int64_t duration = -1) {
        std::map<std::string, JsonValue> params;
        params["group_id"] = JsonValue(group_id);
        params["user_id"] = JsonValue(user_id);
        params["special_title"] = JsonValue(title);
        params["duration"] = JsonValue(duration);
        return callApi("set_group_special_title", JsonValue(params));
    }
    
    std::string setFriendAddRequest(const std::string& flag, bool approve = true, const std::string& remark = "") {
        std::map<std::string, JsonValue> params;
        params["flag"] = JsonValue(flag);
        params["approve"] = JsonValue(approve);
        if (!remark.empty()) params["remark"] = JsonValue(remark);
        return callApi("set_friend_add_request", JsonValue(params));
    }
    
    std::string setGroupAddRequest(const std::string& flag, const std::string& sub_type, bool approve = true, const std::string& reason = "") {
        std::map<std::string, JsonValue> params;
        params["flag"] = JsonValue(flag);
        params["sub_type"] = JsonValue(sub_type);
        params["approve"] = JsonValue(approve);
        if (!reason.empty()) params["reason"] = JsonValue(reason);
        return callApi("set_group_add_request", JsonValue(params));
    }
    
    std::string getLoginInfo() {
        return callApi("get_login_info", JsonValue(std::map<std::string, JsonValue>{}));
    }
    
    std::string getStrangerInfo(int64_t user_id, bool no_cache = false) {
        std::map<std::string, JsonValue> params;
        params["user_id"] = JsonValue(user_id);
        params["no_cache"] = JsonValue(no_cache);
        return callApi("get_stranger_info", JsonValue(params));
    }
    
    std::string getFriendList() {
        return callApi("get_friend_list", JsonValue(std::map<std::string, JsonValue>{}));
    }
    
    std::string getGroupInfo(int64_t group_id, bool no_cache = false) {
        std::map<std::string, JsonValue> params;
        params["group_id"] = JsonValue(group_id);
        params["no_cache"] = JsonValue(no_cache);
        return callApi("get_group_info", JsonValue(params));
    }
    
    std::string getGroupList() {
        return callApi("get_group_list", JsonValue(std::map<std::string, JsonValue>{}));
    }
    
    std::string getGroupMemberInfo(int64_t group_id, int64_t user_id, bool no_cache = false) {
        std::map<std::string, JsonValue> params;
        params["group_id"] = JsonValue(group_id);
        params["user_id"] = JsonValue(user_id);
        params["no_cache"] = JsonValue(no_cache);
        return callApi("get_group_member_info", JsonValue(params));
    }
    
    std::string getGroupMemberList(int64_t group_id) {
        std::map<std::string, JsonValue> params;
        params["group_id"] = JsonValue(group_id);
        return callApi("get_group_member_list", JsonValue(params));
    }
    
    std::string getGroupHonorInfo(int64_t group_id, const std::string& type = "all") {
        std::map<std::string, JsonValue> params;
        params["group_id"] = JsonValue(group_id);
        params["type"] = JsonValue(type);
        return callApi("get_group_honor_info", JsonValue(params));
    }
    
    std::string getStatus() {
        return callApi("get_status", JsonValue(std::map<std::string, JsonValue>{}));
    }
    
    std::string getVersionInfo() {
        return callApi("get_version_info", JsonValue(std::map<std::string, JsonValue>{}));
    }
    
    std::string canSendImage() {
        return callApi("can_send_image", JsonValue(std::map<std::string, JsonValue>{}));
    }
    
    std::string canSendRecord() {
        return callApi("can_send_record", JsonValue(std::map<std::string, JsonValue>{}));
    }
    
    void callApiWithCallback(const std::string& action, const JsonValue& params, ResponseCallback callback) {
        std::string echo = callApi(action, params);
        if (callback) {
            std::lock_guard<std::mutex> lock(callbacks_mutex_);
            callbacks_[echo] = std::move(callback);
        }
    }
    
    static MessageSegment text(const std::string& text) {
        MessageSegment seg;
        seg.type = "text";
        seg.data["text"] = text;
        return seg;
    }
    
    static MessageSegment face(int32_t id) {
        MessageSegment seg;
        seg.type = "face";
        seg.data["id"] = std::to_string(id);
        return seg;
    }
    
    static MessageSegment image(const std::string& file) {
        MessageSegment seg;
        seg.type = "image";
        seg.data["file"] = file;
        return seg;
    }
    
    static MessageSegment record(const std::string& file) {
        MessageSegment seg;
        seg.type = "record";
        seg.data["file"] = file;
        return seg;
    }
    
    static MessageSegment at(int64_t qq) {
        MessageSegment seg;
        seg.type = "at";
        seg.data["qq"] = std::to_string(qq);
        return seg;
    }
    
    static MessageSegment atAll() {
        MessageSegment seg;
        seg.type = "at";
        seg.data["qq"] = "all";
        return seg;
    }
    
    static MessageSegment reply(int32_t id) {
        MessageSegment seg;
        seg.type = "reply";
        seg.data["id"] = std::to_string(id);
        return seg;
    }
    
    static MessageSegment share(const std::string& url, const std::string& title, const std::string& content = "", const std::string& image = "") {
        MessageSegment seg;
        seg.type = "share";
        seg.data["url"] = url;
        seg.data["title"] = title;
        if (!content.empty()) seg.data["content"] = content;
        if (!image.empty()) seg.data["image"] = image;
        return seg;
    }
    
    static MessageSegment json(const std::string& data) {
        MessageSegment seg;
        seg.type = "json";
        seg.data["data"] = data;
        return seg;
    }
    
private:
    std::string callApi(const std::string& action, const JsonValue& params) {
        std::string echo = generateEcho();
        
        std::map<std::string, JsonValue> request;
        request["action"] = JsonValue(action);
        request["params"] = params;
        request["echo"] = JsonValue(echo);
        
        if (send_func_) {
            std::string json = JsonParser::stringify(JsonValue(request));
            LOG_INFO("[OneBotApi] Sending: " + json.substr(0, 300));
            send_func_(json);
        }
        
        return echo;
    }
    
    std::string generateEcho() {
        static std::atomic<uint64_t> counter{0};
        return "lchbot_" + std::to_string(++counter);
    }
    
    JsonValue serializeMessage(const std::vector<MessageSegment>& message) {
        std::vector<JsonValue> arr;
        for (const auto& seg : message) {
            std::map<std::string, JsonValue> seg_obj;
            seg_obj["type"] = JsonValue(seg.type);
            std::map<std::string, JsonValue> data_obj;
            for (const auto& [k, v] : seg.data) {
                data_obj[k] = JsonValue(v);
            }
            seg_obj["data"] = JsonValue(data_obj);
            arr.push_back(JsonValue(seg_obj));
        }
        return JsonValue(arr);
    }
    
    SendFunc send_func_;
    std::map<std::string, ResponseCallback> callbacks_;
    std::mutex callbacks_mutex_;
};

}
