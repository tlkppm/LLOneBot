import json
import os
import random
import time
import base64
from datetime import datetime, date
from io import BytesIO

try:
    from PIL import Image, ImageDraw, ImageFont, ImageFilter
    HAS_PIL = True
except:
    HAS_PIL = False

class TouQingPlugin(LCHBotPlugin):
    def __init__(self):
        super().__init__()
        self.name = "TouQingPlugin"
        self.version = "1.0.0"
        self.author = "LCHBOT"
        self.description = "偷情 - 随机配对约会游戏"
        self.priority = 100
        
        self.data_file = "data/touqing_data.json"
        self.data = {"players": {}, "matches": {}, "history": []}
        
        self.waiting_queue = {}
        
        self.scenes = [
            {"name": "浪漫海滩", "desc": "夕阳下的沙滩漫步", "mood": "romantic", "color": (255, 180, 100)},
            {"name": "星空露台", "desc": "繁星点点的夜晚", "mood": "romantic", "color": (50, 50, 120)},
            {"name": "咖啡小馆", "desc": "温馨的午后时光", "mood": "cozy", "color": (139, 90, 43)},
            {"name": "樱花树下", "desc": "粉色花瓣飘落", "mood": "sweet", "color": (255, 182, 193)},
            {"name": "摩天轮上", "desc": "俯瞰城市夜景", "mood": "exciting", "color": (100, 100, 200)},
            {"name": "雨中漫步", "desc": "共撑一把伞", "mood": "romantic", "color": (100, 150, 180)},
            {"name": "电影院后排", "desc": "黑暗中的悄悄话", "mood": "secret", "color": (40, 40, 60)},
            {"name": "游乐园", "desc": "欢声笑语的约会", "mood": "fun", "color": (255, 200, 50)},
            {"name": "温泉旅馆", "desc": "放松身心的时刻", "mood": "relaxing", "color": (200, 220, 255)},
            {"name": "私人游艇", "desc": "海上的浪漫之旅", "mood": "luxury", "color": (30, 100, 180)},
        ]
        
        self.actions = [
            {"name": "牵手", "intimacy": 5},
            {"name": "拥抱", "intimacy": 10},
            {"name": "亲吻", "intimacy": 20},
            {"name": "耳语", "intimacy": 8},
            {"name": "喂食", "intimacy": 12},
            {"name": "跳舞", "intimacy": 15},
            {"name": "对视", "intimacy": 7},
            {"name": "合影", "intimacy": 6},
            {"name": "送花", "intimacy": 10},
            {"name": "背后抱", "intimacy": 18},
        ]
        
        self.events = [
            {"type": "good", "desc": "你们发现了一家隐秘的甜品店", "intimacy": 15},
            {"type": "good", "desc": "偶遇流星划过天际", "intimacy": 20},
            {"type": "good", "desc": "街头艺人为你们演奏情歌", "intimacy": 12},
            {"type": "good", "desc": "收到神秘人送的玫瑰花", "intimacy": 10},
            {"type": "bad", "desc": "被熟人撞见了！", "intimacy": -10},
            {"type": "bad", "desc": "突然下起大雨", "intimacy": -5},
            {"type": "bad", "desc": "约会中途手机响了", "intimacy": -8},
            {"type": "neutral", "desc": "路过一对情侣在吵架", "intimacy": 0},
            {"type": "special", "desc": "今天是特别的日子", "intimacy": 25},
        ]
        
        self.titles = [
            {"name": "路人", "min_intimacy": 0},
            {"name": "暧昧对象", "min_intimacy": 50},
            {"name": "地下情人", "min_intimacy": 150},
            {"name": "秘密恋人", "min_intimacy": 300},
            {"name": "灵魂伴侣", "min_intimacy": 500},
            {"name": "命中注定", "min_intimacy": 1000},
        ]
    
    def on_load(self):
        os.makedirs("data", exist_ok=True)
        os.makedirs("data/avatars", exist_ok=True)
        self.load_data()
        print(f"[{self.name}] 偷情插件已加载")
    
    def on_unload(self):
        self.save_data()
    
    def load_data(self):
        try:
            if os.path.exists(self.data_file):
                with open(self.data_file, 'r', encoding='utf-8') as f:
                    self.data = json.load(f)
        except: pass
    
    def save_data(self):
        try:
            with open(self.data_file, 'w', encoding='utf-8') as f:
                json.dump(self.data, f, ensure_ascii=False, indent=2)
        except: pass
    
    def get_player(self, user_id):
        uid = str(user_id)
        if uid not in self.data["players"]:
            self.data["players"][uid] = {
                "nickname": "",
                "total_dates": 0,
                "total_intimacy": 0,
                "partners": {},
                "last_date": None,
                "in_date": False,
                "current_partner": None,
                "current_scene": None,
            }
        return self.data["players"][uid]
    
    def get_pair_key(self, uid1, uid2):
        return f"{min(uid1, uid2)}_{max(uid1, uid2)}"
    
    def get_intimacy(self, uid1, uid2):
        key = self.get_pair_key(str(uid1), str(uid2))
        if key not in self.data.get("matches", {}):
            self.data["matches"][key] = {"intimacy": 0, "dates": 0, "last_date": None}
        return self.data["matches"][key]
    
    def add_intimacy(self, uid1, uid2, amount):
        match = self.get_intimacy(uid1, uid2)
        match["intimacy"] = max(0, match.get("intimacy", 0) + amount)
        self.save_data()
        return match["intimacy"]
    
    def get_title(self, intimacy):
        title = self.titles[0]["name"]
        for t in self.titles:
            if intimacy >= t["min_intimacy"]:
                title = t["name"]
        return title
    
    def download_avatar(self, user_id):
        try:
            avatar_path = f"data/avatars/{user_id}.jpg"
            if os.path.exists(avatar_path):
                mtime = os.path.getmtime(avatar_path)
                if time.time() - mtime < 3600:
                    return avatar_path
            
            import urllib.request
            url = f"https://q1.qlogo.cn/g?b=qq&nk={user_id}&s=640"
            urllib.request.urlretrieve(url, avatar_path)
            return avatar_path
        except:
            return None
    
    def generate_match_image(self, user1_id, user1_name, user2_id, user2_name, scene):
        if not HAS_PIL:
            return None
        try:
            img = Image.new('RGB', (500, 280), color=scene["color"])
            draw = ImageDraw.Draw(img)
            
            overlay = Image.new('RGBA', (500, 280), (0, 0, 0, 100))
            img = Image.alpha_composite(img.convert('RGBA'), overlay).convert('RGB')
            draw = ImageDraw.Draw(img)
            
            try:
                font_title = ImageFont.truetype("msyh.ttc", 28)
                font_scene = ImageFont.truetype("msyh.ttc", 18)
                font_name = ImageFont.truetype("msyh.ttc", 16)
            except:
                font_title = ImageFont.load_default()
                font_scene = font_title
                font_name = font_title
            
            avatar1_path = self.download_avatar(user1_id)
            if avatar1_path and os.path.exists(avatar1_path):
                avatar1 = Image.open(avatar1_path).resize((90, 90))
                mask = Image.new('L', (90, 90), 0)
                ImageDraw.Draw(mask).ellipse((0, 0, 90, 90), fill=255)
                img.paste(avatar1, (80, 90), mask)
            
            avatar2_path = self.download_avatar(user2_id)
            if avatar2_path and os.path.exists(avatar2_path):
                avatar2 = Image.open(avatar2_path).resize((90, 90))
                mask = Image.new('L', (90, 90), 0)
                ImageDraw.Draw(mask).ellipse((0, 0, 90, 90), fill=255)
                img.paste(avatar2, (330, 90), mask)
            
            draw.text((250, 30), "[ 配对成功 ]", fill=(255, 255, 255), font=font_title, anchor="mm")
            draw.text((250, 65), f"地点: {scene['name']}", fill=(255, 220, 150), font=font_scene, anchor="mm")
            
            draw.text((125, 195), user1_name[:8], fill=(255, 255, 255), font=font_name, anchor="mm")
            draw.text((375, 195), user2_name[:8], fill=(255, 255, 255), font=font_name, anchor="mm")
            
            draw.text((250, 140), "<3", fill=(255, 100, 100), font=font_title, anchor="mm")
            
            draw.text((250, 230), scene["desc"], fill=(200, 200, 200), font=font_name, anchor="mm")
            draw.text((250, 260), "发送 [偷情动作] 开始互动", fill=(150, 255, 150), font=font_name, anchor="mm")
            
            buffer = BytesIO()
            img.save(buffer, format='PNG')
            return base64.b64encode(buffer.getvalue()).decode()
        except Exception as e:
            print(f"[TouQing] Match image error: {e}")
            return None
    
    def generate_action_image(self, user1_id, user1_name, user2_id, user2_name, action, intimacy, total_intimacy):
        if not HAS_PIL:
            return None
        try:
            img = Image.new('RGB', (450, 200), color=(40, 30, 50))
            draw = ImageDraw.Draw(img)
            
            try:
                font_title = ImageFont.truetype("msyh.ttc", 24)
                font_action = ImageFont.truetype("msyh.ttc", 32)
                font_info = ImageFont.truetype("msyh.ttc", 14)
            except:
                font_title = ImageFont.load_default()
                font_action = font_title
                font_info = font_title
            
            avatar1_path = self.download_avatar(user1_id)
            if avatar1_path and os.path.exists(avatar1_path):
                avatar1 = Image.open(avatar1_path).resize((60, 60))
                mask = Image.new('L', (60, 60), 0)
                ImageDraw.Draw(mask).ellipse((0, 0, 60, 60), fill=255)
                img.paste(avatar1, (30, 70), mask)
            
            avatar2_path = self.download_avatar(user2_id)
            if avatar2_path and os.path.exists(avatar2_path):
                avatar2 = Image.open(avatar2_path).resize((60, 60))
                mask = Image.new('L', (60, 60), 0)
                ImageDraw.Draw(mask).ellipse((0, 0, 60, 60), fill=255)
                img.paste(avatar2, (360, 70), mask)
            
            draw.text((225, 25), f"[ {action['name']} ]", fill=(255, 200, 200), font=font_title, anchor="mm")
            
            draw.text((110, 100), user1_name[:6], fill=(200, 200, 200), font=font_info, anchor="lm")
            draw.text((340, 100), user2_name[:6], fill=(200, 200, 200), font=font_info, anchor="rm")
            
            draw.text((225, 100), "→", fill=(255, 150, 150), font=font_action, anchor="mm")
            
            color = (100, 255, 100) if intimacy > 0 else (255, 100, 100)
            sign = "+" if intimacy > 0 else ""
            draw.text((225, 145), f"亲密度 {sign}{intimacy}", fill=color, font=font_info, anchor="mm")
            
            title = self.get_title(total_intimacy)
            draw.text((225, 175), f"当前关系: {title} (亲密度{total_intimacy})", fill=(255, 180, 200), font=font_info, anchor="mm")
            
            buffer = BytesIO()
            img.save(buffer, format='PNG')
            return base64.b64encode(buffer.getvalue()).decode()
        except Exception as e:
            print(f"[TouQing] Action image error: {e}")
            return None
    
    def generate_rank_image(self, pairs_data):
        if not HAS_PIL:
            return None
        try:
            count = min(len(pairs_data), 10)
            height = 100 + count * 60
            img = Image.new('RGB', (500, height), color=(35, 25, 40))
            draw = ImageDraw.Draw(img)
            
            try:
                font_title = ImageFont.truetype("msyh.ttc", 26)
                font_rank = ImageFont.truetype("msyh.ttc", 16)
                font_info = ImageFont.truetype("msyh.ttc", 14)
            except:
                font_title = ImageFont.load_default()
                font_rank = font_title
                font_info = font_title
            
            draw.text((250, 35), "[ 偷情排行榜 ]", fill=(255, 180, 200), font=font_title, anchor="mm")
            draw.line((20, 65, 480, 65), fill=(100, 80, 100), width=1)
            
            rank_colors = [(255, 215, 0), (192, 192, 192), (205, 127, 50)]
            
            for i, (pair_key, data) in enumerate(pairs_data[:10]):
                y = 80 + i * 60
                uids = pair_key.split("_")
                
                for j, uid in enumerate(uids[:2]):
                    avatar_path = self.download_avatar(uid)
                    if avatar_path and os.path.exists(avatar_path):
                        avatar = Image.open(avatar_path).resize((40, 40))
                        mask = Image.new('L', (40, 40), 0)
                        ImageDraw.Draw(mask).ellipse((0, 0, 40, 40), fill=255)
                        x_pos = 70 + j * 50
                        img.paste(avatar, (x_pos, y + 5), mask)
                
                color = rank_colors[i] if i < 3 else (200, 200, 200)
                draw.text((30, y + 25), f"{i+1}", fill=color, font=font_rank, anchor="mm")
                
                intimacy = data.get("intimacy", 0)
                title = self.get_title(intimacy)
                draw.text((180, y + 15), title, fill=color, font=font_rank, anchor="lm")
                draw.text((180, y + 38), f"亲密度 {intimacy} | 约会 {data.get('dates', 0)}次", fill=(150, 150, 150), font=font_info, anchor="lm")
                
                draw.text((450, y + 25), f"{intimacy}", fill=(255, 150, 180), font=font_rank, anchor="rm")
            
            buffer = BytesIO()
            img.save(buffer, format='PNG')
            return base64.b64encode(buffer.getvalue()).decode()
        except Exception as e:
            print(f"[TouQing] Rank image error: {e}")
            return None
    
    def on_message(self, event):
        if event.get("message_type") != "group":
            return False
        
        raw = event.get("raw_message", "").strip()
        group_id = event.get("group_id", 0)
        user_id = event.get("user_id", 0)
        self_id = event.get("self_id", 0)
        sender = event.get("sender", {})
        nickname = sender.get("card") or sender.get("nickname") or str(user_id)
        
        import re
        at_pattern = r'\[CQ:at,qq=' + str(self_id) + r'[^\]]*\]'
        if not re.search(at_pattern, raw):
            return False
        
        clean_message = re.sub(r'\[CQ:[^\]]+\]', '', raw).strip()
        
        if clean_message in ["/偷情", "/偷情配对", "/找对象"]:
            return self.handle_match(group_id, user_id, nickname)
        
        if clean_message in ["/偷情取消", "/取消配对"]:
            return self.handle_cancel(group_id, user_id)
        
        if clean_message.startswith("/偷情动作"):
            return self.handle_action(group_id, user_id, nickname)
        
        if clean_message in ["/偷情结束", "/结束约会"]:
            return self.handle_end_date(group_id, user_id, nickname)
        
        if clean_message in ["/偷情排行", "/偷情榜"]:
            return self.handle_rank(group_id)
        
        if clean_message in ["/偷情状态", "/我的偷情"]:
            return self.handle_status(group_id, user_id, nickname)
        
        if clean_message in ["/偷情帮助", "/偷情说明"]:
            return self.handle_help(group_id)
        
        return False
    
    def handle_match(self, group_id, user_id, nickname):
        gid = str(group_id)
        uid = str(user_id)
        
        player = self.get_player(user_id)
        if player.get("in_date"):
            self.send_group_msg(group_id, f"[CQ:at,qq={user_id}] 你正在约会中！发送「偷情结束」先结束当前约会")
            return True
        
        if gid not in self.waiting_queue:
            self.waiting_queue[gid] = []
        
        for w in self.waiting_queue[gid]:
            if w["user_id"] == uid:
                self.send_group_msg(group_id, f"[CQ:at,qq={user_id}] 你已在等待队列中，请耐心等待配对...")
                return True
        
        if len(self.waiting_queue[gid]) > 0:
            partner = self.waiting_queue[gid].pop(0)
            partner_id = partner["user_id"]
            partner_name = partner["nickname"]
            
            if partner_id == uid:
                self.waiting_queue[gid].append({"user_id": uid, "nickname": nickname, "time": time.time()})
                self.send_group_msg(group_id, f"[CQ:at,qq={user_id}] 等待其他人加入配对...")
                return True
            
            scene = random.choice(self.scenes)
            
            player["in_date"] = True
            player["current_partner"] = partner_id
            player["current_scene"] = scene["name"]
            player["nickname"] = nickname
            
            partner_player = self.get_player(partner_id)
            partner_player["in_date"] = True
            partner_player["current_partner"] = uid
            partner_player["current_scene"] = scene["name"]
            partner_player["nickname"] = partner_name
            
            match = self.get_intimacy(uid, partner_id)
            match["dates"] = match.get("dates", 0) + 1
            match["last_date"] = datetime.now().isoformat()
            
            self.save_data()
            
            img_base64 = self.generate_match_image(user_id, nickname, partner_id, partner_name, scene)
            
            if img_base64:
                self.send_group_msg(group_id, f"[CQ:image,file=base64://{img_base64}]")
            else:
                self.send_group_msg(group_id, 
                    f"[配对成功]\n"
                    f"[CQ:at,qq={user_id}] + [CQ:at,qq={partner_id}]\n"
                    f"地点: {scene['name']}\n"
                    f"{scene['desc']}\n\n"
                    f"发送「偷情动作」开始互动")
        else:
            self.waiting_queue[gid].append({"user_id": uid, "nickname": nickname, "time": time.time()})
            self.send_group_msg(group_id, f"[CQ:at,qq={user_id}] 已加入配对队列，等待另一位玩家加入...")
        
        return True
    
    def handle_cancel(self, group_id, user_id):
        gid = str(group_id)
        uid = str(user_id)
        
        if gid in self.waiting_queue:
            self.waiting_queue[gid] = [w for w in self.waiting_queue[gid] if w["user_id"] != uid]
        
        self.send_group_msg(group_id, f"[CQ:at,qq={user_id}] 已取消配对")
        return True
    
    def handle_action(self, group_id, user_id, nickname):
        uid = str(user_id)
        player = self.get_player(user_id)
        
        if not player.get("in_date"):
            self.send_group_msg(group_id, f"[CQ:at,qq={user_id}] 你当前没有在约会中！发送「偷情」开始配对")
            return True
        
        partner_id = player.get("current_partner")
        if not partner_id:
            player["in_date"] = False
            self.send_group_msg(group_id, f"[CQ:at,qq={user_id}] 约会状态异常，已重置")
            return True
        
        partner_player = self.get_player(partner_id)
        partner_name = partner_player.get("nickname", str(partner_id))
        
        action = random.choice(self.actions)
        
        if random.randint(1, 100) <= 20:
            event = random.choice(self.events)
            extra_intimacy = event["intimacy"]
            event_text = f"\n[事件] {event['desc']} (亲密度{'+' if extra_intimacy >= 0 else ''}{extra_intimacy})"
        else:
            extra_intimacy = 0
            event_text = ""
        
        total_gain = action["intimacy"] + extra_intimacy
        new_intimacy = self.add_intimacy(uid, partner_id, total_gain)
        
        img_base64 = self.generate_action_image(
            user_id, nickname, partner_id, partner_name,
            action, total_gain, new_intimacy
        )
        
        if img_base64:
            msg = f"[CQ:image,file=base64://{img_base64}]"
            if event_text:
                msg += event_text
            self.send_group_msg(group_id, msg)
        else:
            title = self.get_title(new_intimacy)
            self.send_group_msg(group_id,
                f"[{action['name']}] {nickname} 对 {partner_name}\n"
                f"亲密度 +{total_gain} (当前: {new_intimacy})\n"
                f"关系: {title}"
                f"{event_text}")
        
        self.save_data()
        return True
    
    def handle_end_date(self, group_id, user_id, nickname):
        uid = str(user_id)
        player = self.get_player(user_id)
        
        if not player.get("in_date"):
            self.send_group_msg(group_id, f"[CQ:at,qq={user_id}] 你当前没有在约会中")
            return True
        
        partner_id = player.get("current_partner")
        
        player["in_date"] = False
        player["current_partner"] = None
        player["current_scene"] = None
        player["total_dates"] = player.get("total_dates", 0) + 1
        
        if partner_id:
            partner_player = self.get_player(partner_id)
            partner_player["in_date"] = False
            partner_player["current_partner"] = None
            partner_player["current_scene"] = None
            partner_player["total_dates"] = partner_player.get("total_dates", 0) + 1
            
            match = self.get_intimacy(uid, partner_id)
            intimacy = match.get("intimacy", 0)
            title = self.get_title(intimacy)
            
            self.send_group_msg(group_id,
                f"[约会结束]\n"
                f"[CQ:at,qq={user_id}] 与 [CQ:at,qq={partner_id}]\n"
                f"当前关系: {title} (亲密度{intimacy})")
        else:
            self.send_group_msg(group_id, f"[CQ:at,qq={user_id}] 约会已结束")
        
        self.save_data()
        return True
    
    def handle_rank(self, group_id):
        matches = self.data.get("matches", {})
        if not matches:
            self.send_group_msg(group_id, "暂无偷情记录")
            return True
        
        sorted_matches = sorted(matches.items(), key=lambda x: x[1].get("intimacy", 0), reverse=True)
        
        img_base64 = self.generate_rank_image(sorted_matches[:10])
        
        if img_base64:
            self.send_group_msg(group_id, f"[CQ:image,file=base64://{img_base64}]")
        else:
            msg = "[偷情排行榜]\n"
            for i, (pair_key, data) in enumerate(sorted_matches[:10]):
                uids = pair_key.split("_")
                intimacy = data.get("intimacy", 0)
                title = self.get_title(intimacy)
                msg += f"{i+1}. {title} 亲密度{intimacy}\n"
            self.send_group_msg(group_id, msg)
        
        return True
    
    def handle_status(self, group_id, user_id, nickname):
        uid = str(user_id)
        player = self.get_player(user_id)
        
        matches = self.data.get("matches", {})
        my_matches = [(k, v) for k, v in matches.items() if uid in k.split("_")]
        my_matches.sort(key=lambda x: x[1].get("intimacy", 0), reverse=True)
        
        msg = f"[{nickname} 的偷情记录]\n"
        msg += f"总约会次数: {player.get('total_dates', 0)}\n\n"
        
        if my_matches:
            msg += "亲密对象:\n"
            for pair_key, data in my_matches[:5]:
                uids = pair_key.split("_")
                partner_id = uids[0] if uids[1] == uid else uids[1]
                intimacy = data.get("intimacy", 0)
                title = self.get_title(intimacy)
                msg += f"  [CQ:at,qq={partner_id}] - {title} (亲密度{intimacy})\n"
        else:
            msg += "还没有偷情记录，发送「偷情」开始配对吧！"
        
        if player.get("in_date"):
            partner_id = player.get("current_partner")
            scene = player.get("current_scene", "未知地点")
            msg += f"\n\n当前正在 {scene} 与 [CQ:at,qq={partner_id}] 约会中"
        
        self.send_group_msg(group_id, msg)
        return True
    
    def handle_help(self, group_id):
        help_text = """[偷情游戏说明]

基础指令:
- /偷情 - 加入配对队列
- /偷情取消 - 退出配对队列
- /偷情动作 - 与对象互动(增加亲密度)
- /偷情结束 - 结束当前约会
- /偷情排行 - 查看亲密度排行
- /偷情状态 - 查看个人记录

玩法说明:
1. 发送 /偷情 加入配对队列
2. 系统随机匹配另一位玩家
3. 配对成功后进入约会场景
4. 发送 /偷情动作 进行互动
5. 互动会增加亲密度
6. 约会结束后发送 /偷情结束

关系等级:
路人 -> 昧昧对象 -> 地下情人 -> 秘密恋人 -> 灵魂伴侣 -> 命中注定"""
        
        self.send_group_msg(group_id, help_text)
        return True

register_plugin(TouQingPlugin())
