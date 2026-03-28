import base64
import json
import os
import re
import ssl
import urllib.parse
import urllib.request
from datetime import datetime
from io import BytesIO

try:
    from PIL import Image, ImageDraw, ImageFilter, ImageFont
    HAS_PIL = True
except Exception:
    HAS_PIL = False

try:
    from PyQt6.QtCore import QByteArray, QBuffer, QIODevice, QRectF
    from PyQt6.QtGui import QImage, QPainter
    from PyQt6.QtSvg import QSvgRenderer
    HAS_QTSVG = True
except Exception:
    HAS_QTSVG = False


class UtilityToolsPlugin(LCHBotPlugin):
    def __init__(self):
        super().__init__()
        self.name = "UtilityToolsPlugin"
        self.version = "1.0.0"
        self.author = "LCHBOT"
        self.description = "天气查询工具"
        self.priority = 92
        self.cache_dir = os.path.join("data", "weather_cards")
        self.commands = {
            "天气 城市": "查询城市实时天气卡片",
        }
        self.weather_code_map = {
            "113": "晴",
            "116": "多云",
            "119": "阴",
            "122": "阴天",
            "143": "雾",
            "176": "局部小雨",
            "179": "雨夹雪",
            "182": "冻雨",
            "185": "冻毛雨",
            "200": "雷暴",
            "227": "吹雪",
            "230": "暴风雪",
            "248": "雾",
            "260": "浓雾",
            "263": "零星毛毛雨",
            "266": "毛毛雨",
            "281": "冻毛雨",
            "284": "强冻毛雨",
            "293": "零星小雨",
            "296": "小雨",
            "299": "中雨",
            "302": "大雨",
            "305": "阵雨",
            "308": "暴雨",
            "311": "小冻雨",
            "314": "中冻雨",
            "317": "小雨夹雪",
            "320": "中雨夹雪",
            "323": "零星小雪",
            "326": "小雪",
            "329": "中雪",
            "332": "大雪",
            "335": "暴雪",
            "338": "强暴雪",
            "350": "冰粒",
            "353": "零星阵雨",
            "356": "阵雨",
            "359": "暴雨",
            "362": "零星雨夹雪",
            "365": "雨夹雪",
            "368": "零星阵雪",
            "371": "阵雪",
            "374": "零星冰粒",
            "377": "冰粒",
            "386": "零星雷阵雨",
            "389": "雷阵雨",
            "392": "零星雷阵雪",
            "395": "雷阵雪",
        }

    def on_load(self):
        os.makedirs(self.cache_dir, exist_ok=True)
        print(f"[{self.name}] 实用工具插件已加载")

    def on_message(self, event):
        message = self._normalize(event.get("raw_message", ""))
        if not message:
            return False

        city = self._extract_argument(message, "天气")
        if city is not None:
            self._handle_weather(event, city)
            return True

        return False

    def _normalize(self, message):
        text = re.sub(r"\[CQ:[^\]]+\]", "", message or "").strip()
        text = re.sub(r"\s+", " ", text)
        return text.strip()

    def _extract_argument(self, message, command):
        for prefix in (command, "/" + command):
            if message == prefix:
                return ""
            if message.startswith(prefix + " "):
                return message[len(prefix):].strip()
        return None

    def _handle_weather(self, event, city):
        if not city:
            self.reply(event, "用法：/天气 城市")
            return

        try:
            weather = self._query_weather(city)
        except Exception as exc:
            print(f"[{self.name}] 天气查询失败: {exc}")
            self.reply(event, "天气查询失败，请稍后再试")
            return

        if not weather:
            self.reply(event, f"没有查到“{city}”的天气")
            return

        if HAS_PIL:
            image_b64 = self._render_weather_card(weather)
            if image_b64:
                self.reply(event, f"[CQ:image,file=base64://{image_b64}]")
                return

        lines = [
            f"{weather['location']} 天气",
            f"当前：{weather['description']} {weather['temp_c']}°C，体感 {weather['feels_like_c']}°C",
            f"湿度：{weather['humidity']}%  风速：{weather['wind_kmph']} km/h",
            f"今日：{weather['min_c']}°C ~ {weather['max_c']}°C，降雨概率 {weather['rain_chance']}%",
        ]
        if weather["update_time"]:
            lines.append(f"观测时间：{weather['update_time']}")
        self.reply(event, "\n".join(lines))

    def _query_weather(self, city):
        city_encoded = urllib.parse.quote(city)
        url = f"https://wttr.in/{city_encoded}?format=j1&lang=zh"
        payload = json.loads(self._http_get_text(url))

        current_list = payload.get("current_condition") or []
        forecast_list = payload.get("weather") or []
        nearest_area = payload.get("nearest_area") or []
        if not current_list or not forecast_list:
            return {}

        current = current_list[0]
        today = forecast_list[0]
        area = nearest_area[0] if nearest_area else {}
        area_name = self._pick_text(area.get("areaName"))
        country_name = self._pick_text(area.get("country"))
        astronomy = (today.get("astronomy") or [{}])[0]
        description = self._resolve_description(current)
        rain_chance = self._max_rain_chance(today.get("hourly") or [])
        location = city.strip()
        if not location:
            location = area_name or country_name or "未知地点"

        return {
            "location": location,
            "description": description,
            "temp_c": current.get("temp_C", "?"),
            "feels_like_c": current.get("FeelsLikeC", "?"),
            "humidity": current.get("humidity", "?"),
            "wind_kmph": current.get("windspeedKmph", "?"),
            "wind_dir": current.get("winddir16Point", "?"),
            "min_c": today.get("mintempC", "?"),
            "max_c": today.get("maxtempC", "?"),
            "rain_chance": rain_chance,
            "pressure": current.get("pressure", "?"),
            "visibility": current.get("visibility", "?"),
            "uv_index": current.get("uvIndex", "?"),
            "cloudcover": current.get("cloudcover", "?"),
            "precip_mm": current.get("precipMM", "?"),
            "update_time": current.get("localObsDateTime", ""),
            "icon_url": self._normalize_icon_url(self._pick_text(current.get("weatherIconUrl"))),
            "weather_code": str(current.get("weatherCode", "")),
            "is_night": self._is_night_icon(current.get("weatherIconUrl")),
            "sunrise": astronomy.get("sunrise", ""),
            "sunset": astronomy.get("sunset", ""),
            "hourly": self._build_hourly(today.get("hourly") or []),
        }

    def _http_get_text(self, url):
        headers = {
            "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/131.0 Safari/537.36",
            "Accept-Language": "zh-CN,zh;q=0.9,en;q=0.8",
        }
        request = urllib.request.Request(url, headers=headers, method="GET")
        ssl_context = ssl._create_unverified_context()
        with urllib.request.urlopen(request, timeout=20, context=ssl_context) as response:
            charset = response.headers.get_content_charset() or "utf-8"
            return response.read().decode(charset, errors="ignore")

    def _pick_text(self, value):
        if isinstance(value, list) and value:
            item = value[0]
            if isinstance(item, dict):
                text = item.get("value", "")
                return re.sub(r"\s+", " ", str(text)).strip()
        if isinstance(value, str):
            return re.sub(r"\s+", " ", value).strip()
        return ""

    def _max_rain_chance(self, hourly_list):
        values = []
        for item in hourly_list:
            chance = item.get("chanceofrain")
            if str(chance).isdigit():
                values.append(int(chance))
        return max(values) if values else 0

    def _resolve_description(self, current):
        lang_text = self._pick_text(current.get("lang_zh"))
        if lang_text and self._looks_like_chinese(lang_text):
            return lang_text

        code = str(current.get("weatherCode", ""))
        if code in self.weather_code_map:
            return self.weather_code_map[code]

        desc = self._pick_text(current.get("weatherDesc"))
        return desc or "未知"

    def _looks_like_chinese(self, text):
        return any("\u4e00" <= char <= "\u9fff" for char in str(text or ""))

    def _normalize_icon_url(self, url):
        text = str(url or "").strip()
        if not text:
            return ""
        if text.startswith("//"):
            return "https:" + text
        return text

    def _build_hourly(self, hourly_list):
        cards = []
        if not hourly_list:
            return cards

        seen = set()
        preferred_times = {"0600", "0900", "1200", "1500", "1800", "2100"}
        for item in hourly_list:
            raw_time = str(item.get("time", "")).zfill(4)
            if raw_time not in preferred_times or raw_time in seen:
                continue
            seen.add(raw_time)
            cards.append({
                "time": f"{raw_time[:2]}:{raw_time[2:]}",
                "temp_c": item.get("tempC", "?"),
                "description": self._resolve_hourly_description(item),
                "rain_chance": item.get("chanceofrain", "0"),
                "icon_url": self._normalize_icon_url(self._pick_text(item.get("weatherIconUrl"))),
                "weather_code": str(item.get("weatherCode", "")),
                "is_night": self._is_night_hour(raw_time),
            })
            if len(cards) >= 4:
                break

        if not cards:
            sample = hourly_list[:4]
            for item in sample:
                raw_time = str(item.get("time", "")).zfill(4)
                cards.append({
                    "time": f"{raw_time[:2]}:{raw_time[2:]}",
                    "temp_c": item.get("tempC", "?"),
                    "description": self._resolve_hourly_description(item),
                    "rain_chance": item.get("chanceofrain", "0"),
                    "icon_url": self._normalize_icon_url(self._pick_text(item.get("weatherIconUrl"))),
                    "weather_code": str(item.get("weatherCode", "")),
                    "is_night": self._is_night_hour(raw_time),
                })

        return cards

    def _resolve_hourly_description(self, item):
        lang_text = self._pick_text(item.get("lang_zh"))
        if lang_text and self._looks_like_chinese(lang_text):
            return lang_text
        code = str(item.get("weatherCode", ""))
        if code in self.weather_code_map:
            return self.weather_code_map[code]
        return self._pick_text(item.get("weatherDesc")) or "未知"

    def _render_weather_card(self, weather):
        try:
            width, height = 1320, 880
            scale = 2
            scaled_width = width * scale
            scaled_height = height * scale
            background_top, background_bottom, panel_fill, accent = self._pick_palette(weather)

            image = Image.new("RGBA", (scaled_width, scaled_height), background_top)
            self._draw_vertical_gradient(image, background_top, background_bottom)
            draw = ImageDraw.Draw(image)
            sv = lambda value: int(round(value * scale))

            title_font = self._load_font(sv(48), "bold")
            desc_font = self._load_font(sv(28), "regular")
            temp_font = self._load_font(sv(124), "bold")
            body_font = self._load_font(sv(24), "regular")
            label_font = self._load_font(sv(19), "light")
            small_font = self._load_font(sv(18), "regular")
            metric_value_font = self._load_font(sv(30), "bold")
            metric_title_font = self._load_font(sv(17), "light")
            metric_subtitle_font = self._load_font(sv(16), "regular")
            hourly_temp_font = self._load_font(sv(26), "bold")
            hourly_desc_font = self._load_font(sv(20), "regular")

            self._draw_blurred_shadow(image, (30, 28, width - 30, height - 28), 38, 14, (6, 12, 24, 96), scale)
            self._draw_glass_panel(draw, (30, 28, width - 30, height - 28), 38, (8, 16, 30, 212), (255, 255, 255, 24), scale)
            self._draw_glass_panel(draw, (54, 56, 782, 548), 32, panel_fill, (255, 255, 255, 28), scale)
            self._draw_glass_panel(draw, (808, 56, width - 54, 548), 32, (14, 24, 44, 214), (255, 255, 255, 24), scale)
            self._draw_glass_panel(draw, (54, 560, width - 54, height - 54), 30, (11, 21, 38, 212), (255, 255, 255, 24), scale)

            draw.text((sv(90), sv(92)), weather["location"], font=title_font, fill=(246, 249, 255))
            draw.text((sv(92), sv(154)), weather["description"], font=desc_font, fill=accent)
            draw.text((sv(92), sv(194)), f"观测时间 {self._format_observation_time(weather['update_time'])}", font=label_font, fill=(188, 203, 225))

            draw.text((sv(86), sv(238)), f"{weather['temp_c']}°", font=temp_font, fill=(255, 255, 255))
            self._draw_stat_chip(draw, (92, 384, 308, 456), "体感温度", f"{weather['feels_like_c']}°C", label_font, body_font, scale)
            self._draw_stat_chip(draw, (332, 384, 574, 456), "今日温差", f"{weather['min_c']}°C ~ {weather['max_c']}°C", label_font, body_font, scale)
            self._draw_stat_chip(draw, (92, 470, 352, 542), "天气状态", self._summarize_weather_text(weather["description"]), label_font, body_font, scale)

            self._draw_icon_showcase(draw, image, weather, (562, 134, 740, 352), accent, scale)
            self._draw_daylight_capsules(draw, weather, label_font, body_font, scale)

            metric_cards = [
                ("降雨概率", f"{weather['rain_chance']}%", "今日最高"),
                ("湿度", f"{weather['humidity']}%", "空气含湿"),
                ("风速", f"{weather['wind_kmph']} km/h", weather["wind_dir"] or "风向未知"),
                ("能见度", f"{weather['visibility']} km", f"气压 {weather['pressure']} hPa"),
                ("云量", f"{weather['cloudcover']}%", f"UV {weather['uv_index']}"),
                ("降水量", f"{weather['precip_mm']} mm", "当前累计"),
            ]
            self._draw_metric_grid(
                draw,
                metric_cards,
                836,
                94,
                190,
                108,
                18,
                18,
                metric_title_font,
                metric_value_font,
                metric_subtitle_font,
                scale,
            )

            draw.text((sv(92), sv(590)), "未来时段", font=desc_font, fill=(246, 249, 255))
            self._draw_hourly_cards(
                image,
                weather.get("hourly") or [],
                92,
                636,
                width - 184,
                170,
                20,
                label_font,
                hourly_temp_font,
                hourly_desc_font,
                small_font,
                scale,
            )

            final_image = image.resize((width, height), Image.Resampling.LANCZOS)
            buffer = BytesIO()
            final_image.convert("RGB").save(buffer, format="PNG")
            return base64.b64encode(buffer.getvalue()).decode("ascii")
        except Exception as exc:
            print(f"[{self.name}] 天气卡片渲染失败: {exc}")
            return ""

    def _pick_palette(self, weather):
        description = weather.get("description", "")
        if "雷" in description:
            return ((24, 30, 64), (70, 79, 126), (18, 26, 48, 222), (255, 212, 126))
        if "雪" in description:
            return ((95, 126, 158), (196, 220, 238), (233, 241, 248, 222), (76, 114, 152))
        if "雨" in description:
            return ((28, 52, 88), (75, 116, 166), (18, 32, 58, 220), (142, 210, 255))
        if "云" in description or "阴" in description:
            return ((74, 88, 109), (126, 142, 164), (22, 33, 52, 220), (201, 216, 236))
        if "雾" in description or "霾" in description:
            return ((86, 96, 110), (146, 157, 171), (26, 35, 52, 220), (219, 226, 237))
        if weather.get("is_night"):
            return ((18, 28, 54), (46, 73, 118), (14, 24, 46, 222), (255, 218, 126))
        return ((40, 104, 182), (122, 183, 235), (22, 44, 76, 212), (255, 224, 139))

    def _draw_vertical_gradient(self, image, top_color, bottom_color):
        width, height = image.size
        draw = ImageDraw.Draw(image)
        for y in range(height):
            ratio = y / max(height - 1, 1)
            color = tuple(
                int(top_color[i] + (bottom_color[i] - top_color[i]) * ratio)
                for i in range(3)
            )
            draw.line((0, y, width, y), fill=color)

    def _draw_metric_grid(self, draw, cards, start_x, start_y, card_width, card_height, gap_x, gap_y, title_font, value_font, subtitle_font, scale):
        for index, (title, value, subtitle) in enumerate(cards):
            row = index // 2
            col = index % 2
            x1 = self._scaled(start_x + col * (card_width + gap_x), scale)
            y1 = self._scaled(start_y + row * (card_height + gap_y), scale)
            x2 = self._scaled(start_x + col * (card_width + gap_x) + card_width, scale)
            y2 = self._scaled(start_y + row * (card_height + gap_y) + card_height, scale)
            draw.rounded_rectangle((x1, y1, x2, y2), radius=self._scaled(24, scale), fill=(245, 249, 255, 238), outline=(255, 255, 255, 48), width=self._scaled(1, scale))
            draw.text((x1 + self._scaled(18, scale), y1 + self._scaled(14, scale)), title, font=title_font, fill=(98, 123, 162))
            fitted_value_font = self._fit_font(draw, value, value_font, self._scaled(card_width - 36, scale), self._font_size(value_font, scale, 20))
            draw.text((x1 + self._scaled(18, scale), y1 + self._scaled(40, scale)), value, font=fitted_value_font, fill=(19, 38, 70))
            fitted_subtitle_font = self._fit_font(draw, subtitle, subtitle_font, self._scaled(card_width - 36, scale), self._font_size(subtitle_font, scale, 14))
            draw.text((x1 + self._scaled(18, scale), y1 + self._scaled(78, scale)), subtitle, font=fitted_subtitle_font, fill=(193, 122, 18))

    def _draw_hourly_cards(self, image, hourly_cards, start_x, start_y, total_width, card_height, gap, label_font, temp_font, desc_font, foot_font, scale):
        if not hourly_cards:
            draw = ImageDraw.Draw(image)
            draw.text((self._scaled(start_x, scale), self._scaled(start_y, scale)), "天气源没有返回分时数据", font=desc_font, fill=(220, 228, 241))
            return

        card_width = int((total_width - gap * 3) / 4)
        draw = ImageDraw.Draw(image)
        for index, item in enumerate(hourly_cards[:4]):
            logical_x1 = start_x + index * (card_width + gap)
            logical_y1 = start_y
            logical_x2 = logical_x1 + card_width
            logical_y2 = logical_y1 + card_height
            x1 = self._scaled(logical_x1, scale)
            y1 = self._scaled(logical_y1, scale)
            x2 = self._scaled(logical_x2, scale)
            y2 = self._scaled(logical_y2, scale)
            draw.rounded_rectangle((x1, y1, x2, y2), radius=self._scaled(28, scale), fill=(245, 249, 255, 238), outline=(255, 255, 255, 48), width=self._scaled(1, scale))
            draw.text((x1 + self._scaled(22, scale), y1 + self._scaled(18, scale)), item["time"], font=label_font, fill=(109, 124, 155))
            draw.text((x1 + self._scaled(22, scale), y1 + self._scaled(54, scale)), f"{item['temp_c']}°C", font=temp_font, fill=(195, 124, 18))

            chip_left = x2 - self._scaled(104, scale)
            chip_top = y1 + self._scaled(16, scale)
            chip_right = x2 - self._scaled(22, scale)
            chip_bottom = y1 + self._scaled(98, scale)
            draw.rounded_rectangle((chip_left, chip_top, chip_right, chip_bottom), radius=self._scaled(22, scale), fill=(229, 239, 255), outline=(211, 225, 244), width=self._scaled(1, scale))
            icon = self._fetch_icon_image(item, 56, (73, 108, 160, 255))
            if icon:
                icon_x = chip_left + (chip_right - chip_left - icon.width) // 2
                icon_y = chip_top + (chip_bottom - chip_top - icon.height) // 2
                image.alpha_composite(icon, (icon_x, icon_y))

            desc = self._clamp_text(item["description"], 7)
            desc_y = y2 - self._scaled(56, scale)
            foot_y = y2 - self._scaled(28, scale)
            draw.text((x1 + self._scaled(22, scale), desc_y), desc, font=desc_font, fill=(78, 97, 130))
            draw.text((x1 + self._scaled(22, scale), foot_y), f"降雨 {item['rain_chance']}%", font=foot_font, fill=(105, 129, 166))

    def _fetch_icon_image(self, weather_item, size, tint=None):
        if isinstance(weather_item, dict):
            icon_key = self._map_fluent_icon(weather_item)
        else:
            icon_key = ""
        if not icon_key:
            return None
        try:
            cache_path = self._icon_cache_path(icon_key, size)
            if os.path.exists(cache_path) and os.path.getsize(cache_path) > 256:
                with open(cache_path, "rb") as file:
                    image_bytes = file.read()
            else:
                svg_bytes = self._download_fluent_icon(icon_key)
                image_bytes = self._render_svg_icon(svg_bytes, size * 2)
                if image_bytes:
                    with open(cache_path, "wb") as file:
                        file.write(image_bytes)
            if not image_bytes:
                return None
            icon = Image.open(BytesIO(image_bytes)).convert("RGBA")
            if tint:
                alpha = icon.getchannel("A")
                tinted = Image.new("RGBA", icon.size, tint)
                tinted.putalpha(alpha)
                icon = tinted
            return icon.resize((size, size))
        except Exception:
            return None

    def _icon_cache_path(self, icon_key, size):
        safe_name = re.sub(r"[^a-zA-Z0-9_.-]", "_", icon_key or "weather")
        safe_name = f"{safe_name}_{size}.png"
        return os.path.join(self.cache_dir, safe_name)

    def _load_font(self, size, style="regular"):
        candidate_map = {
            "regular": [
                "C:/Windows/Fonts/msyh.ttc",
                "C:/Windows/Fonts/segoeui.ttf",
                "C:/Windows/Fonts/simhei.ttf",
            ],
            "bold": [
                "C:/Windows/Fonts/msyhbd.ttc",
                "C:/Windows/Fonts/msyh.ttc",
                "C:/Windows/Fonts/segoeuib.ttf",
                "C:/Windows/Fonts/simhei.ttf",
            ],
            "light": [
                "C:/Windows/Fonts/msyhl.ttc",
                "C:/Windows/Fonts/msyh.ttc",
                "C:/Windows/Fonts/segoeuil.ttf",
                "C:/Windows/Fonts/simhei.ttf",
            ],
        }
        for path in candidate_map.get(style, candidate_map["regular"]):
            try:
                return ImageFont.truetype(path, size)
            except Exception:
                continue
        return ImageFont.load_default()

    def _fit_font(self, draw, text, base_font, max_width, min_size):
        current_size = getattr(base_font, "size", min_size)
        if self._text_width(draw, text, base_font) <= max_width:
            return base_font

        for size in range(current_size - 1, min_size - 1, -1):
            font = self._load_font(size)
            if self._text_width(draw, text, font) <= max_width:
                return font
        return self._load_font(min_size)

    def _text_width(self, draw, text, font):
        bbox = draw.textbbox((0, 0), str(text), font=font)
        return bbox[2] - bbox[0]

    def _draw_blurred_shadow(self, image, rect, radius, blur_radius, color, scale):
        layer = Image.new("RGBA", image.size, (0, 0, 0, 0))
        shadow_draw = ImageDraw.Draw(layer)
        shadow_draw.rounded_rectangle(
            (
                self._scaled(rect[0], scale),
                self._scaled(rect[1], scale),
                self._scaled(rect[2], scale),
                self._scaled(rect[3], scale),
            ),
            radius=self._scaled(radius, scale),
            fill=color,
        )
        layer = layer.filter(ImageFilter.GaussianBlur(self._scaled(blur_radius, scale)))
        image.alpha_composite(layer)

    def _draw_glass_panel(self, draw, rect, radius, fill, outline, scale):
        draw.rounded_rectangle(
            (
                self._scaled(rect[0], scale),
                self._scaled(rect[1], scale),
                self._scaled(rect[2], scale),
                self._scaled(rect[3], scale),
            ),
            radius=self._scaled(radius, scale),
            fill=fill,
            outline=outline,
            width=self._scaled(1, scale),
        )

    def _draw_stat_chip(self, draw, rect, title, value, title_font, value_font, scale):
        x1 = self._scaled(rect[0], scale)
        y1 = self._scaled(rect[1], scale)
        x2 = self._scaled(rect[2], scale)
        y2 = self._scaled(rect[3], scale)
        draw.rounded_rectangle((x1, y1, x2, y2), radius=self._scaled(26, scale), fill=(240, 245, 252, 240), outline=(255, 255, 255, 68), width=self._scaled(1, scale))
        draw.text((x1 + self._scaled(18, scale), y1 + self._scaled(12, scale)), title, font=title_font, fill=(116, 136, 172))
        fitted = self._fit_font(draw, value, value_font, x2 - x1 - self._scaled(28, scale), self._font_size(value_font, scale, 18))
        value_bbox = draw.textbbox((0, 0), value, font=fitted)
        available_top = y1 + self._scaled(28, scale)
        available_bottom = y2 - self._scaled(10, scale)
        value_height = value_bbox[3] - value_bbox[1]
        value_y = available_top + max((available_bottom - available_top - value_height) // 2, 0) - value_bbox[1]
        draw.text((x1 + self._scaled(18, scale), value_y), value, font=fitted, fill=(22, 39, 70))

    def _draw_icon_showcase(self, draw, image, weather, rect, accent, scale):
        x1 = self._scaled(rect[0], scale)
        y1 = self._scaled(rect[1], scale)
        x2 = self._scaled(rect[2], scale)
        y2 = self._scaled(rect[3], scale)
        draw.rounded_rectangle((x1, y1, x2, y2), radius=self._scaled(34, scale), fill=(255, 255, 255, 26), outline=(255, 255, 255, 42), width=self._scaled(1, scale))
        draw.ellipse((x1 + self._scaled(18, scale), y1 + self._scaled(18, scale), x2 - self._scaled(18, scale), y2 - self._scaled(18, scale)), fill=(255, 255, 255, 16))
        icon = self._fetch_icon_image(weather, 136 * scale, (34, 46, 70, 255))
        if icon:
            icon_x = x1 + (x2 - x1 - icon.width) // 2
            icon_y = y1 + (y2 - y1 - icon.height) // 2
            image.alpha_composite(icon, (icon_x, icon_y))

    def _draw_daylight_capsules(self, draw, weather, label_font, body_font, scale):
        sunrise = self._format_clock(weather["sunrise"])
        sunset = self._format_clock(weather["sunset"])
        self._draw_stat_chip(draw, (372, 470, 518, 542), "日出", sunrise, label_font, body_font, scale)
        self._draw_stat_chip(draw, (536, 470, 682, 542), "日落", sunset, label_font, body_font, scale)

    def _download_fluent_icon(self, icon_key):
        icon_path = os.path.join(self.cache_dir, f"{icon_key}.svg")
        if os.path.exists(icon_path) and os.path.getsize(icon_path) > 128:
            with open(icon_path, "rb") as file:
                return file.read()
        base_url = "https://raw.githubusercontent.com/microsoft/fluentui-system-icons/main/assets/{asset}/SVG/ic_fluent_{slug}_48_{variant}.svg"
        slug = icon_key
        asset_name = " ".join(word.capitalize() for word in slug.split("_"))
        headers = {"User-Agent": "Mozilla/5.0"}
        ssl_context = ssl._create_unverified_context()
        for variant in ("filled", "regular"):
            url = base_url.format(asset=urllib.parse.quote(asset_name), slug=slug, variant=variant)
            try:
                request = urllib.request.Request(url, headers=headers, method="GET")
                with urllib.request.urlopen(request, timeout=20, context=ssl_context) as response:
                    svg_bytes = response.read()
                if svg_bytes:
                    with open(icon_path, "wb") as file:
                        file.write(svg_bytes)
                    return svg_bytes
            except Exception:
                continue
        return b""

    def _render_svg_icon(self, svg_bytes, size):
        if not svg_bytes or not HAS_QTSVG:
            return b""
        try:
            renderer = QSvgRenderer(QByteArray(svg_bytes))
            if not renderer.isValid():
                return b""
            image = QImage(size, size, QImage.Format.Format_ARGB32)
            image.fill(0)
            painter = QPainter(image)
            painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
            painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, True)
            renderer.render(painter, QRectF(0, 0, size, size))
            painter.end()
            output = QByteArray()
            buffer = QBuffer(output)
            buffer.open(QIODevice.OpenModeFlag.WriteOnly)
            image.save(buffer, b"PNG")
            buffer.close()
            return bytes(output)
        except Exception:
            return b""

    def _map_fluent_icon(self, weather_item):
        code = str(weather_item.get("weather_code", "") or "")
        description = str(weather_item.get("description", "") or "")
        is_night = bool(weather_item.get("is_night"))

        if code in {"200", "386", "389", "392", "395"} or "雷" in description:
            return "weather_thunderstorm"
        if code in {"227", "230", "335", "338"}:
            return "weather_blowing_snow"
        if code in {"323", "326", "368", "371"}:
            return "weather_snow_shower_night" if is_night else "weather_snow_shower_day"
        if code in {"179", "182", "311", "314", "317", "320", "362", "365"}:
            return "weather_rain_snow"
        if code in {"350", "374", "377"}:
            return "weather_hail_night" if is_night else "weather_hail_day"
        if code in {"143", "248", "260"} or "雾" in description:
            return "weather_fog"
        if code in {"176", "263", "266", "281", "284", "293", "296", "353"}:
            return "weather_rain_showers_night" if is_night else "weather_rain_showers_day"
        if code in {"299", "302", "305", "308", "356", "359"} or "暴雨" in description or "大雨" in description:
            return "weather_rain"
        if code in {"116"}:
            return "weather_partly_cloudy_night" if is_night else "weather_partly_cloudy_day"
        if code in {"119", "122"}:
            return "weather_cloudy"
        if code in {"113"}:
            return "weather_moon" if is_night else "weather_sunny"
        if "雪" in description:
            return "weather_snow"
        if "阴" in description or "云" in description:
            return "weather_cloudy"
        if "雨" in description:
            return "weather_rain_showers_night" if is_night else "weather_rain_showers_day"
        if "霾" in description or "沙" in description:
            return "weather_haze"
        return "weather_moon" if is_night else "weather_sunny"

    def _is_night_icon(self, icon_value):
        text = str(icon_value or "").lower()
        return "night" in text or "moon" in text

    def _is_night_hour(self, raw_time):
        try:
            hour = int(str(raw_time).zfill(4)[:2])
        except Exception:
            return False
        return hour < 6 or hour >= 18

    def _summarize_weather_text(self, text):
        cleaned = re.sub(r"\s+", "", str(text or ""))
        if len(cleaned) <= 8:
            return cleaned
        return cleaned[:8]

    def _clamp_text(self, text, max_chars):
        cleaned = re.sub(r"\s+", "", str(text or ""))
        if len(cleaned) <= max_chars:
            return cleaned
        return cleaned[:max_chars]

    def _scaled(self, value, scale):
        return int(round(value * scale))

    def _font_size(self, font, scale, fallback):
        size = getattr(font, "size", 0)
        if size:
            return max(int(size / max(scale, 1)), fallback)
        return fallback

    def _format_observation_time(self, text):
        raw = str(text or "").strip()
        if not raw:
            return datetime.now().strftime("%Y-%m-%d %H:%M")
        for fmt in ("%Y-%m-%d %I:%M %p", "%Y-%m-%d %H:%M"):
            try:
                return datetime.strptime(raw, fmt).strftime("%Y-%m-%d %H:%M")
            except Exception:
                continue
        return raw

    def _format_clock(self, text):
        raw = str(text or "").strip()
        if not raw:
            return "--:--"
        for fmt in ("%I:%M %p", "%H:%M"):
            try:
                return datetime.strptime(raw, fmt).strftime("%H:%M")
            except Exception:
                continue
        return raw


register_plugin(UtilityToolsPlugin())
