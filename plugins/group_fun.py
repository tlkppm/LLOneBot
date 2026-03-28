import base64
import json
import os
import random
import re
from datetime import datetime
from io import BytesIO

try:
    from PIL import Image, ImageDraw, ImageFont
    HAS_PIL = True
except Exception:
    HAS_PIL = False


class GroupFunPlugin(LCHBotPlugin):
    def __init__(self):
        super().__init__()
        self.name = "GroupFunPlugin"
        self.version = "1.0.0"
        self.author = "LCHBOT"
        self.description = "群聊轻玩法与随机工具"
        self.priority = 80
        self.data_file = os.path.join("data", "group_fun_data.json")
        self.data = {"daily_lots": {}}
        self.truth_questions = [
            "你最近最想偷偷学会的技能是什么？",
            "你手机里最舍不得删的一张图是什么？",
            "你最近一次心动是因为什么？",
            "如果立刻换一个新身份，你想变成谁？",
            "你有没有一句一直没说出口的话？",
        ]
        self.dares = [
            "现在去群里夸一位群友三句话。",
            "把你最近在听的一首歌分享出来。",
            "用五个词形容你今天的状态。",
            "发一句中二台词，但不能解释。",
            "把你下一条消息的结尾改成“喵”。",
        ]
        self.lots = [
            ("上上签", "今天做决定时会非常顺，适合直接推进。"),
            ("上签", "运气在线，适合尝试新想法。"),
            ("中签", "稳扎稳打就会有收获。"),
            ("平签", "今天宜保守，不宜头铁。"),
            ("下签", "先别急，观察一下局势再出手。"),
        ]
        self.lot_tips = [
            "宜先处理最重要的一件事。",
            "适合主动沟通，避免闷着。",
            "今天不要被情绪牵着跑。",
            "先保守，再推进。",
            "遇到犹豫时先停十秒。",
            "适合发出一个邀请。",
            "少熬夜，少冲动消费。",
        ]

    def on_load(self):
        os.makedirs("data", exist_ok=True)
        self._load_data()
        print(f"[{self.name}] 群聊玩法插件已加载")

    def on_unload(self):
        self._save_data()

    def on_message(self, event):
        message = self._normalize(event.get("raw_message", ""))
        if not message:
            return False

        if message.startswith("骰子") or message.startswith("/骰子") or message.startswith("roll") or message.startswith("/roll"):
            self._handle_roll(event, message)
            return True
        if message == "抽签" or message == "/抽签":
            self._handle_lot(event)
            return True
        if message.startswith("选择 ") or message.startswith("/选择 "):
            payload = message[3:].strip() if not message.startswith("/") else message[4:].strip()
            self._handle_choice(event, payload)
            return True
        if message == "真心话" or message == "/真心话":
            self.reply(event, "真心话题目：\n" + random.choice(self.truth_questions))
            return True
        if message == "大冒险" or message == "/大冒险":
            self.reply(event, "大冒险任务：\n" + random.choice(self.dares))
            return True

        return False

    def _normalize(self, message):
        text = re.sub(r"\[CQ:[^\]]+\]", "", message or "").strip()
        text = re.sub(r"\s+", " ", text)
        return text.strip()

    def _load_data(self):
        try:
            if os.path.exists(self.data_file):
                with open(self.data_file, "r", encoding="utf-8") as f:
                    loaded = json.load(f)
                if isinstance(loaded, dict):
                    self.data = {"daily_lots": loaded.get("daily_lots", {})}
                else:
                    self.data = {"daily_lots": {}}
        except Exception:
            self.data = {"daily_lots": {}}

    def _save_data(self):
        try:
            with open(self.data_file, "w", encoding="utf-8") as f:
                json.dump(self.data, f, ensure_ascii=False, indent=2)
        except Exception:
            pass

    def _handle_roll(self, event, message):
        normalized = message[1:] if message.startswith("/") else message
        parts = normalized.split(" ", 1)
        sides = 100
        if len(parts) > 1 and parts[1].isdigit():
            sides = max(2, min(1000, int(parts[1])))
        value = random.randint(1, sides)
        self.reply(event, f"你掷出了 1 到 {sides} 的骰子，结果是：{value}")

    def _handle_lot(self, event):
        today = datetime.now().strftime("%Y-%m-%d")
        user_id = str(event.get("user_id", 0))
        nickname = self._clean_name(
            event.get("sender", {}).get("card") or event.get("sender", {}).get("nickname") or "你"
        )

        record = self.data.get("daily_lots", {}).get(user_id)
        already_drawn = bool(record and record.get("date") == today)
        if not already_drawn:
            sign, text = random.choice(self.lots)
            tip = random.choice(self.lot_tips)
            record = {
                "date": today,
                "sign": sign,
                "text": text,
                "tip": tip,
            }
            self.data.setdefault("daily_lots", {})[user_id] = record
            self._save_data()

        if HAS_PIL:
            image_b64 = self._render_lot_card(today, nickname, record, already_drawn)
            if image_b64:
                self.reply(event, f"[CQ:image,file=base64://{image_b64}]")
                return

        prefix = "今日已抽过" if already_drawn else "今日新签"
        self.reply(
            event,
            f"{prefix}\n{record['sign']}\n{record['text']}\n提示：{record['tip']}"
        )

    def _handle_choice(self, event, payload):
        options = [item.strip() for item in re.split(r"[|/／,，]", payload) if item.strip()]
        if len(options) < 2:
            self.reply(event, "请至少提供两个选项，例如：选择 火锅 / 烧烤 / 麻辣烫")
            return
        selected = random.choice(options)
        self.reply(event, f"我替你选：{selected}")

    def _clean_name(self, name):
        cleaned = re.sub(r"[^\w\u4e00-\u9fff·_\-. ]", "", str(name or ""))
        cleaned = re.sub(r"\s+", " ", cleaned).strip(" .-_")
        return cleaned[:16] or "你"

    def _render_lot_card(self, date_text, nickname, record, already_drawn):
        try:
            image = Image.new("RGB", (900, 560), (21, 26, 40))
            draw = ImageDraw.Draw(image)
            title_font = self._load_font(42)
            body_font = self._load_font(28)
            small_font = self._load_font(20)
            sign_font = self._load_font(80)

            draw.rounded_rectangle((30, 30, 870, 530), radius=28, fill=(36, 45, 72))
            draw.text((60, 60), "今日灵签", font=title_font, fill=(255, 255, 255))
            subtitle = "今日已抽过，仅展示同一签文" if already_drawn else "今日首次抽签"
            draw.text((60, 118), f"{nickname}  ·  {date_text}", font=small_font, fill=(188, 198, 226))
            draw.text((60, 148), subtitle, font=small_font, fill=(142, 226, 184) if already_drawn else (255, 206, 120))

            draw.rounded_rectangle((60, 200, 840, 360), radius=22, fill=(248, 242, 229))
            draw.text((100, 228), record["sign"], font=sign_font, fill=(104, 60, 24))
            self._draw_multiline_text(draw, record["text"], (100, 330), body_font, (72, 48, 28), 620, 14)

            draw.rounded_rectangle((60, 394, 840, 500), radius=18, fill=(51, 62, 95))
            draw.text((84, 420), "今日提示", font=body_font, fill=(255, 255, 255))
            self._draw_multiline_text(draw, record["tip"], (84, 460), small_font, (210, 220, 245), 700, 10)

            buffer = BytesIO()
            image.save(buffer, format="PNG")
            return base64.b64encode(buffer.getvalue()).decode("ascii")
        except Exception as exc:
            print(f"[{self.name}] 抽签卡片绘制失败: {exc}")
            return ""

    def _draw_multiline_text(self, draw, text, position, font, fill, max_width, line_gap):
        x, y = position
        current = ""
        for char in text:
            trial = current + char
            bbox = draw.textbbox((0, 0), trial, font=font)
            if bbox[2] - bbox[0] <= max_width:
                current = trial
                continue
            draw.text((x, y), current, font=font, fill=fill)
            y += (bbox[3] - bbox[1]) + line_gap
            current = char
        if current:
            draw.text((x, y), current, font=font, fill=fill)

    def _load_font(self, size):
        for path in [
            "C:/Windows/Fonts/msyh.ttc",
            "C:/Windows/Fonts/simhei.ttf",
            "C:/Windows/Fonts/simsun.ttc",
        ]:
            try:
                return ImageFont.truetype(path, size)
            except Exception:
                continue
        return ImageFont.load_default()


register_plugin(GroupFunPlugin())
