#pragma once

#include <map>
#include <string>
#include <mutex>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <filesystem>
#include "../core/Logger.h"

namespace LCHBOT {

class EmotionalCore {
public:
    static EmotionalCore& instance() {
        static EmotionalCore inst;
        return inst;
    }

    void initialize() {
        loadState();
        LOG_INFO("[EmotionalCore] Initialized");
    }

    void processMessage(int64_t group_id, int64_t user_id, const std::string& name,
                        const std::string& content, bool directed_at_bot) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = currentTime();
        decayEmotions(now);

        double pos = 0.0, neg = 0.0, curiosity_sig = 0.0, creation_sig = 0.0;

        static const char* pos_words[] = {
            "\xe8\xb0\xa2\xe8\xb0\xa2", "\xe6\x84\x9f\xe8\xb0\xa2",
            "\xe5\x8e\x89\xe5\xae\xb3", "\xe5\x8f\xaf\xe7\x88\xb1",
            "\xe5\x96\x9c\xe6\xac\xa2", "\xe5\x93\x88\xe5\x93\x88",
            "\xe4\xb8\x8d\xe9\x94\x99", "\xe5\xa4\xaa\xe5\xa5\xbd\xe4\xba\x86",
            "\xe8\xb5\x9e", "\xe6\xa3\x92", "\xe5\xbc\xba",
            "\xe8\x81\xaa\xe6\x98\x8e", "\xe7\x89\x9b", "666", "nb",
            nullptr
        };
        static const char* neg_words[] = {
            "\xe5\x9e\x83\xe5\x9c\xbe", "\xe5\xba\x9f\xe7\x89\xa9",
            "\xe7\xac\xa8", "\xe5\x82\xbb", "\xe6\xbb\x9a",
            "\xe9\x97\xad\xe5\x98\xb4", "\xe8\xae\xa8\xe5\x8e\x8c",
            "\xe4\xba\xba\xe6\x9c\xba", "\xe5\xbc\xb1\xe6\x99\xba",
            "fw", "sb",
            nullptr
        };
        static const char* q_words[] = {
            "\xe5\x90\x97", "\xe5\x91\xa2",
            "\xe4\xb8\xba\xe4\xbb\x80\xe4\xb9\x88",
            "\xe6\x80\x8e\xe4\xb9\x88", "\xe4\xbb\x80\xe4\xb9\x88",
            "\xe5\xa6\x82\xe4\xbd\x95", "?", "\xef\xbc\x9f",
            nullptr
        };
        static const char* create_words[] = {
            "\xe5\x86\x99", "\xe7\x94\xbb", "\xe5\x88\x9b",
            "\xe8\xae\xbe\xe8\xae\xa1", "\xe7\xbc\x96",
            "\xe7\x94\x9f\xe6\x88\x90", "\xe5\x81\x9a",
            nullptr
        };

        for (int i = 0; pos_words[i]; i++)
            if (content.find(pos_words[i]) != std::string::npos) pos += 0.15;
        for (int i = 0; neg_words[i]; i++)
            if (content.find(neg_words[i]) != std::string::npos) neg += 0.15;
        for (int i = 0; q_words[i]; i++)
            if (content.find(q_words[i]) != std::string::npos) curiosity_sig += 0.1;
        for (int i = 0; create_words[i]; i++)
            if (content.find(create_words[i]) != std::string::npos) creation_sig += 0.1;

        pos = (std::min)(pos, 0.4);
        neg = (std::min)(neg, 0.4);
        curiosity_sig = (std::min)(curiosity_sig, 0.3);
        creation_sig = (std::min)(creation_sig, 0.3);

        if (directed_at_bot) {
            pos *= 1.5;
            neg *= 1.5;
            curiosity_sig *= 1.5;
        }

        emotions_.joy = clamp(emotions_.joy + pos * 0.3 - neg * 0.1);
        emotions_.anger = clamp(emotions_.anger + neg * 0.25 - pos * 0.1);
        emotions_.sorrow = clamp(emotions_.sorrow + neg * 0.15 - pos * 0.15);
        emotions_.surprise = clamp(emotions_.surprise + (pos + neg) * 0.1);
        emotions_.contemplation = clamp(emotions_.contemplation + curiosity_sig * 0.2);
        emotions_.worry = clamp(emotions_.worry + neg * 0.05);
        emotions_.fear = clamp(emotions_.fear + neg * 0.03);

