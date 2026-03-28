import base64
import json
import os
import re
import ssl
import urllib.request
from datetime import datetime


class DailyNewsPlugin(LCHBotPlugin):
    def __init__(self):
        super().__init__()
        self.name = "DailyNewsPlugin"
        self.version = "1.0.0"
        self.author = "LCHBOT"
        self.description = "60秒看世界图片播报"
        self.priority = 95
        self.cache_dir = os.path.join("data", "daily_news")
        self.commands = {
            "60秒早报": "发送 60 秒早报图片",
        }
        self.json_sources = [
            "https://60s.viki.moe/v2/60s?encoding=json",
            "https://api.chinasclm.com/api/api-60s-news",
        ]
        self.image_sources = [
            "https://api.chinasclm.com/api/api-60s-news?type=img",
        ]

    def on_load(self):
        os.makedirs(self.cache_dir, exist_ok=True)
        print(f"[{self.name}] 60秒早报插件已加载")

    def on_message(self, event):
        message = self._normalize(event.get("raw_message", ""))
        if not message:
            return False

        command, date_text = self._match_command(message)
        if not command:
            return False

        self._send_news(event, date_text)
        return True

    def _normalize(self, message):
        text = re.sub(r"\[CQ:[^\]]+\]", "", message or "").strip()
        text = re.sub(r"\s+", " ", text)
        return text.strip()

    def _match_command(self, message):
        for command in self.commands.keys():
            slash_command = "/" + command
            if message == command or message == slash_command:
                return command, ""
            if message.startswith(command + " "):
                return command, message[len(command):].strip()
            if message.startswith(slash_command + " "):
                return command, message[len(slash_command):].strip()
        return "", ""

    def _send_news(self, event, date_text):
        target_date = datetime.now().strftime("%Y-%m-%d")
        cache_path = self._get_cache_path(target_date)
        image_bytes = self._read_cache(cache_path)
        caption = f"{target_date} 60秒早报"

        if image_bytes is None:
            image_bytes, meta = self._download_news_image(target_date)
            if image_bytes:
                self._write_cache(cache_path, image_bytes)
                if meta.get("date"):
                    caption = f"{meta['date']} 60秒早报"
        else:
            meta = {}

        if not image_bytes:
            fallback_text = self._download_news_text(target_date)
            if fallback_text:
                self.reply(event, fallback_text)
            else:
                self.reply(event, "今日 60 秒早报图片获取失败，请稍后再试")
            return

        image_b64 = base64.b64encode(image_bytes).decode("ascii")
        self.reply(event, f"[CQ:image,file=base64://{image_b64}]\n{caption}")

    def _get_cache_path(self, date_text):
        return os.path.join(self.cache_dir, f"{date_text}.jpg")

    def _read_cache(self, cache_path):
        try:
            if os.path.exists(cache_path) and os.path.getsize(cache_path) > 1024:
                with open(cache_path, "rb") as f:
                    return f.read()
        except Exception:
            return None
        return None

    def _write_cache(self, cache_path, image_bytes):
        try:
            with open(cache_path, "wb") as f:
                f.write(image_bytes)
        except Exception:
            pass

    def _download_news_image(self, date_text):
        headers = {
            "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/131.0 Safari/537.36",
            "Accept": "image/avif,image/webp,image/apng,image/svg+xml,image/*,*/*;q=0.8",
        }
        ssl_context = ssl._create_unverified_context()

        for source in self.json_sources:
            try:
                request_url = source
                req = urllib.request.Request(request_url, headers=headers, method="GET")
                with urllib.request.urlopen(req, timeout=20, context=ssl_context) as response:
                    body = response.read()
                    payload = json.loads(body.decode("utf-8", errors="ignore"))
                    image_url, meta = self._extract_image_url(payload)
                    if not image_url:
                        continue

                    image_req = urllib.request.Request(image_url, headers=headers, method="GET")
                    with urllib.request.urlopen(image_req, timeout=20, context=ssl_context) as image_response:
                        image_body = image_response.read()
                        content_type = image_response.headers.get("Content-Type", "").lower()
                        if image_body and ("image" in content_type or image_body.startswith(b"\xff\xd8") or image_body.startswith(b"\x89PNG")):
                            return image_body, meta
            except Exception as exc:
                print(f"[{self.name}] 获取新闻图片失败: {source} -> {exc}")

        for source in self.image_sources:
            try:
                req = urllib.request.Request(source, headers=headers, method="GET")
                with urllib.request.urlopen(req, timeout=20, context=ssl_context) as response:
                    content_type = response.headers.get("Content-Type", "").lower()
                    body = response.read()
                    if body and ("image" in content_type or body.startswith(b"\xff\xd8") or body.startswith(b"\x89PNG")):
                        return body, {"date": date_text}
            except Exception as exc:
                print(f"[{self.name}] 备用新闻图片失败: {source} -> {exc}")

        return None, {}

    def _download_news_text(self, date_text):
        headers = {
            "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/131.0 Safari/537.36",
            "Accept": "application/json,text/plain,*/*",
        }
        ssl_context = ssl._create_unverified_context()

        for source in self.json_sources:
            try:
                req = urllib.request.Request(source, headers=headers, method="GET")
                with urllib.request.urlopen(req, timeout=20, context=ssl_context) as response:
                    payload = json.loads(response.read().decode("utf-8", errors="ignore"))
                    lines, actual_date = self._extract_news_lines(payload)
                    if lines:
                        title = f"{actual_date or date_text} 60秒早报"
                        return title + "\n" + "\n".join(lines[:15])
            except Exception as exc:
                print(f"[{self.name}] 获取新闻文字失败: {source} -> {exc}")

        return ""

    def _extract_image_url(self, payload):
        if isinstance(payload, dict):
            if isinstance(payload.get("data"), dict):
                data = payload["data"]
                if data.get("image"):
                    return data.get("image"), {"date": data.get("date", "")}
            if payload.get("image"):
                return payload.get("image"), {"date": payload.get("date", "")}

        return "", {}

    def _extract_news_lines(self, payload):
        if isinstance(payload, dict):
            if isinstance(payload.get("data"), dict):
                data = payload["data"]
                news = data.get("news")
                if isinstance(news, list):
                    return [str(item) for item in news], data.get("date", "")
            news = payload.get("news")
            if isinstance(news, list):
                return [str(item) for item in news], payload.get("date", "")
            data = payload.get("data")
            if isinstance(data, list):
                return [str(item) for item in data], payload.get("date", "")

        return [], ""


register_plugin(DailyNewsPlugin())
