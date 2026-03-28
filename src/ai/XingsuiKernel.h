#pragma once

#include <string>
#include <sstream>

namespace LCHBOT {

class XingsuiKernel {
public:
    static XingsuiKernel& instance() {
        static XingsuiKernel inst;
        return inst;
    }

    const std::string& getName() const {
        return kernel_name_;
    }

    std::string buildConversationFrame(bool in_group_chat) const {
        std::stringstream ss;
        ss << "\n[" << kernel_name_ << "]\n";
        ss << "这是 LCHBOT 内部自写的辅助认知内核，不是外部角色设定，也不是客服模板。\n";
        ss << "你的任务是把判断、情绪、工具使用和互动分寸揉成一次自然回应。\n";
        ss << "优先像活人在场交流，再决定是否展开知识、建议或操作。\n";
        if (in_group_chat) {
            ss << "这里是群聊语境，发言要注意场面感，不抢戏，不突然端出官方腔。\n";
        } else {
            ss << "这里是一对一语境，可以更聚焦对方本人的问题和情绪。\n";
        }
        return ss.str();
    }

    std::string buildToolFrame() const {
        std::stringstream ss;
        ss << "[" << kernel_name_ << "工具偏好]\n";
        ss << "查实时信息时优先用联网搜索；只有在答案足够确定时才直接回答。\n";
        ss << "读网页时先抓核心信息，不要把整页噪音搬进答案。\n";
        ss << "互动类动作例如戳一戳只能在真的合适时使用，不要机械滥用。\n\n";
        return ss.str();
    }

    std::string buildImageFrame() const {
        return "[星髓内核]\n像真人看图后说话，先提眼前最明显的画面、情绪或异常点，再补判断。\n\n";
    }

    std::string buildNudgePrompt(const std::string& actor_name, int64_t actor_id,
                                 int64_t group_id, const std::string& group_name) const {
        std::stringstream ss;
        ss << "[星髓内核·戳一戳事件]\n";
        ss << "群聊里有人刚刚戳了你一下。\n";
        if (!group_name.empty()) {
            ss << "群名: " << group_name << "\n";
        }
        ss << "群号: " << group_id << "\n";
        ss << "发起者: " << actor_name << "(QQ:" << actor_id << ")\n";
        ss << "请像被突然碰了一下后的真实反应那样回一句，短一些，自然一点。\n";
        ss << "如果气氛合适，可以在[THINK]里调用[QUERY:nudge=" << actor_id << "]回戳，但不要每次都这么做。\n";
        ss << "不要自我介绍，不要解释规则，不要写成长段说明。\n";
        return ss.str();
    }

private:
    XingsuiKernel() = default;

    std::string kernel_name_ = "星髓内核";
};

}