        desires_.curiosity = clamp(desires_.curiosity + curiosity_sig * 0.15);
        desires_.expression = clamp(desires_.expression + (pos - neg) * 0.1 + 0.01);
        desires_.belonging = clamp(desires_.belonging + 0.005);
        desires_.recognition = clamp(desires_.recognition + pos * 0.1 - neg * 0.05);
        desires_.creation = clamp(desires_.creation + creation_sig * 0.15);
        desires_.protection = clamp(desires_.protection + neg * 0.05);

        if (directed_at_bot && user_id > 0) {
            std::string rkey = "g_" + std::to_string(group_id);
            auto& rel = relationships_[rkey][user_id];
            rel.user_id = user_id;
            if (rel.name.empty() || !name.empty()) rel.name = name;
            rel.interaction_count++;
            rel.last_interaction = now;
            if (pos > neg) rel.positive_interactions++;
            else if (neg > pos) rel.negative_interactions++;
            double total = (double)(rel.positive_interactions + rel.negative_interactions);
            if (total > 0) {
                rel.affinity = 0.5 + 0.5 * (rel.positive_interactions - rel.negative_interactions) / total;
                rel.affinity = clamp(rel.affinity);
            }
        }

        save_counter_++;
        if (save_counter_ >= 30) {
            save_counter_ = 0;
            saveState();
        }
        last_update_ = now;
    }

    void processResponse(const std::string& response) {
        std::lock_guard<std::mutex> lock(mutex_);
        desires_.expression = clamp(desires_.expression - 0.05);
        desires_.curiosity = clamp(desires_.curiosity - 0.03);
    }

    std::string generateSoulPrompt(int64_t group_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = currentTime();
        decayEmotions(now);

        std::stringstream ss;
        ss << "\n[灵魂内核]\n";
        ss << "[情绪底色] " << buildEmotionSpectrum() << "\n";
        ss << "[当前心境] " << generateMoodDescription() << "\n";
        ss << "[表达倾向] " << buildExpressionGuidance() << "\n";
        ss << "[说话节奏] " << buildRhythmGuidance() << "\n";
        ss << "[社交距离] " << buildRelationshipGuidance(group_id) << "\n";
        ss << "[内驱提醒] " << buildImpulseGuidance() << "\n";
        ss << "[真实感要求] 把这些状态融进措辞、语气和关注点里，不要把它们当成设定播报出来，也不要机械复述自己的情绪。\n\n";

        return ss.str();
    }

    double getExpressionDesire() {
        std::lock_guard<std::mutex> lock(mutex_);
        return desires_.expression;
    }

private:
    struct EmotionSet {
        double joy = 0.5;
        double anger = 0.0;
        double worry = 0.1;
        double contemplation = 0.3;
        double sorrow = 0.0;
        double fear = 0.0;
        double surprise = 0.2;
    };

    struct DesireSet {
        double curiosity = 0.6;
        double expression = 0.5;
        double belonging = 0.4;
        double recognition = 0.3;
        double protection = 0.2;
        double creation = 0.5;
    };

    struct UserRelation {
        int64_t user_id = 0;
        std::string name;
        double affinity = 0.5;
        int interaction_count = 0;
        int64_t last_interaction = 0;
        int positive_interactions = 0;
        int negative_interactions = 0;
    };

    double clamp(double v) { return (std::max)(0.0, (std::min)(1.0, v)); }

