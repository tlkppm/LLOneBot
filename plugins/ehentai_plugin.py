import os
import re
import time
import json
import base64
import traceback
import shutil
import random
import string
import threading
from io import BytesIO
from datetime import datetime
from urllib.parse import quote, urlencode
import html as html_lib

try:
    from PIL import Image, ImageDraw, ImageFont
    HAS_PIL = True
except:
    HAS_PIL = False

try:
    import requests as req_lib
    HAS_REQUESTS = True
except:
    HAS_REQUESTS = False

try:
    import pyzipper
    HAS_PYZIPPER = True
except:
    HAS_PYZIPPER = False

# ===== E-Hentai Constants =====

EH_BASE = "https://e-hentai.org"
EH_API = "https://api.e-hentai.org/api.php"
EH_POPULAR = "https://e-hentai.org/popular"

CATEGORIES = {
    'misc': 0x1, 'doujinshi': 0x2, 'manga': 0x4,
    'artistcg': 0x8, 'gamecg': 0x10, 'imageset': 0x20,
    'cosplay': 0x40, 'asianporn': 0x80, 'non-h': 0x100,
    'western': 0x200,
}
ALL_CAT = 0x3ff

CAT_NAMES_EN = {
    'doujinshi': 'Doujinshi', 'manga': 'Manga', 'artistcg': 'Artist CG',
    'gamecg': 'Game CG', 'imageset': 'Image Set', 'cosplay': 'Cosplay',
    'asianporn': 'Asian Porn', 'non-h': 'Non-H', 'western': 'Western', 'misc': 'Misc',
}
CAT_NAMES_ZH = {
    'doujinshi': '同人志', 'manga': '漫画', 'artistcg': '画集',
    'gamecg': '游戏CG', 'imageset': '图集', 'cosplay': 'Cosplay',
    'asianporn': '亚洲', 'non-h': '全年龄', 'western': '西方', 'misc': '杂项',
}
CAT_ALIASES = {}
for _k, _v in CAT_NAMES_ZH.items():
    CAT_ALIASES[_v] = _k
    CAT_ALIASES[_k] = _k
for _alias, _cat in [
    ('同人', 'doujinshi'), ('dj', 'doujinshi'), ('mg', 'manga'),
    ('cg', 'artistcg'), ('画集', 'artistcg'), ('游戏', 'gamecg'),
    ('img', 'imageset'), ('图集', 'imageset'), ('cos', 'cosplay'),
    ('asian', 'asianporn'), ('亚洲', 'asianporn'), ('nonh', 'non-h'),
    ('全年龄', 'non-h'), ('西方', 'western'), ('杂项', 'misc'),
]:
    CAT_ALIASES[_alias] = _cat

LANG_ALIASES = {}
for _code, _tag, _zh in [
    ('en', 'english', '英语'), ('zh', 'chinese', '中文'),
    ('ja', 'japanese', '日语'), ('ko', 'korean', '韩语'),
    ('es', 'spanish', '西班牙语'), ('ru', 'russian', '俄语'),
    ('fr', 'french', '法语'), ('pt', 'portuguese', '葡萄牙语'),
    ('th', 'thai', '泰语'), ('de', 'german', '德语'),
    ('it', 'italian', '意大利语'), ('vi', 'vietnamese', '越南语'),
]:
    LANG_ALIASES[_code] = _tag
    LANG_ALIASES[_tag] = _tag
    LANG_ALIASES[_zh] = _tag
LANG_ALIASES['英文'] = 'english'
LANG_ALIASES['汉化'] = 'chinese'
LANG_ALIASES['中国语'] = 'chinese'
LANG_ALIASES['日文'] = 'japanese'
LANG_ALIASES['韩文'] = 'korean'

LANG_DISPLAY = {
    'english': '英语', 'chinese': '中文', 'japanese': '日语',
    'korean': '韩语', 'spanish': '西班牙语', 'russian': '俄语',
    'french': '法语', 'portuguese': '葡萄牙语', 'thai': '泰语',
    'german': '德语', 'italian': '意大利语', 'vietnamese': '越南语',
}

CAT_COLORS = {
    'doujinshi': (244, 67, 54), 'manga': (255, 152, 0),
    'artistcg': (251, 192, 45), 'gamecg': (76, 175, 80),
    'imageset': (63, 81, 181), 'cosplay': (156, 39, 176),
    'asianporn': (149, 117, 205), 'non-h': (33, 150, 243),
    'western': (139, 195, 74), 'misc': (240, 98, 146),
}

def cat_value_to_key(val):
    for k, v in CATEGORIES.items():
        if v == val:
            return k
    return 'misc'

USER_AGENT = ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
              "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36")


