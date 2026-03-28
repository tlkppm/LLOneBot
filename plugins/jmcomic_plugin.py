import os
import re
import time
import base64
import traceback
import shutil
import random
import string
from io import BytesIO

try:
    from PIL import Image, ImageDraw, ImageFont
    HAS_PIL = True
except:
    HAS_PIL = False

try:
    import jmcomic
    HAS_JM = True
except:
    HAS_JM = False

try:
    import pyzipper
    HAS_PYZIPPER = True
except:
    HAS_PYZIPPER = False


class JMComicPlugin(LCHBotPlugin):
    def __init__(self):
        super().__init__()
        self.name = "JMComicPlugin"
        self.version = "1.0.0"
        self.author = "LCHBOT"
        self.description = "JM Comic - 禁漫搜索与阅读"
        self.priority = 90

        self.download_dir = "data/jmcomic"
        self.option = None
        self.client = None
        self._last_search = {}
        self._cooldown = {}
        self._downloading = set()
        self.COOLDOWN_SEC = 5

    def on_load(self):
        os.makedirs(self.download_dir, exist_ok=True)
        if HAS_JM:
            try:
                self.option = jmcomic.create_option_by_str(
                    "client:\n"
                    "  impl: api\n"
                    "  retry_times: 3\n"
                    "download:\n"
                    "  image:\n"
                    "    suffix: .jpg\n"
                    "  threading:\n"
                    "    image: 4\n"
                    "    photo: 2\n",
                    mode='yml'
                )
                self.client = self.option.build_jm_client()
                print(f"[{self.name}] JMComic plugin loaded (jmcomic {jmcomic.__version__})")
            except Exception as e:
                print(f"[{self.name}] Failed to init jmcomic client: {e}")
                self.client = None
        else:
            print(f"[{self.name}] jmcomic not installed, plugin disabled")

    def on_unload(self):
        print(f"[{self.name}] JMComic plugin unloaded")

    def _check_cooldown(self, user_id):
        now = time.time()
        last = self._cooldown.get(user_id, 0)
        if now - last < self.COOLDOWN_SEC:
            return False
        self._cooldown[user_id] = now
        return True

    @staticmethod
    def _gen_password(length=8):
        chars = string.ascii_letters + string.digits
        return ''.join(random.choice(chars) for _ in range(length))

    def _send_photo(self, group_id, img_bytes):
        """Send image as photo (subType=0) to avoid garbled emoji encoding"""
        b64 = base64.b64encode(img_bytes).decode()
        self.send_group_msg(group_id, f"[CQ:image,file=base64://{b64},subType=0]")

    def _send_photo_b64(self, group_id, b64_str):
        """Send base64 string as photo"""
        self.send_group_msg(group_id, f"[CQ:image,file=base64://{b64_str},subType=0]")

    def _send_text(self, group_id, user_id, text):
        self.send_group_msg(group_id, f"[CQ:at,qq={user_id}] {text}")

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

        if not clean.startswith("/jm"):
            return False

        if not HAS_JM or not self.client:
            self._send_text(group_id, user_id, "JMComic 模块未就绪，请联系管理员")
            return True

        cmd_body = clean[3:].strip()
        parts = cmd_body.split(None, 1)
        if not parts:
            return self._handle_help(group_id, user_id)

        subcmd = parts[0].lower()
        arg = parts[1] if len(parts) > 1 else ""

        if subcmd in ("搜索", "search", "s"):
            return self._handle_search(group_id, user_id, arg)
        elif subcmd in ("详情", "detail", "d"):
            return self._handle_detail(group_id, user_id, arg)
        elif subcmd in ("看", "read", "r"):
            return self._handle_read(group_id, user_id, arg)
        elif subcmd in ("下载", "download", "dl"):
            return self._handle_download(group_id, user_id, arg)
        elif subcmd in ("热门", "hot", "h"):
            return self._handle_hot(group_id, user_id)
        elif subcmd in ("帮助", "help"):
            return self._handle_help(group_id, user_id)
        else:
            return self._handle_help(group_id, user_id)

    # ========== Search ==========

    def _handle_search(self, group_id, user_id, keyword):
        if not keyword:
            self._send_text(group_id, user_id, "请输入搜索关键词\n用法: /jm 搜索 <关键词>")
            return True

        if not self._check_cooldown(user_id):
            self._send_text(group_id, user_id, "操作太频繁，请稍后再试")
            return True

        self._send_text(group_id, user_id, f"正在搜索: {keyword}...")

        try:
            page = self.client.search_site(keyword, page=1)
            if not page or len(page) == 0:
                self._send_text(group_id, user_id, f"未找到与「{keyword}」相关的结果")
                return True

            results = []
            for aid, ainfo in page.content[:10]:
                title = ainfo.get('name', '未知')
                tags = ainfo.get('tags', [])
                tag_str = ', '.join(tags[:5]) if tags else ''
                results.append((aid, title, tag_str))

            self._last_search[str(group_id)] = results

            img = self._render_search_results(keyword, results, len(page.content), page.total)
            if img:
                self._send_photo(group_id, img)
            else:
                text = f"搜索结果: {keyword} (共{page.total}个)\n"
                text += "=" * 30 + "\n"
                for i, (aid, title, tags) in enumerate(results, 1):
                    text += f"{i}. [JM{aid}] {title[:40]}\n"
                text += "\n查看详情: /jm 详情 <车号>"
                self._send_text(group_id, user_id, text)

            return True

        except Exception as e:
            print(f"[JMComic] Search error: {traceback.format_exc()}")
            self._send_text(group_id, user_id, f"搜索失败: {str(e)[:100]}")
            return True

    def _render_search_results(self, keyword, results, page_count, total):
        if not HAS_PIL:
            return None
        try:
            count = len(results)
            row_h = 56
            header_h = 80
            footer_h = 50
            h = header_h + count * row_h + footer_h
            w = 620

            img = Image.new('RGB', (w, h), (30, 30, 40))
            draw = ImageDraw.Draw(img)

            try:
                font_title = ImageFont.truetype("msyh.ttc", 22)
                font_item = ImageFont.truetype("msyh.ttc", 16)
                font_tag = ImageFont.truetype("msyh.ttc", 12)
                font_footer = ImageFont.truetype("msyh.ttc", 13)
            except:
                font_title = ImageFont.load_default()
                font_item = font_title
                font_tag = font_title
                font_footer = font_title

            draw.rectangle((0, 0, w, header_h), fill=(45, 45, 60))
            draw.text((20, 15), f"JMComic 搜索: {keyword}", fill=(255, 200, 100), font=font_title)
            draw.text((20, 48), f"共 {total} 个结果", fill=(180, 180, 180), font=font_tag)

            for i, (aid, title, tags) in enumerate(results):
                y = header_h + i * row_h
                bg = (38, 38, 50) if i % 2 == 0 else (32, 32, 44)
                draw.rectangle((0, y, w, y + row_h), fill=bg)

                idx_color = (255, 180, 80) if i < 3 else (160, 160, 180)
                draw.text((15, y + 8), f"{i+1}.", fill=idx_color, font=font_item)

                draw.text((45, y + 4), f"JM{aid}", fill=(100, 180, 255), font=font_tag)
                display_title = title[:35] + "..." if len(title) > 35 else title
                draw.text((120, y + 4), display_title, fill=(240, 240, 240), font=font_item)

                if tags:
                    display_tags = tags[:50] + "..." if len(tags) > 50 else tags
                    draw.text((45, y + 30), display_tags, fill=(130, 130, 150), font=font_tag)

            fy = header_h + count * row_h + 15
            draw.text((w // 2, fy), "查看详情: /jm 详情 <车号>  |  阅读: /jm 看 <车号>",
                       fill=(140, 140, 160), font=font_footer, anchor="mm")

            buf = BytesIO()
            img.save(buf, format='JPEG', quality=85)
            return buf.getvalue()
        except Exception as e:
            print(f"[JMComic] Render search error: {e}")
            return None

    # ========== Detail ==========

    def _handle_detail(self, group_id, user_id, album_id_str):
        if not album_id_str:
            self._send_text(group_id, user_id, "请输入车号\n用法: /jm 详情 <车号>")
            return True

        album_id = re.sub(r'[^0-9]', '', album_id_str)
        if not album_id:
            self._send_text(group_id, user_id, "车号格式错误，请输入数字")
            return True

        if not self._check_cooldown(user_id):
            self._send_text(group_id, user_id, "操作太频繁，请稍后再试")
            return True

        self._send_text(group_id, user_id, f"正在获取 JM{album_id} 详情...")

        try:
            album = self.client.get_album_detail(album_id)

            cover_img = None
            try:
                cover_path = os.path.join(self.download_dir, f"cover_{album_id}.jpg")
                self.client.download_album_cover(album_id, cover_path)
                if os.path.exists(cover_path) and os.path.getsize(cover_path) > 100:
                    cover_img = Image.open(cover_path).convert('RGB')
                try:
                    os.remove(cover_path)
                except:
                    pass
            except Exception as e:
                print(f"[JMComic] Cover download: {e}")

            info_lines = [
                f"JM{album.album_id}",
                album.name,
                f"作者: {album.author}",
                f"页数: {album.page_count}  |  章节: {len(album)}章",
            ]
            if album.views and str(album.views) != '0':
                info_lines.append(f"观看: {album.views}  |  喜欢: {album.likes}")
            if album.pub_date and str(album.pub_date) != '0':
                info_lines.append(f"发布: {album.pub_date}")
            if album.update_date and str(album.update_date) != '0':
                info_lines.append(f"更新: {album.update_date}")
            if album.tags:
                info_lines.append(f"标签: {', '.join(album.tags[:12])}")

            rendered = self._render_detail(info_lines, cover_img)
            if rendered:
                self._send_photo(group_id, rendered)
            else:
                self._send_text(group_id, user_id, '\n'.join(info_lines))

            return True

        except Exception as e:
            print(f"[JMComic] Detail error: {traceback.format_exc()}")
            self._send_text(group_id, user_id, f"获取详情失败: {str(e)[:100]}")
            return True

    def _render_detail(self, info_lines, cover_img=None):
        if not HAS_PIL:
            return None
        try:
            try:
                font_id = ImageFont.truetype("msyh.ttc", 28)
                font_title = ImageFont.truetype("msyh.ttc", 22)
                font_info = ImageFont.truetype("msyh.ttc", 18)
                font_tag = ImageFont.truetype("msyh.ttc", 16)
            except:
                font_id = ImageFont.load_default()
                font_title = font_id
                font_info = font_id
                font_tag = font_id

            cover_w, cover_h = 0, 0
            cover_resized = None
            if cover_img:
                cw, ch = cover_img.size
                target_h = 480
                scale = target_h / ch
                cover_w = int(cw * scale)
                cover_h = target_h
                cover_resized = cover_img.resize((cover_w, cover_h), Image.LANCZOS)

            info_x = cover_w + 30
            line_h = 40
            info_w = 420
            total_w = info_x + info_w + 30
            if total_w < 700:
                total_w = 700

            title_block_h = 80
            info_block_h = (len(info_lines) - 2) * line_h + 20
            total_h = max(cover_h + 40, title_block_h + info_block_h + 40)
            if total_h < 300:
                total_h = 300

            img = Image.new('RGB', (total_w, total_h), (25, 25, 38))
            draw = ImageDraw.Draw(img)

            draw.rectangle((0, 0, total_w, title_block_h), fill=(40, 40, 58))

            if cover_resized:
                cy = (total_h - cover_h) // 2
                if cy < title_block_h + 10:
                    cy = title_block_h + 10
                img.paste(cover_resized, (15, cy))
                draw.rectangle((14, cy, 15 + cover_w + 1, cy + cover_h + 1),
                                outline=(80, 80, 120), width=2)

            jm_id = info_lines[0] if info_lines else ""
            draw.text((20, 15), jm_id, fill=(100, 200, 255), font=font_id)

            title_text = info_lines[1] if len(info_lines) > 1 else ""
            max_title_chars = (total_w - 40) // 22
            if len(title_text) > max_title_chars:
                title_text = title_text[:max_title_chars] + "..."
            draw.text((20, 48), title_text, fill=(255, 220, 100), font=font_title)

            y = title_block_h + 20
            text_x = info_x if cover_resized else 30

            for line in info_lines[2:]:
                if line.startswith("标签"):
                    color = (140, 200, 255)
                    font = font_tag
                elif line.startswith("作者"):
                    color = (220, 220, 240)
                    font = font_info
                else:
                    color = (190, 190, 210)
                    font = font_info
                draw.text((text_x, y), line, fill=color, font=font)
                y += line_h

            y += 15
            draw.text((text_x, y), "阅读: /jm 看 <车号>", fill=(100, 160, 100), font=font_tag)
            draw.text((text_x, y + 28), "下载: /jm 下载 <车号>", fill=(100, 160, 100), font=font_tag)

            buf = BytesIO()
            img.save(buf, format='JPEG', quality=90)
            return buf.getvalue()
        except Exception as e:
            print(f"[JMComic] Render detail error: {e}")
            return None

    # ========== Read ==========

    def _handle_read(self, group_id, user_id, arg):
        if not arg:
            self._send_text(group_id, user_id,
                "用法: /jm 看 <车号> [章节] [起始页]\n"
                "示例: /jm 看 123456\n"
                "示例: /jm 看 123456 1 5")
            return True

        parts = arg.split()
        album_id = re.sub(r'[^0-9]', '', parts[0])
        chapter_idx = 0
        start_page = 0

        if len(parts) > 1:
            try:
                chapter_idx = max(0, int(parts[1]) - 1)
            except:
                pass
        if len(parts) > 2:
            try:
                start_page = max(0, int(parts[2]) - 1)
            except:
                pass

        if not album_id:
            self._send_text(group_id, user_id, "车号格式错误")
            return True

        if not self._check_cooldown(user_id):
            self._send_text(group_id, user_id, "操作太频繁，请稍后再试")
            return True

        self._send_text(group_id, user_id, f"正在加载 JM{album_id} ...")

        try:
            album = self.client.get_album_detail(album_id)

            if chapter_idx >= len(album):
                self._send_text(group_id, user_id,
                    f"JM{album_id} 共 {len(album)} 章, 请输入1-{len(album)}")
                return True

            photo = album[chapter_idx]
            self.client.check_photo(photo)

            total_pages = len(photo)
            if start_page >= total_pages:
                self._send_text(group_id, user_id,
                    f"第{chapter_idx+1}章共 {total_pages} 页, 请输入1-{total_pages}")
                return True

            max_send = 5
            end_page = min(start_page + max_send, total_pages)

            self._send_text(group_id, user_id,
                f"JM{album_id} 第{chapter_idx+1}章 第{start_page+1}-{end_page}/{total_pages}页")

            sent = 0
            for page_idx in range(start_page, end_page):
                try:
                    image_detail = photo[page_idx]
                    save_path = os.path.join(self.download_dir,
                        f"read_{album_id}_{chapter_idx}_{page_idx}.jpg")

                    self.client.download_by_image_detail(image_detail, save_path)

                    if os.path.exists(save_path) and os.path.getsize(save_path) > 100:
                        with open(save_path, 'rb') as f:
                            self._send_photo(group_id, f.read())
                        sent += 1
                    try:
                        os.remove(save_path)
                    except:
                        pass
                except Exception as e:
                    print(f"[JMComic] Download page {page_idx} error: {e}")

            if end_page < total_pages:
                next_cmd = f"/jm 看 {album_id} {chapter_idx+1} {end_page+1}"
                self._send_text(group_id, user_id,
                    f"已发送{sent}页 | 下一页: {next_cmd}")

            return True

        except Exception as e:
            print(f"[JMComic] Read error: {traceback.format_exc()}")
            self._send_text(group_id, user_id, f"加载失败: {str(e)[:100]}")
            return True

    # ========== Download ==========

    def _handle_download(self, group_id, user_id, arg):
        if not HAS_PYZIPPER:
            self._send_text(group_id, user_id, "加密压缩模块未安装(pyzipper)，请联系管理员")
            return True

        if not arg:
            self._send_text(group_id, user_id, "用法: /jm 下载 <车号> [章节]\n示例: /jm 下载 123456\n示例: /jm 下载 123456 2")
            return True

        parts = arg.split()
        album_id = re.sub(r'[^0-9]', '', parts[0])
        chapter_idx = None
        if len(parts) > 1:
            try:
                chapter_idx = max(0, int(parts[1]) - 1)
            except:
                pass

        if not album_id:
            self._send_text(group_id, user_id, "车号格式错误")
            return True

        dl_key = f"{group_id}_{album_id}"
        if dl_key in self._downloading:
            self._send_text(group_id, user_id, f"JM{album_id} 正在下载中，请稍候...")
            return True

        if not self._check_cooldown(user_id):
            self._send_text(group_id, user_id, "操作太频繁，请稍后再试")
            return True

        self._downloading.add(dl_key)
        try:
            self._send_text(group_id, user_id, f"开始下载 JM{album_id}，下载完成后将发送加密压缩包...")

            album = self.client.get_album_detail(album_id)
            album_dir = os.path.join(self.download_dir, f"dl_{album_id}")
            os.makedirs(album_dir, exist_ok=True)

            if chapter_idx is not None:
                if chapter_idx >= len(album):
                    self._send_text(group_id, user_id, f"JM{album_id} 共{len(album)}章")
                    return True
                chapters = [(chapter_idx, album[chapter_idx])]
                ch_label = f"_ch{chapter_idx+1}"
            else:
                chapters = [(i, album[i]) for i in range(len(album))]
                ch_label = ""

            total_images = 0
            for ci, photo in chapters:
                self.client.check_photo(photo)
                ch_dir = os.path.join(album_dir, f"ch{ci+1:03d}")
                os.makedirs(ch_dir, exist_ok=True)
                for pi in range(len(photo)):
                    try:
                        img_detail = photo[pi]
                        save_path = os.path.join(ch_dir, f"{pi+1:04d}.jpg")
                        if not os.path.exists(save_path):
                            self.client.download_by_image_detail(img_detail, save_path)
                        total_images += 1
                    except Exception as e:
                        print(f"[JMComic] Download ch{ci+1} p{pi+1} error: {e}")

            if total_images == 0:
                self._send_text(group_id, user_id, "下载失败：没有成功下载任何图片")
                return True

            safe_name = re.sub(r'[\\/:*?"<>|]', '_', album.name)[:30]
            zip_name = f"JM{album_id}{ch_label}_{safe_name}.zip"
            zip_path = os.path.join(self.download_dir, zip_name)

            rand_pwd = self._gen_password()
            password = rand_pwd.encode('utf-8')
            with pyzipper.AESZipFile(zip_path, 'w',
                                      compression=pyzipper.ZIP_DEFLATED,
                                      encryption=pyzipper.WZ_AES) as zf:
                zf.setpassword(password)
                for root, dirs, files in os.walk(album_dir):
                    for fname in sorted(files):
                        fpath = os.path.join(root, fname)
                        arcname = os.path.relpath(fpath, album_dir)
                        zf.write(fpath, arcname)

            zip_size = os.path.getsize(zip_path)
            size_mb = zip_size / (1024 * 1024)

            shutil.rmtree(album_dir, ignore_errors=True)

            abs_zip_path = os.path.abspath(zip_path)
            self.send_group_file(group_id, abs_zip_path, zip_name)

            self._send_text(group_id, user_id,
                f"JM{album_id} 下载完成\n"
                f"共 {total_images} 张图片 ({size_mb:.1f}MB)\n"
                f"文件名: {zip_name}\n"
                f"解压密码: {rand_pwd}")

        except Exception as e:
            print(f"[JMComic] Download error: {traceback.format_exc()}")
            self._send_text(group_id, user_id, f"下载失败: {str(e)[:100]}")
        finally:
            self._downloading.discard(dl_key)

        return True

    # ========== Hot ==========

    def _handle_hot(self, group_id, user_id):
        if not self._check_cooldown(user_id):
            self._send_text(group_id, user_id, "操作太频繁，请稍后再试")
            return True

        self._send_text(group_id, user_id, "正在获取热门排行...")

        try:
            page = self.client.week_ranking(page=1)
            if not page or len(page) == 0:
                self._send_text(group_id, user_id, "获取排行失败")
                return True

            results = []
            for aid, ainfo in page.content[:10]:
                title = ainfo.get('name', '未知')
                tags = ainfo.get('tags', [])
                tag_str = ', '.join(tags[:5]) if tags else ''
                results.append((aid, title, tag_str))

            img = self._render_search_results("周热门排行", results, len(page.content), page.total)
            if img:
                self._send_photo(group_id, img)
            else:
                text = "周热门排行 Top 10\n" + "=" * 30 + "\n"
                for i, (aid, title, tags) in enumerate(results, 1):
                    text += f"{i}. [JM{aid}] {title[:40]}\n"
                self._send_text(group_id, user_id, text)

            return True

        except Exception as e:
            print(f"[JMComic] Hot error: {traceback.format_exc()}")
            self._send_text(group_id, user_id, f"获取失败: {str(e)[:100]}")
            return True

    # ========== Help ==========

    def _handle_help(self, group_id, user_id):
        img = self._render_help()
        if img:
            self._send_photo(group_id, img)
        else:
            self._send_text(group_id, user_id,
                "JMComic 指令 (先@我再输入):\n"
                "/jm 搜索 <关键词> - 搜索漫画\n"
                "/jm 详情 <车号> - 查看漫画详情\n"
                "/jm 看 <车号> [章节] [页码] - 阅读漫画\n"
                "/jm 下载 <车号> [章节] - 下载加密压缩包\n"
                "/jm 热门 - 周热门排行\n"
                "/jm 帮助 - 显示此帮助")
        return True

    def _render_help(self):
        if not HAS_PIL:
            return None
        try:
            w, h = 500, 400
            img = Image.new('RGB', (w, h), (30, 30, 42))
            draw = ImageDraw.Draw(img)

            try:
                font_title = ImageFont.truetype("msyh.ttc", 22)
                font_cmd = ImageFont.truetype("msyh.ttc", 15)
                font_desc = ImageFont.truetype("msyh.ttc", 13)
            except:
                font_title = ImageFont.load_default()
                font_cmd = font_title
                font_desc = font_title

            draw.rectangle((0, 0, w, 50), fill=(50, 50, 70))
            draw.text((w // 2, 25), "JMComic 使用帮助", fill=(255, 210, 100),
                       font=font_title, anchor="mm")

            cmds = [
                ("/jm 搜索 <关键词>", "搜索漫画"),
                ("/jm 详情 <车号>", "查看漫画详情+封面"),
                ("/jm 看 <车号> [章] [页]", "在线阅读(每次5页)"),
                ("/jm 下载 <车号> [章节]", "下载整本/单章 -> AES加密压缩包"),
                ("/jm 热门", "周热门排行 Top 10"),
                ("/jm 帮助", "显示此帮助信息"),
            ]

            y = 70
            for cmd, desc in cmds:
                draw.text((25, y), cmd, fill=(100, 200, 255), font=font_cmd)
                draw.text((25, y + 22), desc, fill=(170, 170, 190), font=font_desc)
                y += 48

            draw.text((w // 2, h - 50), "解压密码: 每次下载随机生成",
                       fill=(255, 180, 100), font=font_cmd, anchor="mm")
            draw.text((w // 2, h - 25), "用法: @我 /jm 搜索 xxx  |  冷却5秒",
                       fill=(120, 120, 140), font=font_desc, anchor="mm")

            buf = BytesIO()
            img.save(buf, format='JPEG', quality=85)
            return buf.getvalue()
        except:
            return None


register_plugin(JMComicPlugin())