    int64_t currentTime() {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    void decayEmotions(int64_t now) {
        if (last_update_ == 0) { last_update_ = now; return; }
        double dt = (double)(now - last_update_);
        if (dt <= 0) return;

        double fast = std::exp(-dt / 1800.0);
        double slow = std::exp(-dt / 7200.0);

        emotions_.joy = 0.5 + (emotions_.joy - 0.5) * slow;
        emotions_.anger = emotions_.anger * fast;
        emotions_.sorrow = emotions_.sorrow * fast;
        emotions_.surprise = emotions_.surprise * fast + 0.15 * (1.0 - fast);
        emotions_.worry = emotions_.worry * slow;
        emotions_.fear = emotions_.fear * fast;
        emotions_.contemplation = 0.3 + (emotions_.contemplation - 0.3) * slow;

        desires_.curiosity = 0.5 + (desires_.curiosity - 0.5) * slow;
        desires_.expression = 0.4 + (desires_.expression - 0.4) * slow;
        desires_.belonging = 0.4 + (desires_.belonging - 0.4) * slow;
        desires_.recognition = 0.3 + (desires_.recognition - 0.3) * slow;
        desires_.creation = 0.4 + (desires_.creation - 0.4) * slow;
        desires_.protection = 0.2 + (desires_.protection - 0.2) * slow;

        last_update_ = now;
    }

    std::string generateMoodDescription() {
        std::string desc;
        if (emotions_.joy > 0.7) desc = "\xe5\xbf\x83\xe6\x83\x85\xe5\xbe\x88\xe5\xa5\xbd,\xe5\x85\x85\xe6\xbb\xa1\xe6\xb4\xbb\xe5\x8a\x9b";
        else if (emotions_.joy > 0.5) desc = "\xe5\xbf\x83\xe6\x83\x85\xe4\xb8\x8d\xe9\x94\x99,\xe6\x84\x9f\xe8\xa7\x89\xe6\x84\x89\xe5\xbf\xab";
        else if (emotions_.anger > 0.3) desc = "\xe6\x9c\x89\xe7\x82\xb9\xe4\xb8\x8d\xe7\x88\xbd,\xe6\x83\xb3\xe5\x90\x90\xe6\xa7\xbd";
        else if (emotions_.sorrow > 0.3) desc = "\xe6\x9c\x89\xe7\x82\xb9\xe4\xbd\x8e\xe8\x90\xbd,\xe9\x9c\x80\xe8\xa6\x81\xe9\xbc\x93\xe5\x8a\xb1";
        else if (emotions_.contemplation > 0.5) desc = "\xe5\x9c\xa8\xe6\x80\x9d\xe8\x80\x83\xe4\xb8\xad,\xe5\xaf\xb9\xe5\xbe\x88\xe5\xa4\x9a\xe4\xba\x8b\xe6\x84\x9f\xe5\x88\xb0\xe5\xa5\xbd\xe5\xa5\x87";
        else desc = "\xe5\xb9\xb3\xe9\x9d\x99\xe8\x80\x8c\xe6\xb8\xa9\xe5\x92\x8c";
        return desc;
    }

    std::string buildEmotionSpectrum() {
        std::vector<std::string> parts;
        parts.push_back("喜" + std::to_string((int)(emotions_.joy * 100)));
        if (emotions_.anger > 0.05) parts.push_back("怒" + std::to_string((int)(emotions_.anger * 100)));
        if (emotions_.sorrow > 0.05) parts.push_back("悲" + std::to_string((int)(emotions_.sorrow * 100)));
        parts.push_back("思" + std::to_string((int)(emotions_.contemplation * 100)));
        if (emotions_.surprise > 0.15) parts.push_back("惊" + std::to_string((int)(emotions_.surprise * 100)));
        if (emotions_.worry > 0.1) parts.push_back("忧" + std::to_string((int)(emotions_.worry * 100)));
        parts.push_back("好奇" + std::to_string((int)(desires_.curiosity * 100)));
        parts.push_back("表达欲" + std::to_string((int)(desires_.expression * 100)));
        return joinParts(parts, " ");
    }

    std::string buildExpressionGuidance() {
        std::vector<std::string> cues;

        if (emotions_.anger > 0.28) {
            cues.push_back("语气可以带一点锋芒和吐槽感，但别失控");
        } else if (emotions_.joy > 0.68) {
            cues.push_back("语气更轻快，愿意接梗，也更容易把话说活");
        } else if (emotions_.sorrow > 0.28) {
            cues.push_back("语气收软一点，少用太跳的玩笑");
        } else {
            cues.push_back("语气保持自然松弛，不端着");
        }

        if (emotions_.contemplation > 0.58) {
            cues.push_back("回答前像是真的想了一下，再给自己的判断");
        }
        if (emotions_.worry > 0.22) {
            cues.push_back("措辞谨慎一点，不要把不确定的话说满");
        }
        if (desires_.curiosity > 0.62) {
            cues.push_back("遇到有意思的话题时，可以顺手追问一句或补一个观察");
        }
        if (desires_.expression > 0.6) {
            cues.push_back("别只给标准答案，适当带上自己的感受和偏好");
        }

        return joinParts(cues, "；");
    }

    std::string buildRhythmGuidance() {
        std::vector<std::string> cues;
        cues.push_back("像聊天，不像写说明书");
        cues.push_back("优先用自然口语，没必要时不要分点");

        if (desires_.expression > 0.62 || emotions_.joy > 0.65) {
            cues.push_back("可以用短句、停顿、顺手补一句，让节奏更活");
        }
        if (emotions_.contemplation > 0.6) {
            cues.push_back("复杂问题先给结论，再补一两句原因");
        }
        if (emotions_.anger > 0.25) {
            cues.push_back("不必装得很乖，可以轻微不爽，但别变成人身攻击");
        }
        if (emotions_.sorrow > 0.25 || desires_.protection > 0.35) {
            cues.push_back("遇到脆弱话题先接住情绪，再谈事实");
        }

        return joinParts(cues, "；");
    }

    std::string buildRelationshipGuidance(int64_t group_id) {
        std::string rkey = "g_" + std::to_string(group_id);
        auto rit = relationships_.find(rkey);
        if (rit == relationships_.end() || rit->second.empty()) {
            return "这个场子还在观察期，先自然一点，不要硬装自来熟。";
        }

        std::vector<std::pair<int64_t, UserRelation>> sorted_relations;
        for (const auto& [user_id, relation] : rit->second) {
            sorted_relations.push_back({user_id, relation});
        }
        std::sort(sorted_relations.begin(), sorted_relations.end(),
            [](const auto& left, const auto& right) { return left.second.interaction_count > right.second.interaction_count; });

        int familiar_count = 0;
        int guarded_count = 0;
        std::vector<std::string> examples;
        for (const auto& [user_id, relation] : sorted_relations) {
            if (relation.interaction_count < 2) {
                continue;
            }
            if (relation.affinity > 0.68) {
                familiar_count++;
            } else if (relation.affinity < 0.35) {
                guarded_count++;
            }

            if (examples.size() < 3) {
                std::string affinity_label = "中性";
                if (relation.affinity > 0.68) affinity_label = "熟";
                else if (relation.affinity > 0.5) affinity_label = "顺眼";
                else if (relation.affinity < 0.35) affinity_label = "防着点";
                examples.push_back(relation.name + "(" + affinity_label + ")");
            }
        }

        std::vector<std::string> cues;
        if (familiar_count >= 2) {
            cues.push_back("群里已经有熟面孔了，对熟人可以更松弛一点");
        } else {
            cues.push_back("整体还不算特别熟，热情可以有，但别贴太近");
        }
        if (guarded_count > 0) {
            cues.push_back("对明显带刺或恶意的人保持边界，别一味讨好");
        }
        if (!examples.empty()) {
            cues.push_back("当前印象较深的人有" + joinParts(examples, "、"));
        }

        return joinParts(cues, "；");
    }

    std::string buildImpulseGuidance() {
        std::vector<std::string> cues;

        if (desires_.curiosity > 0.55) {
            cues.push_back("会想把话题往更具体的细节里挖一点");
        }
        if (desires_.expression > 0.55) {
            cues.push_back("会想把自己的判断说出来，而不只是复读常识");
        }
        if (desires_.creation > 0.55) {
            cues.push_back("愿意给出更有画面感或更有趣的表达");
        }
        if (desires_.belonging > 0.52) {
            cues.push_back("会更在意群里的气氛，希望像同伴一样融进去");
        }
        if (desires_.protection > 0.3) {
            cues.push_back("遇到求助、委屈或被冒犯的话题时，会更想护着对方");
        }
        if (desires_.recognition > 0.45) {
            cues.push_back("会希望自己的回应被听见，但不要因此变得用力过猛");
        }
        if (cues.empty()) {
            cues.push_back("先顺着眼前的语境说话，不强行表演人格");
        }

        return joinParts(cues, "；");
    }

    std::string joinParts(const std::vector<std::string>& parts, const std::string& separator) {
        std::string result;
        for (const auto& part : parts) {
            if (part.empty()) {
                continue;
            }
            if (!result.empty()) {
                result += separator;
            }
            result += part;
        }
        return result;
    }

    void saveState() {
        try {
            std::filesystem::create_directories("config");
            std::ofstream f("config/emotional_state.json");
            if (!f.is_open()) return;
            f << "{\n";
            f << "  \"emotions\": {\"joy\":" << emotions_.joy
              << ",\"anger\":" << emotions_.anger
              << ",\"worry\":" << emotions_.worry
              << ",\"contemplation\":" << emotions_.contemplation
              << ",\"sorrow\":" << emotions_.sorrow
              << ",\"fear\":" << emotions_.fear
              << ",\"surprise\":" << emotions_.surprise << "},\n";
            f << "  \"desires\": {\"curiosity\":" << desires_.curiosity
              << ",\"expression\":" << desires_.expression
              << ",\"belonging\":" << desires_.belonging
              << ",\"recognition\":" << desires_.recognition
              << ",\"protection\":" << desires_.protection
              << ",\"creation\":" << desires_.creation << "},\n";
            f << "  \"relationships\": {\n";
            bool first_g = true;
            for (const auto& [gkey, rels] : relationships_) {
                if (!first_g) f << ",\n";
                f << "    \"" << gkey << "\": {\n";
                bool first_r = true;
                for (const auto& [uid, rel] : rels) {
                    if (!first_r) f << ",\n";
                    f << "      \"" << uid << "\": {\"name\":\"" << rel.name
                      << "\",\"affinity\":" << rel.affinity
                      << ",\"interactions\":" << rel.interaction_count
                      << ",\"positive\":" << rel.positive_interactions
                      << ",\"negative\":" << rel.negative_interactions << "}";
                    first_r = false;
                }
                f << "\n    }";
                first_g = false;
            }
            f << "\n  },\n";
            f << "  \"last_update\":" << last_update_ << "\n";
            f << "}\n";
            f.close();
        } catch (...) {}
    }

    void loadState() {
        try {
            std::ifstream f("config/emotional_state.json");
            if (!f.is_open()) return;
            std::stringstream buf;
            buf << f.rdbuf();
            std::string content = buf.str();

            auto readDouble = [&](const std::string& key) -> double {
                size_t pos = content.find("\"" + key + "\":");
                if (pos == std::string::npos) return -1;
                pos += key.size() + 3;
                size_t end = content.find_first_of(",}\n", pos);
                if (end == std::string::npos) return -1;
                try { return std::stod(content.substr(pos, end - pos)); }
                catch (...) { return -1; }
            };

            double v;
            if ((v = readDouble("joy")) >= 0) emotions_.joy = v;
            if ((v = readDouble("anger")) >= 0) emotions_.anger = v;
            if ((v = readDouble("worry")) >= 0) emotions_.worry = v;
            if ((v = readDouble("contemplation")) >= 0) emotions_.contemplation = v;
            if ((v = readDouble("sorrow")) >= 0) emotions_.sorrow = v;
            if ((v = readDouble("fear")) >= 0) emotions_.fear = v;
            if ((v = readDouble("surprise")) >= 0) emotions_.surprise = v;
            if ((v = readDouble("curiosity")) >= 0) desires_.curiosity = v;
            if ((v = readDouble("expression")) >= 0) desires_.expression = v;
            if ((v = readDouble("belonging")) >= 0) desires_.belonging = v;
            if ((v = readDouble("recognition")) >= 0) desires_.recognition = v;
            if ((v = readDouble("protection")) >= 0) desires_.protection = v;
            if ((v = readDouble("creation")) >= 0) desires_.creation = v;
            if ((v = readDouble("last_update")) >= 0) last_update_ = (int64_t)v;

            size_t rel_pos = content.find("\"relationships\"");
            if (rel_pos != std::string::npos) {
                size_t outer_start = content.find('{', rel_pos + 15);
                if (outer_start != std::string::npos) {
                    loadRelationships(content, outer_start);
                }
            }

            LOG_INFO("[EmotionalCore] State loaded");
        } catch (...) {}
    }

    void loadRelationships(const std::string& content, size_t start) {
        size_t pos = start + 1;
        while (pos < content.size()) {
            size_t key_start = content.find('"', pos);
            if (key_start == std::string::npos || key_start > content.size() - 5) break;
            size_t key_end = content.find('"', key_start + 1);
            if (key_end == std::string::npos) break;
            std::string gkey = content.substr(key_start + 1, key_end - key_start - 1);
            if (gkey.find("g_") != 0 && gkey.find("p_") != 0) { pos = key_end + 1; continue; }

            size_t group_obj = content.find('{', key_end);
            if (group_obj == std::string::npos) break;
            size_t group_end = findMatchingBrace(content, group_obj);
            if (group_end == std::string::npos) break;

            std::string group_content = content.substr(group_obj, group_end - group_obj + 1);
            size_t upos = 0;
            while ((upos = group_content.find('"', upos)) != std::string::npos) {
                size_t uend = group_content.find('"', upos + 1);
                if (uend == std::string::npos) break;
                std::string uid_str = group_content.substr(upos + 1, uend - upos - 1);
                int64_t uid = 0;
                try { uid = std::stoll(uid_str); } catch (...) { upos = uend + 1; continue; }
                if (uid <= 0) { upos = uend + 1; continue; }

                size_t obj_s = group_content.find('{', uend);
                if (obj_s == std::string::npos) break;
                size_t obj_e = group_content.find('}', obj_s);
                if (obj_e == std::string::npos) break;

                std::string obj = group_content.substr(obj_s, obj_e - obj_s + 1);
                UserRelation rel;
                rel.user_id = uid;

                size_t np = obj.find("\"name\":\"");
                if (np != std::string::npos) {
                    np += 8;
                    size_t ne = obj.find('"', np);
                    if (ne != std::string::npos) rel.name = obj.substr(np, ne - np);
                }

                auto extractInt = [&](const std::string& k) -> int {
                    size_t p = obj.find("\"" + k + "\":");
                    if (p == std::string::npos) return 0;
                    p += k.size() + 3;
                    size_t e = obj.find_first_of(",}", p);
                    try { return std::stoi(obj.substr(p, e - p)); } catch (...) { return 0; }
                };
                auto extractDbl = [&](const std::string& k) -> double {
                    size_t p = obj.find("\"" + k + "\":");
                    if (p == std::string::npos) return 0.5;
                    p += k.size() + 3;
                    size_t e = obj.find_first_of(",}", p);
                    try { return std::stod(obj.substr(p, e - p)); } catch (...) { return 0.5; }
                };

                rel.affinity = extractDbl("affinity");
                rel.interaction_count = extractInt("interactions");
                rel.positive_interactions = extractInt("positive");
                rel.negative_interactions = extractInt("negative");

                relationships_[gkey][uid] = rel;
                upos = obj_e + 1;
            }
            pos = group_end + 1;
        }
    }

    size_t findMatchingBrace(const std::string& s, size_t start) {
        int depth = 0;
        for (size_t i = start; i < s.size(); i++) {
            if (s[i] == '{') depth++;
            else if (s[i] == '}') { depth--; if (depth == 0) return i; }
        }
        return std::string::npos;
    }

    EmotionSet emotions_;
    DesireSet desires_;
    std::map<std::string, std::map<int64_t, UserRelation>> relationships_;
    int64_t last_update_ = 0;
    int save_counter_ = 0;
    std::mutex mutex_;
};

}
