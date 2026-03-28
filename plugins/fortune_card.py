import base64
import hashlib
import random
import re
import unicodedata
from datetime import datetime
from io import BytesIO

try:
    from PIL import Image, ImageDraw, ImageFont
    HAS_PIL = True
except Exception:
    HAS_PIL = False


class FortuneCardPlugin(LCHBotPlugin):
    def __init__(self):
        super().__init__()
        self.name = "FortuneCardPlugin"
        self.version = "1.0.0"
        self.author = "LCHBOT"
        self.description = "今日运势卡片"
        self.priority = 85
        self.colors = ["绯樱粉", "深海蓝", "琥珀金", "薄荷绿", "月光银", "流霞橙", "星雾紫"]
        self.items = ["奶茶", "耳机", "硬币", "手账本", "围巾", "钥匙扣", "小蛋糕", "便签纸"]
        self.blessings = [
            "适合推进拖延已久的小事。",
            "今天适合主动出击，不适合犹豫。",
            "会有意料之外的好消息靠近你。",
            "适合整理情绪，也适合整理桌面。",
            "今天要少内耗，多执行。",
            "适合联机、聊天、扩列或者约饭。",
            "先把手头最麻烦的一件事拿下。",
        ]
        self.events = [
            "会被一句话点醒",
            "抽卡/抽奖手气提升",
            "适合清任务和清 backlog",
            "容易在群里成为焦点",
            "适合发起一个新点子",
            "会突然很想整活",
            "适合补番、补档、补作业",
        ]

    def on_load(self):
        print(f"[{self.name}] 今日运势插件已加载")

    def on_message(self, event):
        message = self._normalize(event.get("raw_message", ""))
        if message not in {"今日运势", "运势", "抽运势", "/今日运势", "/运势", "/抽运势"}:
            return False

        nickname = event.get("sender", {}).get("nickname") or event.get("sender", {}).get("card") or "你"
        nickname = self._sanitize_nickname(nickname)
        fortune = self._build_fortune(event.get("user_id", 0), nickname)
        if HAS_PIL:
            image_b64 = self._render_card(fortune)
            if image_b64:
                self.reply(event, f"[CQ:image,file=base64://{image_b64}]")
                return True

        self.reply(event, self._build_text(fortune))
        return True

    def _normalize(self, message):
        text = re.sub(r"\[CQ:[^\]]+\]", "", message or "").strip()
        return text.strip()

    def _sanitize_nickname(self, nickname):
        safe_chars = []
        for char in str(nickname or ""):
            codepoint = ord(char)
            category = unicodedata.category(char)

            if codepoint > 0xFFFF:
                continue
            if 0xFE00 <= codepoint <= 0xFE0F:
                continue
            if category.startswith("C"):
                continue
            if category == "So":
                continue
            safe_chars.append(char)

        cleaned = "".join(safe_chars)
        cleaned = re.sub(r"\s+", " ", cleaned).strip(" .-_")
        if not cleaned:
            return "你"
        return cleaned[:18]

    def _build_fortune(self, user_id, nickname):
        today = datetime.now().strftime("%Y-%m-%d")
        seed_source = f"{today}:{user_id}"
        seed_value = int(hashlib.md5(seed_source.encode("utf-8")).hexdigest(), 16)
        rng = random.Random(seed_value)
        return {
            "date": today,
            "nickname": nickname,
            "overall": rng.randint(55, 99),
            "career": rng.randint(45, 99),
            "love": rng.randint(45, 99),
            "wealth": rng.randint(45, 99),
            "lucky_color": rng.choice(self.colors),
            "lucky_item": rng.choice(self.items),
            "blessing": rng.choice(self.blessings),
            "event": rng.choice(self.events),
        }

    def _build_text(self, fortune):
        return (
            f"{fortune['nickname']} 的今日运势\n"
            f"日期：{fortune['date']}\n"
            f"综合：{fortune['overall']}\n"
            f"事业：{fortune['career']}\n"
            f"感情：{fortune['love']}\n"
            f"财运：{fortune['wealth']}\n"
            f"幸运色：{fortune['lucky_color']}\n"
            f"幸运物：{fortune['lucky_item']}\n"
            f"提示：{fortune['blessing']}\n"
            f"今日事件：{fortune['event']}"
        )

    def _render_card(self, fortune):
        try:
            title_font = self._load_font(42)
            section_font = self._load_font(28)
            small_font = self._load_font(20)
            nickname_font = self._load_font(24)
            stat_font = self._load_font(24)

            meta_text = f"{fortune['nickname']}  ·  {fortune['date']}"
            meta_text = self._fit_single_line(meta_text, nickname_font, 760)

            labels = [
                ("综合", fortune["overall"], (82, 196, 255)),
                ("事业", fortune["career"], (255, 190, 92)),
                ("感情", fortune["love"], (255, 132, 180)),
                ("财运", fortune["wealth"], (142, 224, 161)),
            ]

            blessing_lines = self._wrap_text(f"提示：{fortune['blessing']}", small_font, 314)
            event_lines = self._wrap_text(f"今日事件：{fortune['event']}", small_font, 314)

            small_line_height = self._line_height(small_font) + 6
            stat_box_height = 74
            detail_box_top_padding = 18
            detail_box_bottom_padding = 18
            blessing_height = detail_box_top_padding + len(blessing_lines) * small_line_height + detail_box_bottom_padding + 22
            event_height = detail_box_top_padding + len(event_lines) * small_line_height + detail_box_bottom_padding + 22
            detail_box_height = max(blessing_height, event_height)
            info_height = stat_box_height + 18 + detail_box_height

            image_height = 220 + len(labels) * 72 + info_height + 80
            card_bottom = image_height - 30

            image = Image.new("RGB", (900, image_height), (22, 28, 44))
            draw = ImageDraw.Draw(image)

            draw.rounded_rectangle((30, 30, 870, card_bottom), radius=28, fill=(34, 42, 66))
            draw.text((60, 60), "今日运势", font=title_font, fill=(255, 255, 255))
            draw.text((60, 118), meta_text, font=nickname_font, fill=(176, 190, 220))

            y = 180
            for label, score, color in labels:
                draw.text((60, y), f"{label}", font=section_font, fill=(240, 244, 255))
                draw.rounded_rectangle((180, y + 6, 720, y + 36), radius=12, fill=(56, 67, 98))
                fill_width = int(540 * max(0, min(score, 100)) / 100)
                draw.rounded_rectangle((180, y + 6, 180 + fill_width, y + 36), radius=12, fill=color)
                draw.text((748, y - 2), str(score), font=section_font, fill=color)
                y += 72

            info_top = y + 2
            info_bottom = min(card_bottom - 30, info_top + info_height)
            left_box = (60, info_top, 438, info_top + stat_box_height)
            right_box = (462, info_top, 840, info_top + stat_box_height)
            detail_top = info_top + stat_box_height + 18
            blessing_box = (60, detail_top, 438, detail_top + detail_box_height)
            event_box = (462, detail_top, 840, detail_top + detail_box_height)

            draw.rounded_rectangle(left_box, radius=18, fill=(45, 55, 82))
            draw.rounded_rectangle(right_box, radius=18, fill=(45, 55, 82))
            draw.rounded_rectangle(blessing_box, radius=18, fill=(45, 55, 82))
            draw.rounded_rectangle(event_box, radius=18, fill=(45, 55, 82))

            draw.text((84, info_top + 16), "幸运色", font=small_font, fill=(170, 183, 216))
            draw.text((84, info_top + 42), fortune["lucky_color"], font=stat_font, fill=(255, 255, 255))
            draw.text((486, info_top + 16), "幸运物", font=small_font, fill=(170, 183, 216))
            draw.text((486, info_top + 42), fortune["lucky_item"], font=stat_font, fill=(255, 255, 255))

            draw.text((84, detail_top + 16), "提示", font=small_font, fill=(170, 183, 216))
            self._draw_wrapped_lines(draw, blessing_lines, (84, detail_top + 44), small_font, (210, 220, 245), small_line_height)
            draw.text((486, detail_top + 16), "今日事件", font=small_font, fill=(170, 183, 216))
            self._draw_wrapped_lines(draw, event_lines, (486, detail_top + 44), small_font, (210, 220, 245), small_line_height)

            buffer = BytesIO()
            image.save(buffer, format="PNG")
            return base64.b64encode(buffer.getvalue()).decode("ascii")
        except Exception as exc:
            print(f"[{self.name}] 运势卡片绘制失败: {exc}")
            return ""

    def _wrap_text(self, text, font, max_width):
        lines = []
        current = ""
        for char in text:
            trial = current + char
            if self._text_width(trial, font) <= max_width:
                current = trial
                continue
            if current:
                lines.append(current)
            current = char
        if current:
            lines.append(current)
        return lines or [""]

    def _fit_single_line(self, text, font, max_width):
        if self._text_width(text, font) <= max_width:
            return text
        trimmed = text
        while len(trimmed) > 1 and self._text_width(trimmed + "...", font) > max_width:
            trimmed = trimmed[:-1]
        return trimmed + "..."

    def _draw_wrapped_lines(self, draw, lines, position, font, fill, line_height):
        x, y = position
        for line in lines:
            draw.text((x, y), line, font=font, fill=fill)
            y += line_height
        return y

    def _text_width(self, text, font):
        left, _, right, _ = font.getbbox(text)
        return right - left

    def _line_height(self, font):
        _, top, _, bottom = font.getbbox("测试Ag")
        return bottom - top

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


register_plugin(FortuneCardPlugin())