class EhentaiPlugin(LCHBotPlugin):
    def __init__(self):
        super().__init__()
        self.name = "EhentaiPlugin"
        self.version = "1.0.0"
        self.author = "LCHBOT"
        self.description = "E-Hentai - 搜索与下载"
        self.priority = 89

        self.download_dir = "data/ehentai"
        self._last_search = {}
        self._cooldown = {}
        self._downloading = set()
        self._dl_lock = threading.Lock()
        self.COOLDOWN_SEC = 5
        self.session = None

    def on_load(self):
        os.makedirs(self.download_dir, exist_ok=True)
        if HAS_REQUESTS:
            self.session = req_lib.Session()
            self.session.headers.update({
                'User-Agent': USER_AGENT,
                'Accept': 'text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8',
                'Accept-Language': 'en-US,en;q=0.5',
            })
            print(f"[{self.name}] E-Hentai plugin loaded")
        else:
            print(f"[{self.name}] WARNING: requests not available")

    def on_unload(self):
        if self.session:
            self.session.close()
        print(f"[{self.name}] E-Hentai plugin unloaded")

    def _check_cooldown(self, user_id):
        now = time.time()
        last = self._cooldown.get(user_id, 0)
        if now - last < self.COOLDOWN_SEC:
            return False
        self._cooldown[user_id] = now
        return True

    # ===== Comments =====

    def _handle_comments(self, group_id, user_id, arg):
        if not arg:
            self._send_text(group_id, user_id, "用法: /eh 评论 <GID>/<TOKEN> [页码]\n或: /eh 评论 <序号> [页码]")
            return True

        parts = arg.split()
        id_part = parts[0]
        page = 1
        if len(parts) > 1:
            try:
                page = max(1, int(parts[1]))
            except:
                page = 1

        gid, token = self._parse_gallery_id(id_part)
        if not gid:
            idx = re.match(r'^(\d+)$', id_part)
            if idx and user_id in self._last_search:
                i = int(idx.group(1)) - 1
                results = self._last_search[user_id]
                if 0 <= i < len(results):
                    gid = results[i].get('gid')
                    token = results[i].get('token')
            if not gid:
                self._send_text(group_id, user_id, "无法解析画廊ID")
                return True

        if not self._check_cooldown(user_id):
            self._send_text(group_id, user_id, "操作太频繁，请稍后再试")
            return True

        try:
            comments = self._fetch_comments_all(gid, token, hard_limit=60)
            if not comments:
                self._send_text(group_id, user_id, "未获取到评论(可能需要登录或页面结构变化)")
                return True

            page_size = 3
            total_pages = (len(comments) + page_size - 1) // page_size
            if page > total_pages:
                page = total_pages
            start = (page - 1) * page_size
            chunk = comments[start:start + page_size]

            if HAS_PIL:
                img = self._render_comments(gid, token, chunk, page, total_pages)
                if img:
                    self._send_photo(group_id, img)
                    self._send_text(group_id, user_id, f"评论页 {page}/{total_pages}  下一页: /eh 评论 {gid}/{token} {page + 1 if page < total_pages else total_pages}")
                    return True

            lines = [f"GID: {gid}/{token}", f"评论页 {page}/{total_pages}"]
            for i, c in enumerate(chunk, start=start + 1):
                u = c.get('user', '')
                t = c.get('text', '')
                head = f"{i}. {u}:" if u else f"{i}."
                lines.append(head)
                lines.append(t[:800])
                lines.append("")
            self._send_text(group_id, user_id, "\n".join(lines).strip())
        except Exception as e:
            print(f"[EH] Comments error: {traceback.format_exc()}")
            self._send_text(group_id, user_id, f"获取评论失败: {str(e)[:80]}")

        return True

    def _render_comments(self, gid, token, comments, page, total_pages):
        try:
            w = 1100
            pad = 18
            header_h = 72
            font_title = self._get_font(22)
            font_info = self._get_font(14)
            font_text = self._get_font(15)
            font_small = self._get_font(13)

            tmp_img = Image.new('RGB', (w, 10), (25, 25, 38))
            tmp_draw = ImageDraw.Draw(tmp_img)

            lines = []
            for idx, c in enumerate(comments, start=1):
                u = c.get('user', '')
                t = (c.get('text', '') or '').strip()
                if not t:
                    continue
                head = f"{idx}. {u}:" if u else f"{idx}."
                lines.append(('head', head))
                for part in t.split('\n'):
                    part = part.strip()
                    if not part:
                        continue
                    lines.append(('text', part))
                lines.append(('gap', ''))

            max_w = w - pad * 2
            layout = []
            total_h = header_h + pad
            for kind, text in lines:
                if kind == 'gap':
                    total_h += 10
                    continue
                f = font_info if kind == 'head' else font_text
                color = (200, 180, 100) if kind == 'head' else (210, 210, 230)
                wrapped = self._wrap_text(tmp_draw, text, f, max_w, max_lines=12)
                lh = 22 if kind == 'head' else 24
                for wl in wrapped:
                    layout.append((wl, f, color, lh))
                    total_h += lh

            total_h += pad + 10
            img = Image.new('RGB', (w, total_h), (25, 25, 38))
            draw = ImageDraw.Draw(img)
            draw.rectangle((0, 0, w, header_h), fill=(40, 40, 58))
            draw.text((pad, 16), "E-Hentai 评论", fill=(100, 200, 255), font=font_title)
            draw.text((pad, 44), f"{gid}/{token[:10]}..   页 {page}/{total_pages}", fill=(160, 160, 180), font=font_small)

            y = header_h + 12
            for wl, f, color, lh in layout:
                draw.text((pad, y), wl, fill=color, font=f)
                y += lh

            buf = BytesIO()
            img.save(buf, format='JPEG', quality=90)
            return buf.getvalue()
        except Exception:
            return None

    @staticmethod
    def _gen_password(length=8):
        chars = string.ascii_letters + string.digits
        return ''.join(random.choice(chars) for _ in range(length))

    def _send_photo(self, group_id, img_bytes):
        b64 = base64.b64encode(img_bytes).decode()
        self.send_group_msg(group_id, f"[CQ:image,file=base64://{b64},subType=0]")

    def _send_text(self, group_id, user_id, text):
        self.send_group_msg(group_id, f"[CQ:at,qq={user_id}] {text}")

    def _get_font(self, size):
        try:
            return ImageFont.truetype("msyh.ttc", size)
        except:
            try:
                return ImageFont.truetype("simhei.ttf", size)
            except:
                return ImageFont.load_default()

    def _wrap_text(self, draw, text, font, max_width, max_lines=None):
        if not text:
            return []
        text = str(text)
        text = re.sub(r"\s+", " ", text).strip()
        if not text:
            return []

        lines = []
        cur = ""
        for ch in text:
            test = cur + ch
            try:
                w = draw.textlength(test, font=font)
            except:
                w = draw.textsize(test, font=font)[0]
            if w <= max_width or not cur:
                cur = test
                continue
            lines.append(cur)
            cur = ch
            if max_lines and len(lines) >= max_lines:
                break

        if (not max_lines) or len(lines) < max_lines:
            if cur:
                lines.append(cur)
        else:
            if lines:
                last = lines[-1]
                ell = "…"
                while last:
                    try:
                        w = draw.textlength(last + ell, font=font)
                    except:
                        w = draw.textsize(last + ell, font=font)[0]
                    if w <= max_width:
                        break
                    last = last[:-1]
                lines[-1] = (last + ell) if last else ell

        return lines

    def _draw_text_lines(self, draw, x, y, lines, font, fill, line_h):
        for line in lines:
            draw.text((x, y), line, fill=fill, font=font)
            y += line_h
        return y

    def _download_thumb(self, url, timeout=15):
        if not url:
            return None
        try:
            resp = self._get(url, timeout=timeout)
            if resp.status_code != 200 or not resp.content or len(resp.content) < 200:
                return None
            return Image.open(BytesIO(resp.content)).convert('RGB')
        except:
            return None

    def _html_to_text(self, html):
        if not html:
            return ""
        s = str(html)
        s = re.sub(r"(?i)<br\s*/?>", "\n", s)
        s = re.sub(r"(?is)<[^>]+>", "", s)
        s = html_lib.unescape(s)
        s = s.replace("\r", "")
        s = re.sub(r"\n{3,}", "\n\n", s)
        s = re.sub(r"[\t ]+", " ", s)
        return s.strip()

    def _fetch_comments(self, gid, token, limit=3):
        try:
            url = f"{EH_BASE}/g/{gid}/{token}/?hc=1"
            resp = self._get(url, timeout=20)
            if resp.status_code != 200:
                return []
            html = resp.text

            comments = []

            for m in re.finditer(r"(?is)<div\s+class=\"c1\"[^>]*>.*?</div>\s*</div>", html):
                block = m.group(0)
                um = re.search(r"(?is)<a[^>]*>([^<]{1,40})</a>", block)
                user = self._html_to_text(um.group(1)) if um else ""
                tm = re.search(r"(?is)<div\s+class=\"c6\"[^>]*>(.*?)</div>", block)
                text = self._html_to_text(tm.group(1)) if tm else ""
                if text:
                    comments.append({'user': user, 'text': text})
                if len(comments) >= limit:
                    break

            if comments:
                return comments

            for m in re.finditer(r"(?is)<div\s+id=\"cdiv\"[^>]*>.*?</div>", html):
                block = m.group(0)
                um = re.search(r"(?is)<a[^>]*>([^<]{1,40})</a>", block)
                user = self._html_to_text(um.group(1)) if um else ""
                text = self._html_to_text(block)
                if text:
                    comments.append({'user': user, 'text': text})
                if len(comments) >= limit:
                    break

            return comments
        except Exception:
            return []

    def _fetch_comments_all(self, gid, token, hard_limit=60):
        comments = self._fetch_comments(gid, token, limit=hard_limit)
        if not comments:
            return []
        return comments[:hard_limit]

    # ===== HTTP helpers =====

    def _get(self, url, timeout=15):
        return self.session.get(url, timeout=timeout)

    def _api_gdata(self, gid_token_list):
        payload = {
            "method": "gdata",
            "gidlist": [[g, t] for g, t in gid_token_list],
            "namespace": 1,
        }
        resp = self.session.post(EH_API, json=payload, timeout=15)
        return resp.json()

    # ===== URL / ID parsing =====

    @staticmethod
    def _parse_gallery_id(text):
        text = text.strip()
        m = re.search(r'e[-x]hentai\.org/g/(\d+)/([0-9a-f]+)', text)
        if m:
            return int(m.group(1)), m.group(2)
        m = re.match(r'^(\d+)[/\s]+([0-9a-f]+)$', text)
        if m:
            return int(m.group(1)), m.group(2)
        return None, None

    # ===== Search HTML parsing =====

    def _search_galleries(self, keyword, language=None, category_key=None, page=0):
        params = {}
        search_parts = []

        if keyword:
            search_parts.append(keyword)
        if language:
            search_parts.append(f"language:{language}")

        if search_parts:
            params['f_search'] = ' '.join(search_parts)

        if category_key:
            cat_val = CATEGORIES.get(category_key, 0)
            params['f_cats'] = ALL_CAT ^ cat_val
        elif not any(k in (keyword or '') for k in ['language:', 'l:']):
            pass

        if page > 0:
            params['page'] = page

        url = EH_BASE + "/?" + urlencode(params) if params else EH_BASE
        resp = self._get(url)
        return self._parse_search_html(resp.text)

    def _parse_search_html(self, html):
        results = []
        pattern = re.compile(
            r'e-hentai\.org/g/(\d+)/([0-9a-f]+)/',
        )
        seen = set()
        for m in pattern.finditer(html):
            gid = int(m.group(1))
            token = m.group(2)
            if gid not in seen:
                seen.add(gid)
                results.append((gid, token))
        return results[:25]

    # ===== Gallery detail page parsing =====

    def _get_gallery_images_page(self, gid, token, page=0):
        url = f"{EH_BASE}/g/{gid}/{token}/?p={page}"
        resp = self._get(url)
        html = resp.text

        image_pages = []
        pattern = re.compile(r'href="(https://e-hentai\.org/s/[0-9a-f]+/\d+-\d+)"')
        for m in pattern.finditer(html):
            image_pages.append(m.group(1))

        total_pages = 0
        m = re.search(r'class="ptt".*?</table>', html, re.DOTALL)
        if m:
            page_nums = re.findall(r'>(\d+)<', m.group(0))
            if page_nums:
                total_pages = max(int(p) for p in page_nums)

        return image_pages, total_pages

    def _get_image_url(self, page_url):
        resp = self._get(page_url)
        m = re.search(r'<img[^>]*id="img"[^>]*src="([^"]+)"', resp.text)
        if m:
            return m.group(1)
        m = re.search(r'<img[^>]*src="([^"]+)"[^>]*style="', resp.text)
        if m:
            return m.group(1)
        return None

    # ===== Command handler =====

    def on_message(self, event):
        if event.get("message_type") != "group":
            return False

        raw = event.get("raw_message", "").strip()
        group_id = event.get("group_id", 0)
        user_id = event.get("user_id", 0)
        self_id = event.get("self_id", 0)

        at_pattern = r'\[CQ:at,qq=' + str(self_id) + r'[^\]]*\]'
        if not re.search(at_pattern, raw):
            return False

        clean = re.sub(r'\[CQ:[^\]]+\]', '', raw).strip()

        if not clean.startswith("/eh"):
            return False

        if not HAS_REQUESTS or not self.session:
            self._send_text(group_id, user_id, "E-Hentai 模块未就绪(缺少 requests)")
            return True

        cmd_body = clean[3:].strip()
        parts = cmd_body.split(None, 1)
        if not parts:
            return self._handle_help(group_id, user_id)

        subcmd = parts[0].lower()
        arg = parts[1] if len(parts) > 1 else ""

        if subcmd in ("搜索", "搜", "search", "s"):
            return self._handle_search(group_id, user_id, arg)
        elif subcmd in ("详情", "detail", "d"):
            return self._handle_detail(group_id, user_id, arg)
        elif subcmd in ("评论", "comment", "cmt"):
            return self._handle_comments(group_id, user_id, arg)
        elif subcmd in ("看", "read", "r"):
            return self._handle_read(group_id, user_id, arg)
        elif subcmd in ("下载", "download", "dl"):
            return self._handle_download(group_id, user_id, arg)
        elif subcmd in ("热门", "hot"):
            return self._handle_hot(group_id, user_id)
        elif subcmd in ("帮助", "help"):
            return self._handle_help(group_id, user_id)
        else:
            return self._handle_help(group_id, user_id)

    # ===== Search =====

    def _handle_search(self, group_id, user_id, arg):
        if not arg:
            self._send_text(group_id, user_id,
                "用法: /eh 搜索 <关键词> [-l 语言] [-c 分类]\n"
                "语言: 中文/英语/日语/韩语 等\n"
                "分类: 同人志/漫画/画集/游戏CG/图集/Cosplay/全年龄/西方 等")
            return True

        if not self._check_cooldown(user_id):
            self._send_text(group_id, user_id, "操作太频繁，请稍后再试")
            return True

        language = None
        category_key = None
        keyword_parts = []

        tokens = arg.split()
        i = 0
        while i < len(tokens):
            t = tokens[i]
            if t in ('-l', '-lang', '--lang', '-语言'):
                if i + 1 < len(tokens):
                    lang_input = tokens[i + 1].lower()
                    language = LANG_ALIASES.get(lang_input)
                    if not language:
                        self._send_text(group_id, user_id, f"未知语言: {tokens[i+1]}")
                        return True
                    i += 2
                    continue
            elif t in ('-c', '-cat', '--cat', '-分类'):
                if i + 1 < len(tokens):
                    cat_input = tokens[i + 1].lower()
                    category_key = CAT_ALIASES.get(cat_input)
                    if not category_key:
                        self._send_text(group_id, user_id, f"未知分类: {tokens[i+1]}")
                        return True
                    i += 2
                    continue
            keyword_parts.append(t)
            i += 1

        keyword = ' '.join(keyword_parts)

        filter_desc = keyword or "(全部)"
        if language:
            filter_desc += f" | 语言:{LANG_DISPLAY.get(language, language)}"
        if category_key:
            filter_desc += f" | 分类:{CAT_NAMES_ZH.get(category_key, category_key)}"

        self._send_text(group_id, user_id, f"正在搜索: {filter_desc}")

        try:
            gid_tokens = self._search_galleries(keyword, language, category_key)
            if not gid_tokens:
                self._send_text(group_id, user_id, "未找到结果")
                return True

            batch = gid_tokens[:10]
            api_data = self._api_gdata(batch)
            galleries = api_data.get('gmetadata', [])

            if not galleries:
                self._send_text(group_id, user_id, "获取数据失败")
                return True

            self._last_search[user_id] = galleries

            img = self._render_search(galleries, filter_desc)
            if img:
                self._send_photo(group_id, img)
            else:
                lines = []
                for i, g in enumerate(galleries):
                    cat = g.get('category', 'Unknown')
                    title = g.get('title', '')[:40]
                    pages = g.get('filecount', '?')
                    lines.append(f"{i+1}. [{cat}] {title} ({pages}P)")
                self._send_text(group_id, user_id, '\n'.join(lines))

        except Exception as e:
            print(f"[EH] Search error: {traceback.format_exc()}")
            self._send_text(group_id, user_id, f"搜索失败: {str(e)[:80]}")

        return True

    def _render_search(self, galleries, filter_desc=""):
        if not HAS_PIL:
            return None
        try:
            font_title = self._get_font(22)
            font_header = self._get_font(18)
            font_info = self._get_font(16)
            font_small = self._get_font(13)

            w = 1100
            pad = 18
            header_h = 70
            thumb_w = 96
            thumb_h = 128
            gap = 14
            badge_h = 22
            line_h_title = 24
            line_h_info = 20

            tmp_img = Image.new('RGB', (w, 10), (25, 25, 38))
            tmp_draw = ImageDraw.Draw(tmp_img)

            prepared = []
            total_h = header_h + pad
            for idx, g in enumerate(galleries):
                gid = g.get('gid', 0)
                token = g.get('token', '')
                title = g.get('title', '')
                title_jpn = g.get('title_jpn', '')
                category = g.get('category', 'Unknown').lower().replace(' ', '')
                pages = g.get('filecount', '?')
                rating = g.get('rating', '0')
                uploader = g.get('uploader', '')
                thumb_url = g.get('thumb', '')

                display_title = title_jpn if title_jpn else title
                cat_key = category
                for k, v in CAT_NAMES_EN.items():
                    if v.lower().replace(' ', '') == category:
                        cat_key = k
                        break
                cat_color = CAT_COLORS.get(cat_key, (128, 128, 128))
                cat_zh = CAT_NAMES_ZH.get(cat_key, category)

                lang_tags = [t for t in g.get('tags', []) if t.startswith('language:') and t != 'language:translated']
                lang_str = ''
                if lang_tags:
                    lang_str = ', '.join(LANG_DISPLAY.get(t.split(':')[1], t.split(':')[1]) for t in lang_tags[:3])

                title_x = pad + thumb_w + gap
                title_max_w = w - pad - title_x
                title_lines = self._wrap_text(tmp_draw, display_title, font_info, title_max_w, max_lines=2)

                info_line = f"GID:{gid}/{token[:6]}..  |  {pages}P  |  ★{rating}  |  {uploader}"
                info_lines = self._wrap_text(tmp_draw, info_line, font_small, title_max_w, max_lines=2)

                lang_lines = []
                if lang_str:
                    lang_lines = self._wrap_text(tmp_draw, f"语言: {lang_str}", font_small, title_max_w, max_lines=1)

                text_block_h = badge_h + 6 + max(1, len(title_lines)) * line_h_title + len(info_lines) * line_h_info + len(lang_lines) * line_h_info + 8
                item_h = max(thumb_h + 12, text_block_h + 10)

                prepared.append({
                    'idx': idx,
                    'gid': gid,
                    'token': token,
                    'cat_key': cat_key,
                    'cat_color': cat_color,
                    'cat_zh': cat_zh,
                    'title_lines': title_lines,
                    'info_lines': info_lines,
                    'lang_lines': lang_lines,
                    'thumb_url': thumb_url,
                    'item_h': item_h,
                })
                total_h += item_h

            total_h += pad

            img = Image.new('RGB', (w, total_h), (25, 25, 38))
            draw = ImageDraw.Draw(img)

            draw.rectangle((0, 0, w, header_h), fill=(40, 40, 58))
            draw.text((pad, 16), "E-Hentai 搜索结果", fill=(100, 200, 255), font=font_title)
            if filter_desc:
                fd_lines = self._wrap_text(draw, filter_desc, font_small, w - pad * 2, max_lines=2)
                self._draw_text_lines(draw, pad, 42, fd_lines, font_small, (160, 160, 180), 18)

            y = header_h
            for item in prepared:
                idx = item['idx']
                item_h = item['item_h']
                if idx % 2 == 0:
                    draw.rectangle((0, y, w, y + item_h), fill=(30, 30, 45))
                else:
                    draw.rectangle((0, y, w, y + item_h), fill=(28, 28, 42))

                iy = y + 10
                draw.text((pad, iy), f"{idx + 1}.", fill=(120, 120, 140), font=font_info)

                thumb_x = pad
                thumb_y = y + 34
                try:
                    thumb_img = self._download_thumb(item['thumb_url'])
                    if thumb_img:
                        thumb_img.thumbnail((thumb_w, thumb_h), Image.LANCZOS)
                        tw, th = thumb_img.size
                        tx = thumb_x
                        ty = thumb_y
                        img.paste(thumb_img, (tx, ty))
                        draw.rectangle((tx - 1, ty - 1, tx + tw + 1, ty + th + 1), outline=(70, 70, 100), width=2)
                    else:
                        draw.rectangle((thumb_x, thumb_y, thumb_x + thumb_w, thumb_y + thumb_h), outline=(70, 70, 100), width=2)
                except:
                    draw.rectangle((thumb_x, thumb_y, thumb_x + thumb_w, thumb_y + thumb_h), outline=(70, 70, 100), width=2)

                badge_x = pad + thumb_w + gap
                badge_y = y + 10
                cat_zh = item['cat_zh']
                cat_color = item['cat_color']
                badge_w = 10 + len(cat_zh) * 14
                draw.rounded_rectangle((badge_x, badge_y, badge_x + badge_w, badge_y + badge_h), radius=4, fill=cat_color)
                draw.text((badge_x + 6, badge_y + 2), cat_zh, fill=(255, 255, 255), font=font_small)

                meta_x = badge_x + badge_w + 10
                draw.text((meta_x, badge_y + 1), f"{item['gid']}/{str(item['token'])[:8]}..", fill=(130, 190, 255), font=font_small)

                text_x = badge_x
                ty = badge_y + badge_h + 8
                ty = self._draw_text_lines(draw, text_x, ty, item['title_lines'], font_info, (225, 225, 245), line_h_title)
                ty = self._draw_text_lines(draw, text_x, ty + 2, item['info_lines'], font_small, (150, 150, 170), line_h_info)
                if item['lang_lines']:
                    self._draw_text_lines(draw, text_x, ty + 2, item['lang_lines'], font_small, (120, 180, 120), line_h_info)

                y += item_h

            buf = BytesIO()
            img.save(buf, format='JPEG', quality=90)
            return buf.getvalue()
        except Exception as e:
            print(f"[EH] Render search error: {e}")
            return None

    # ===== Detail =====

    def _handle_detail(self, group_id, user_id, arg):
        if not arg:
            self._send_text(group_id, user_id, "用法: /eh 详情 <GID>/<TOKEN>\n或: /eh 详情 <URL>")
            return True

        gid, token = self._parse_gallery_id(arg)
        if not gid:
            idx = re.match(r'^(\d+)$', arg.strip())
            if idx and user_id in self._last_search:
                i = int(idx.group(1)) - 1
                results = self._last_search[user_id]
                if 0 <= i < len(results):
                    gid = results[i].get('gid')
                    token = results[i].get('token')
            if not gid:
                self._send_text(group_id, user_id, "无法解析画廊ID\n格式: GID/TOKEN 或 完整URL")
                return True

        if not self._check_cooldown(user_id):
            self._send_text(group_id, user_id, "操作太频繁，请稍后再试")
            return True

        self._send_text(group_id, user_id, f"正在获取 {gid} 详情...")

        try:
            api_data = self._api_gdata([(gid, token)])
            galleries = api_data.get('gmetadata', [])
            if not galleries:
                self._send_text(group_id, user_id, "获取详情失败")
                return True

            g = galleries[0]

            comments = self._fetch_comments(gid, token, limit=5)

            cover_img = None
            thumb_url = g.get('thumb', '')
            if thumb_url:
                try:
                    resp = self._get(thumb_url)
                    if resp.status_code == 200 and len(resp.content) > 100:
                        cover_img = Image.open(BytesIO(resp.content)).convert('RGB')
                except Exception as e:
                    print(f"[EH] Cover download: {e}")

            rendered = self._render_detail(g, cover_img, comments)
            if rendered:
                self._send_photo(group_id, rendered)
            else:
                info = self._format_detail_text(g, comments)
                self._send_text(group_id, user_id, info)

        except Exception as e:
            print(f"[EH] Detail error: {traceback.format_exc()}")
            self._send_text(group_id, user_id, f"获取详情失败: {str(e)[:80]}")

        return True

    def _format_detail_text(self, g, comments=None):
        gid = g.get('gid', 0)
        token = g.get('token', '')
        title = g.get('title', '')
        category = g.get('category', '')
        pages = g.get('filecount', '?')
        rating = g.get('rating', '?')
        uploader = g.get('uploader', '?')
        posted = g.get('posted', '')
        tags = g.get('tags', [])

        lang_tags = [t.split(':')[1] for t in tags if t.startswith('language:') and t != 'language:translated']
        lang_str = ', '.join(LANG_DISPLAY.get(l, l) for l in lang_tags) if lang_tags else '未知'

        base = (f"GID: {gid}/{token}\n"
                f"标题: {title[:50]}\n"
                f"分类: {category} | 语言: {lang_str}\n"
                f"页数: {pages} | 评分: {rating}\n"
                f"上传: {uploader}\n"
                f"标签: {', '.join(tags[:8])}")

        if comments:
            lines = [base, "\n评论:"]
            for i, c in enumerate(comments[:3]):
                u = c.get('user', '')
                t = c.get('text', '')
                if not t:
                    continue
                head = f"{i+1}. {u}:" if u else f"{i+1}."
                lines.append(head)
                lines.append(t[:400])
            return "\n".join(lines)

        return base

    def _render_detail(self, g, cover_img=None, comments=None):
        if not HAS_PIL:
            return None
        try:
            font_id = self._get_font(26)
            font_title = self._get_font(20)
            font_info = self._get_font(16)
            font_tag = self._get_font(13)

            gid = g.get('gid', 0)
            token = g.get('token', '')
            title = g.get('title', '')
            title_jpn = g.get('title_jpn', '')
            category = g.get('category', 'Unknown').lower().replace(' ', '')
            pages = g.get('filecount', '?')
            rating = g.get('rating', '?')
            uploader = g.get('uploader', '?')
            posted = g.get('posted', '')
            tags = g.get('tags', [])
            filesize = int(g.get('filesize', 0))

            cat_key = category
            for k, v in CAT_NAMES_EN.items():
                if v.lower().replace(' ', '') == category:
                    cat_key = k
                    break

            lang_tags = [t.split(':')[1] for t in tags if t.startswith('language:') and t != 'language:translated']
            lang_str = ', '.join(LANG_DISPLAY.get(l, l) for l in lang_tags) if lang_tags else '未知'

            size_mb = filesize / (1024 * 1024) if filesize else 0

            w = 1200
            pad = 18
            title_block_h = 96
            cover_w, cover_h = 0, 0
            cover_resized = None
            if cover_img:
                cw, ch = cover_img.size
                target_h = 520
                scale = target_h / ch
                cover_w = int(cw * scale)
                cover_h = target_h
                if cover_w > 420:
                    cover_w = 420
                    cover_resized = cover_img.copy()
                    cover_resized.thumbnail((cover_w, cover_h), Image.LANCZOS)
                    cover_w, cover_h = cover_resized.size
                else:
                    cover_resized = cover_img.resize((cover_w, cover_h), Image.LANCZOS)

            info_x = pad + (cover_w + 18 if cover_resized else 0)
            info_w = w - info_x - pad

            tag_groups = {}
            for t in tags:
                if ':' in t:
                    ns, val = t.split(':', 1)
                    tag_groups.setdefault(ns, []).append(val)
                else:
                    tag_groups.setdefault('misc', []).append(t)

            tmp_img = Image.new('RGB', (w, 10), (25, 25, 38))
            tmp_draw = ImageDraw.Draw(tmp_img)

            display_title = title_jpn if title_jpn else title
            title_lines = self._wrap_text(tmp_draw, display_title, font_title, w - pad * 2, max_lines=2)

            tag_lines = []
            ns_display = {
                'female': '♀', 'male': '♂', 'parody': '原作',
                'character': '角色', 'group': '社团', 'artist': '作者',
                'language': '语言', 'misc': '其他', 'reclass': '重分类',
            }
            for ns, vals in list(tag_groups.items())[:10]:
                ns_name = ns_display.get(ns, ns)
                tag_text = f"{ns_name}: {', '.join(vals[:12])}"
                if len(vals) > 12:
                    tag_text += f" (+{len(vals)-12})"
                tag_lines.extend(self._wrap_text(tmp_draw, tag_text, font_tag, info_w, max_lines=2))

            comment_lines = []
            comment_max_w = w - pad * 2
            if comments:
                comment_lines.append("评论:")
                for i, c in enumerate(comments[:5]):
                    u = c.get('user', '')
                    t = c.get('text', '')
                    if not t:
                        continue
                    prefix = f"{i+1}. {u}:" if u else f"{i+1}."
                    comment_lines.extend(self._wrap_text(tmp_draw, prefix, font_tag, comment_max_w, max_lines=2))
                    for part in t.split('\n'):
                        part = part.strip()
                        if not part:
                            continue
                        comment_lines.extend(self._wrap_text(tmp_draw, part, font_tag, comment_max_w, max_lines=6))

            base_lines = 0
            base_lines += 1
            base_lines += 1
            base_lines += 1
            base_lines += 1 if posted else 0

            info_h = 0
            info_h += len(title_lines) * 28
            info_h += base_lines * 30
            info_h += len(tag_lines) * 22
            info_h += 70

            main_h = max(cover_h + 24, info_h)
            comment_h = 0
            if comment_lines:
                comment_h = 18 + len(comment_lines) * 22

            total_h = title_block_h + main_h + comment_h + pad
            if total_h < 520:
                total_h = 520

            img = Image.new('RGB', (w, total_h), (25, 25, 38))
            draw = ImageDraw.Draw(img)

            draw.rectangle((0, 0, w, title_block_h), fill=(40, 40, 58))

            cat_color = CAT_COLORS.get(cat_key, (128, 128, 128))
            cat_zh = CAT_NAMES_ZH.get(cat_key, category)
            cat_w = len(cat_zh) * 14 + 14
            draw.rounded_rectangle((pad, 16, pad + cat_w, 38), radius=5, fill=cat_color)
            draw.text((pad + 7, 18), cat_zh, fill=(255, 255, 255), font=font_tag)

            id_text = f"{gid}/{token[:10]}.."
            draw.text((pad + cat_w + 10, 16), id_text, fill=(100, 200, 255), font=font_info)

            tx = pad
            ty = 48
            self._draw_text_lines(draw, tx, ty, title_lines, font_title, (255, 220, 100), 28)

            content_top = title_block_h + 14
            if cover_resized:
                cy = content_top
                img.paste(cover_resized, (pad, cy))
                draw.rectangle((pad - 1, cy - 1, pad + cover_w + 1, cy + cover_h + 1), outline=(80, 80, 120), width=2)

            text_x = info_x
            y = content_top
            draw.text((text_x, y), f"上传者: {uploader}", fill=(210, 210, 230), font=font_info)
            y += 30
            draw.text((text_x, y), f"页数: {pages}P  |  大小: {size_mb:.1f}MB", fill=(190, 190, 210), font=font_info)
            y += 30
            draw.text((text_x, y), f"评分: ★{rating}  |  语言: {lang_str}", fill=(190, 190, 210), font=font_info)
            y += 30
            if posted:
                try:
                    ts = int(posted)
                    dt = datetime.fromtimestamp(ts)
                    draw.text((text_x, y), f"发布: {dt.strftime('%Y-%m-%d %H:%M')}", fill=(160, 160, 180), font=font_info)
                except:
                    draw.text((text_x, y), f"发布: {posted}", fill=(160, 160, 180), font=font_info)
                y += 30

            y += 10
            for line in tag_lines[:18]:
                color = (140, 200, 255) if (line.startswith('♀') or line.startswith('♂')) else (170, 170, 190)
                draw.text((text_x, y), line, fill=color, font=font_tag)
                y += 22

            if comment_lines:
                cy = content_top + main_h
                draw.line((pad, cy, w - pad, cy), fill=(60, 60, 90), width=2)
                cy += 12
                for line in comment_lines[:60]:
                    fill = (200, 180, 100) if line == "评论:" else (190, 190, 210)
                    draw.text((pad, cy), line, fill=fill, font=font_tag)
                    cy += 22

            y += 10
            draw.text((text_x, y), "阅读: /eh 看 GID/TOKEN", fill=(110, 170, 110), font=font_tag)
            draw.text((text_x, y + 24), "下载: /eh 下载 GID/TOKEN", fill=(110, 170, 110), font=font_tag)

            buf = BytesIO()
            img.save(buf, format='JPEG', quality=90)
            return buf.getvalue()
        except Exception as e:
            print(f"[EH] Render detail error: {e}")
            return None

    # ===== Read =====

    def _handle_read(self, group_id, user_id, arg):
        if not arg:
            self._send_text(group_id, user_id,
                "用法: /eh 看 <GID>/<TOKEN> [起始页]\n"
                "示例: /eh 看 123456/abcdef0123\n"
                "示例: /eh 看 123456/abcdef0123 5")
            return True

        parts = arg.split()
        id_part = parts[0]
        start_page = 1
        if len(parts) > 1:
            try:
                start_page = max(1, int(parts[1]))
            except:
                pass

        gid, token = self._parse_gallery_id(id_part)
        if not gid:
            idx = re.match(r'^(\d+)$', id_part)
            if idx and user_id in self._last_search:
                i = int(idx.group(1)) - 1
                results = self._last_search[user_id]
                if 0 <= i < len(results):
                    gid = results[i].get('gid')
                    token = results[i].get('token')
            if not gid:
                self._send_text(group_id, user_id, "无法解析画廊ID")
                return True

        if not self._check_cooldown(user_id):
            self._send_text(group_id, user_id, "操作太频繁，请稍后再试")
            return True

        self._send_text(group_id, user_id, f"正在获取 {gid} 第{start_page}页起...")

        try:
            gallery_page = (start_page - 1) // 20
            image_pages, _ = self._get_gallery_images_page(gid, token, gallery_page)

            if not image_pages:
                self._send_text(group_id, user_id, "未找到图片页面")
                return True

            offset = (start_page - 1) % 20
            pages_to_show = image_pages[offset:offset + 5]

            sent = 0
            for page_url in pages_to_show:
                try:
                    img_url = self._get_image_url(page_url)
                    if img_url:
                        resp = self._get(img_url, timeout=20)
                        if resp.status_code == 200 and len(resp.content) > 1000:
                            self._send_photo(group_id, resp.content)
                            sent += 1
                    time.sleep(1)
                except Exception as e:
                    print(f"[EH] Read page error: {e}")

            if sent == 0:
                self._send_text(group_id, user_id, "图片获取失败")
            else:
                self._send_text(group_id, user_id,
                    f"已发送第 {start_page}-{start_page + sent - 1} 页 (共5张/次)\n"
                    f"继续: /eh 看 {gid}/{token} {start_page + sent}")

        except Exception as e:
            print(f"[EH] Read error: {traceback.format_exc()}")
            self._send_text(group_id, user_id, f"阅读失败: {str(e)[:80]}")

        return True

    # ===== Download =====

    def _handle_download(self, group_id, user_id, arg):
        if not HAS_PYZIPPER:
            self._send_text(group_id, user_id, "下载功能需要 pyzipper 库")
            return True

        if not arg:
            self._send_text(group_id, user_id, "用法: /eh 下载 <GID>/<TOKEN>")
            return True

        gid, token = self._parse_gallery_id(arg)
        if not gid:
            idx = re.match(r'^(\d+)$', arg.strip())
            if idx and user_id in self._last_search:
                i = int(idx.group(1)) - 1
                results = self._last_search[user_id]
                if 0 <= i < len(results):
                    gid = results[i].get('gid')
                    token = results[i].get('token')
            if not gid:
                self._send_text(group_id, user_id, "无法解析画廊ID")
                return True

        dl_key = f"{gid}"
        with self._dl_lock:
            if dl_key in self._downloading:
                self._send_text(group_id, user_id, f"EH{gid} 正在下载中...")
                return True
            self._downloading.add(dl_key)

        self._send_text(group_id, user_id, f"开始下载 EH{gid}，下载完成后将发送加密压缩包...")

        try:
            api_data = self._api_gdata([(gid, token)])
            galleries = api_data.get('gmetadata', [])
            if not galleries:
                self._send_text(group_id, user_id, "获取画廊信息失败")
                return True

            g = galleries[0]
            total_count = int(g.get('filecount', 0))
            title = g.get('title', str(gid))

            gallery_dir = os.path.join(self.download_dir, f"eh_{gid}")
            os.makedirs(gallery_dir, exist_ok=True)

            downloaded = 0
            page = 0
            while True:
                image_pages, total_gallery_pages = self._get_gallery_images_page(gid, token, page)
                if not image_pages:
                    break

                for ip_url in image_pages:
                    try:
                        img_url = self._get_image_url(ip_url)
                        if img_url:
                            resp = self._get(img_url, timeout=30)
                            if resp.status_code == 200 and len(resp.content) > 500:
                                ext = '.jpg'
                                if 'png' in img_url.lower():
                                    ext = '.png'
                                elif 'gif' in img_url.lower():
                                    ext = '.gif'
                                fname = f"{downloaded + 1:04d}{ext}"
                                with open(os.path.join(gallery_dir, fname), 'wb') as f:
                                    f.write(resp.content)
                                downloaded += 1
                        time.sleep(1.5)
                    except Exception as e:
                        print(f"[EH] Download image error: {e}")

                page += 1
                if total_gallery_pages and page >= total_gallery_pages:
                    break
                time.sleep(1)

            if downloaded == 0:
                self._send_text(group_id, user_id, "下载失败：没有成功下载任何图片")
                shutil.rmtree(gallery_dir, ignore_errors=True)
                return True

            safe_name = re.sub(r'[\\/:*?"<>|]', '_', title)[:30]
            zip_name = f"EH{gid}_{safe_name}.zip"
            zip_path = os.path.join(self.download_dir, zip_name)

            rand_pwd = self._gen_password()
            password = rand_pwd.encode('utf-8')
            with pyzipper.AESZipFile(zip_path, 'w',
                                      compression=pyzipper.ZIP_DEFLATED,
                                      encryption=pyzipper.WZ_AES) as zf:
                zf.setpassword(password)
                for root, dirs, files in os.walk(gallery_dir):
                    for fname in sorted(files):
                        fpath = os.path.join(root, fname)
                        arcname = os.path.relpath(fpath, gallery_dir)
                        zf.write(fpath, arcname)

            zip_size = os.path.getsize(zip_path)
            size_mb = zip_size / (1024 * 1024)

            shutil.rmtree(gallery_dir, ignore_errors=True)

            abs_zip_path = os.path.abspath(zip_path)
            self.send_group_file(group_id, abs_zip_path, zip_name)

            self._send_text(group_id, user_id,
                f"EH{gid} 下载完成\n"
                f"共 {downloaded}/{total_count} 张图片 ({size_mb:.1f}MB)\n"
                f"文件名: {zip_name}\n"
                f"解压密码: {rand_pwd}")

        except Exception as e:
            print(f"[EH] Download error: {traceback.format_exc()}")
            self._send_text(group_id, user_id, f"下载失败: {str(e)[:100]}")
        finally:
            with self._dl_lock:
                self._downloading.discard(dl_key)

        return True

    # ===== Hot =====

    def _handle_hot(self, group_id, user_id):
        if not self._check_cooldown(user_id):
            self._send_text(group_id, user_id, "操作太频繁，请稍后再试")
            return True

        self._send_text(group_id, user_id, "正在获取热门画廊...")

        try:
            resp = self._get(EH_POPULAR)
            gid_tokens = self._parse_search_html(resp.text)[:10]

            if not gid_tokens:
                self._send_text(group_id, user_id, "获取热门列表失败")
                return True

            api_data = self._api_gdata(gid_tokens)
            galleries = api_data.get('gmetadata', [])

            if not galleries:
                self._send_text(group_id, user_id, "获取数据失败")
                return True

            self._last_search[user_id] = galleries

            img = self._render_search(galleries, "热门画廊 Top 10")
            if img:
                self._send_photo(group_id, img)
            else:
                lines = []
                for i, g in enumerate(galleries):
                    title = g.get('title', '')[:40]
                    lines.append(f"{i+1}. {title}")
                self._send_text(group_id, user_id, '\n'.join(lines))

        except Exception as e:
            print(f"[EH] Hot error: {traceback.format_exc()}")
            self._send_text(group_id, user_id, f"获取热门失败: {str(e)[:80]}")

        return True

    # ===== Help =====

    def _handle_help(self, group_id, user_id):
        img = self._render_help()
        if img:
            self._send_photo(group_id, img)
        else:
            self._send_text(group_id, user_id,
                "E-Hentai 帮助:\n"
                "/eh 搜索 <词> [-l 语言] [-c 分类]\n"
                "/eh 详情 <GID/TOKEN>\n"
                "/eh 评论 <GID/TOKEN> [页码]\n"
                "/eh 看 <GID/TOKEN> [页码]\n"
                "/eh 下载 <GID/TOKEN>\n"
                "/eh 热门\n"
                "/eh 帮助")
        return True

    def _render_help(self):
        if not HAS_PIL:
            return None
        try:
            font_title = self._get_font(26)
            font_cmd = self._get_font(18)
            font_desc = self._get_font(14)

            w = 860
            pad = 20
            header_h = 70

            tmp_img = Image.new('RGB', (w, 10), (25, 25, 38))
            tmp_draw = ImageDraw.Draw(tmp_img)

            cmds = [
                ("/eh 搜索 <关键词>", "搜索画廊 (支持 -l 语言 -c 分类)"),
                ("/eh 搜索 xxx -l 中文", "搜索中文翻译的 xxx"),
                ("/eh 搜索 xxx -c 同人志", "搜索同人志分类的 xxx"),
                ("/eh 详情 <GID/TOKEN>", "查看画廊详情+封面"),
                ("/eh 详情 <序号>", "查看搜索结果中的画廊"),
                ("/eh 评论 <GID/TOKEN> [页码]", "分页查看评论(每页3条)"),
                ("/eh 看 <GID/TOKEN> [页码]", "在线阅读(每次5页)"),
                ("/eh 下载 <GID/TOKEN>", "下载加密压缩包"),
                ("/eh 热门", "查看热门画廊"),
                ("/eh 帮助", "显示此帮助信息"),
            ]

            y = header_h + 12
            for cmd, desc in cmds:
                cmd_lines = self._wrap_text(tmp_draw, cmd, font_cmd, w - pad * 2, max_lines=2)
                desc_lines = self._wrap_text(tmp_draw, desc, font_desc, w - pad * 2, max_lines=2)
                y += len(cmd_lines) * 24 + len(desc_lines) * 20 + 14

            y += 40
            y += 40
            y += 60
            h = y + 18

            img = Image.new('RGB', (w, h), (25, 25, 38))
            draw = ImageDraw.Draw(img)

            draw.rectangle((0, 0, w, header_h), fill=(40, 40, 58))
            draw.text((w // 2, header_h // 2), "E-Hentai 指令帮助", fill=(100, 200, 255),
                       font=font_title, anchor="mm")

            y = header_h + 16
            for cmd, desc in cmds:
                cmd_lines = self._wrap_text(draw, cmd, font_cmd, w - pad * 2, max_lines=2)
                desc_lines = self._wrap_text(draw, desc, font_desc, w - pad * 2, max_lines=2)
                y = self._draw_text_lines(draw, pad, y, cmd_lines, font_cmd, (100, 200, 255), 24)
                y = self._draw_text_lines(draw, pad, y, desc_lines, font_desc, (170, 170, 190), 20)
                y += 14

            y += 6
            lang_str = "语言: 中文/英语/日语/韩语/俄语/法语/..."
            cat_str = "分类: 同人志/漫画/画集/游戏CG/图集/Cosplay/全年龄/西方"
            draw.text((pad, y), lang_str, fill=(200, 180, 100), font=font_desc)
            draw.text((pad, y + 24), cat_str, fill=(200, 180, 100), font=font_desc)

            draw.text((w // 2, h - 44), "解压密码: 每次下载随机生成",
                       fill=(255, 180, 100), font=font_desc, anchor="mm")
            draw.text((w // 2, h - 20), "用法: @我 /eh 搜索 xxx  |  冷却5秒",
                       fill=(120, 120, 140), font=font_desc, anchor="mm")

            buf = BytesIO()
            img.save(buf, format='JPEG', quality=85)
            return buf.getvalue()
        except:
            return None


register_plugin(EhentaiPlugin())
