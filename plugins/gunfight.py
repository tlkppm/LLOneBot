import json
import os
import random
import time
import base64
import math
import urllib.request
from datetime import datetime, date
from io import BytesIO

try:
    from PIL import Image, ImageDraw, ImageFont
    HAS_PIL = True
except:
    HAS_PIL = False

class GunfightPlugin(LCHBotPlugin):
    def __init__(self):
        super().__init__()
        self.name = "DeltaOpsPlugin"
        self.version = "3.0.0"
        self.author = "LCHBOT"
        self.description = "三角洲行动 - 摸金/对枪/打BOSS"
        self.priority = 100
        
        self.data_file = "data/deltaops_data.json"
        self.data = {"players": {}, "raids": {}}
        
        self.weapons = [
            "AK-47", "M4A1", "AWP", "沙漠之鹰", "UZI", "MP5", 
            "SCAR-L", "M416", "98K", "喷子", "加特林", "RPG",
            "HK416", "G36C", "FAMAS", "AUG", "P90", "Vector",
            "VSS", "Mini14", "SKS", "SLR", "QBZ", "Groza",
            "MK14", "M24", "Kar98k", "Win94", "crossbow",
            "平底锅", "镰刀", "撬棍", "拳头", "板砖",
            "M249", "MG3", "DP-28", "汤姆逊", "野牛",
            "R1895", "P18C", "P92", "R45", "蝎式手枪",
            "S686", "S1897", "S12K", "DBS", "黄金沙鹰",
            "信号枪", "燃烧瓶", "手雷", "震爆弹", "烟雾弹"
        ]
        
        self.kill_messages = [
            "{winner} 用 [{weapon}] 爆头击杀了 {loser}",
            "{winner} 的 [{weapon}] 精准命中 {loser}",
            "{winner} 用 [{weapon}] 将 {loser} 送回大厅",
            "{loser} 被 {winner} 的 [{weapon}] 终结",
            "{winner} 一枪 [{weapon}] 带走了 {loser}",
        ]
        
        self.loots = [
            {"name": "黄金条", "value": 50000, "rarity": "legendary", "img": "red_1x2_jintiao_330271.png"},
            {"name": "钻石项链", "value": 30000, "rarity": "epic", "img": "gold_1x1_jinbi_57741.png"},
            {"name": "古董花瓶", "value": 20000, "rarity": "epic", "img": "gold_1x2_taoyong_90669.png"},
            {"name": "保险箱", "value": 15000, "rarity": "rare", "img": "purple_2x2_lixinji_60559.png"},
            {"name": "珠宝盒", "value": 10000, "rarity": "rare", "img": "purple_1x1_erhuan_13172.png"},
            {"name": "现金包", "value": 5000, "rarity": "common", "img": "blue_1x1_shangyewenjian_7812.png"},
            {"name": "电子设备", "value": 3000, "rarity": "common", "img": "blue_1x1_jidianqi_10459.png"},
            {"name": "军用物资", "value": 2000, "rarity": "common", "img": "blue_2x2_youqi_21361.png"},
            {"name": "医疗包", "value": 1000, "rarity": "common", "img": "purple_1x2_fuliaobao_18685.png"},
            {"name": "弹药箱", "value": 500, "rarity": "common", "img": "blue_1x2_zidanlingjian_20405.png"},
        ]
        
        self.item_img_dir = os.path.join("plugins", "deltaops_assets", "items")
        
        self.classes = [
            {
                "id": "assault", "name": "尖兵",
                "hp_bonus": 10, "attack_bonus": 15, "defense_bonus": 0,
                "skill": {"name": "闪光突袭", "desc": "提高攻击力50%持续一回合", "cooldown": 3}
            },
            {
                "id": "medic", "name": "医师",
                "hp_bonus": 0, "attack_bonus": 0, "defense_bonus": 5,
                "skill": {"name": "战地急救", "desc": "立即恢复40HP", "cooldown": 2}
            },
            {
                "id": "support", "name": "支援",
                "hp_bonus": 5, "attack_bonus": 10, "defense_bonus": 5,
                "skill": {"name": "弹药补给", "desc": "获得额外战利品", "cooldown": 3}
            },
            {
                "id": "recon", "name": "侦察",
                "hp_bonus": -10, "attack_bonus": 25, "defense_bonus": 0,
                "skill": {"name": "标记目标", "desc": "下次攻击必定暴击", "cooldown": 4}
            },
            {
                "id": "engineer", "name": "工程",
                "hp_bonus": 5, "attack_bonus": 5, "defense_bonus": 10,
                "skill": {"name": "装甲强化", "desc": "减少50%伤害持续2回合", "cooldown": 3}
            },
            {
                "id": "breacher", "name": "破障",
                "hp_bonus": 15, "attack_bonus": 10, "defense_bonus": 5,
                "skill": {"name": "爆破突入", "desc": "对敌人造成额外伤害", "cooldown": 3}
            },
        ]
        
        self.actions = [
            {"type": "heal", "name": "包扎伤口", "hp_restore": 15, "desc": "使用绷带包扎"},
            {"type": "medkit", "name": "使用医疗包", "hp_restore": 30, "desc": "使用医疗包治疗"},
            {"type": "boost", "name": "注射肾上腺素", "hp_restore": 10, "desc": "提升移动速度"},
            {"type": "armor", "name": "穿戴护甲", "defense": 20, "desc": "增加护甲值"},
        ]
        
        self.bosses = [
            {"name": "变异暴君", "hp": 500, "attack": 30, "reward": 100000, "level": 5},
            {"name": "机械守卫", "hp": 300, "attack": 25, "reward": 50000, "level": 3},
            {"name": "精英佣兵", "hp": 200, "attack": 20, "reward": 30000, "level": 2},
            {"name": "疯狂科学家", "hp": 150, "attack": 15, "reward": 20000, "level": 1},
            {"name": "丧尸首领", "hp": 100, "attack": 10, "reward": 10000, "level": 1},
        ]
        
        self.maps = [
            {"name": "废弃工厂", "danger": 1, "loot_bonus": 0},
            {"name": "地下实验室", "danger": 2, "loot_bonus": 0.2},
            {"name": "军事基地", "danger": 3, "loot_bonus": 0.5},
            {"name": "豪华别墅", "danger": 2, "loot_bonus": 0.3},
            {"name": "银行金库", "danger": 4, "loot_bonus": 1.0},
        ]
        
        self.npcs = [
            {"name": "友军特工", "type": "friendly", "help": "提供情报"},
            {"name": "军火商", "type": "trader", "help": "出售弹药"},
            {"name": "医疗兵", "type": "medic", "help": "恢复HP"},
            {"name": "侦察兵", "type": "scout", "help": "发现隐藏宝藏"},
        ]
        
        self.enemies = [
            {"name": "巡逻士兵", "hp": 50, "attack": 10, "reward": 500},
            {"name": "精英警卫", "hp": 80, "attack": 15, "reward": 1000},
            {"name": "重装机枪手", "hp": 120, "attack": 20, "reward": 2000},
            {"name": "狙击手", "hp": 60, "attack": 25, "reward": 1500},
            {"name": "爆破专家", "hp": 70, "attack": 30, "reward": 1800},
        ]
        
        self.raid_events = [
            {"type": "loot", "weight": 30, "desc": "发现战利品"},
            {"type": "enemy", "weight": 20, "desc": "遭遇敌人"},
            {"type": "npc", "weight": 10, "desc": "遇到NPC"},
            {"type": "trap", "weight": 8, "desc": "触发陷阱"},
            {"type": "treasure", "weight": 4, "desc": "发现宝藏"},
            {"type": "ambush", "weight": 5, "desc": "遭遇埋伏"},
            {"type": "rescue", "weight": 4, "desc": "发现倒地队友"},
            {"type": "heal_self", "weight": 8, "desc": "自我治疗"},
            {"type": "use_skill", "weight": 6, "desc": "使用技能"},
            {"type": "explore", "weight": 5, "desc": "探索区域"},
        ]
        
        self.equipment = [
            {"id": "vest_1", "name": "轻型防弹衣", "type": "armor", "defense": 10, "price": 5000, "rarity": "common"},
            {"id": "vest_2", "name": "重型防弹衣", "type": "armor", "defense": 25, "price": 15000, "rarity": "rare"},
            {"id": "vest_3", "name": "特种战术甲", "type": "armor", "defense": 50, "price": 50000, "rarity": "epic"},
            {"id": "helmet_1", "name": "军用头盔", "type": "helmet", "defense": 5, "price": 3000, "rarity": "common"},
            {"id": "helmet_2", "name": "防弹头盔", "type": "helmet", "defense": 15, "price": 12000, "rarity": "rare"},
            {"id": "helmet_3", "name": "夜视头盔", "type": "helmet", "defense": 30, "price": 40000, "rarity": "epic"},
            {"id": "bag_1", "name": "小型背包", "type": "bag", "capacity": 3, "price": 2000, "rarity": "common"},
            {"id": "bag_2", "name": "战术背包", "type": "bag", "capacity": 5, "price": 8000, "rarity": "rare"},
            {"id": "bag_3", "name": "军用大背包", "type": "bag", "capacity": 8, "price": 25000, "rarity": "epic"},
            {"id": "gun_1", "name": "手枪", "type": "weapon", "attack": 10, "price": 3000, "rarity": "common"},
            {"id": "gun_2", "name": "冲锋枪", "type": "weapon", "attack": 25, "price": 15000, "rarity": "rare"},
            {"id": "gun_3", "name": "突击步枪", "type": "weapon", "attack": 40, "price": 35000, "rarity": "epic"},
            {"id": "gun_4", "name": "狙击步枪", "type": "weapon", "attack": 60, "price": 80000, "rarity": "legendary"},
        ]
        
        self.missions = [
            {"id": "m1", "name": "新手任务", "desc": "完成1次撤离", "type": "extract", "target": 1, "reward": 5000},
            {"id": "m2", "name": "搜刮达人", "desc": "搜索5次战利品", "type": "loot", "target": 5, "reward": 8000},
            {"id": "m3", "name": "猎人", "desc": "击杀1个BOSS", "type": "boss", "target": 1, "reward": 20000},
            {"id": "m4", "name": "杀手", "desc": "击杀3名玩家", "type": "kill", "target": 3, "reward": 15000},
            {"id": "m5", "name": "幸存者", "desc": "成功撤离5次", "type": "extract", "target": 5, "reward": 30000},
            {"id": "m6", "name": "屠龙者", "desc": "击杀变异暴君", "type": "boss_specific", "target": "变异暴君", "reward": 100000},
            {"id": "m7", "name": "财富猎手", "desc": "累计获得10万", "type": "money", "target": 100000, "reward": 25000},
        ]
        
        self.squads = {}
        
        self.battles_2v2 = {}
        self.battle_schedules = {}
    
    def on_load(self):
        os.makedirs("data", exist_ok=True)
        os.makedirs("data/avatars", exist_ok=True)
        self.load_data()
        print(f"[{self.name}] 对枪游戏插件已加载")
    
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
                "kills": 0,
                "deaths": 0,
                "wins": 0,
                "nickname": "",
                "money": 10000,
                "raids": 0,
                "extracts": 0,
                "boss_kills": 0,
                "total_loot": 0,
                "in_raid": False,
                "raid_loot": [],
                "raid_map": "",
                "hp": 100
            }
        p = self.data["players"][uid]
        if "money" not in p: p["money"] = 10000
        if "raids" not in p: p["raids"] = 0
        if "extracts" not in p: p["extracts"] = 0
        if "boss_kills" not in p: p["boss_kills"] = 0
        if "total_loot" not in p: p["total_loot"] = 0
        if "in_raid" not in p: p["in_raid"] = False
        if "raid_loot" not in p: p["raid_loot"] = []
        if "raid_map" not in p: p["raid_map"] = ""
        if "hp" not in p: p["hp"] = 100
        if "equipped" not in p: p["equipped"] = {"armor": None, "helmet": None, "bag": None, "weapon": None}
        if "inventory" not in p: p["inventory"] = []
        if "mission" not in p: p["mission"] = None
        if "mission_progress" not in p: p["mission_progress"] = 0
        if "squad" not in p: p["squad"] = None
        if "loot_count" not in p: p["loot_count"] = 0
        if "class" not in p: p["class"] = None
        if "skill_cooldown" not in p: p["skill_cooldown"] = 0
        if "armor_value" not in p: p["armor_value"] = 0
        if "buff" not in p: p["buff"] = None
        if "buff_duration" not in p: p["buff_duration"] = 0
        return p
    
    def check_mission_progress(self, player, action_type, value):
        if not player.get("mission"):
            return
        
        mission = next((m for m in self.missions if m["name"] == player["mission"]), None)
        if not mission:
            return
        
        if mission["type"] == action_type:
            if action_type == "boss_specific":
                if value == mission["target"]:
                    player["mission_progress"] += 1
            elif action_type == "money":
                player["mission_progress"] = player.get("total_loot", 0)
            else:
                player["mission_progress"] += value
            
            if player["mission_progress"] >= mission["target"]:
                player["money"] += mission["reward"]
                player["mission"] = None
                player["mission_progress"] = 0
    
    def lose_equipment_on_death(self, player):
        equipped = player.get("equipped", {})
        lost = []
        for slot in ["armor", "helmet"]:
            if equipped.get(slot):
                eq = next((e for e in self.equipment if e["id"] == equipped[slot]), None)
                if eq and random.randint(1, 100) <= 50:
                    lost.append(eq["name"])
                    equipped[slot] = None
        player["equipped"] = equipped
        return lost
    
    def download_avatar(self, user_id):
        try:
            avatar_path = f"data/avatars/{user_id}.jpg"
            if os.path.exists(avatar_path):
                mtime = os.path.getmtime(avatar_path)
                if time.time() - mtime < 3600:
                    return avatar_path
            
            url = f"https://q1.qlogo.cn/g?b=qq&nk={user_id}&s=640"
            urllib.request.urlretrieve(url, avatar_path)
            return avatar_path
        except:
            return None
    
    def generate_kill_image(self, winner_id, winner_name, loser_id, loser_name, weapon):
        if not HAS_PIL:
            return None
        try:
            img = Image.new('RGB', (500, 200), color=(20, 20, 25))
            draw = ImageDraw.Draw(img)
            
            try:
                font_title = ImageFont.truetype("msyh.ttc", 24)
                font_name = ImageFont.truetype("msyh.ttc", 18)
                font_weapon = ImageFont.truetype("msyh.ttc", 14)
            except:
                font_title = ImageFont.load_default()
                font_name = font_title
                font_weapon = font_title
            
            winner_avatar_path = self.download_avatar(winner_id)
            if winner_avatar_path and os.path.exists(winner_avatar_path):
                avatar = Image.open(winner_avatar_path).resize((80, 80))
                mask = Image.new('L', (80, 80), 0)
                mask_draw = ImageDraw.Draw(mask)
                mask_draw.ellipse((0, 0, 80, 80), fill=255)
                img.paste(avatar, (30, 60), mask)
            
            loser_avatar_path = self.download_avatar(loser_id)
            if loser_avatar_path and os.path.exists(loser_avatar_path):
                avatar = Image.open(loser_avatar_path).resize((80, 80))
                avatar_gray = avatar.convert('L').convert('RGB')
                mask = Image.new('L', (80, 80), 0)
                mask_draw = ImageDraw.Draw(mask)
                mask_draw.ellipse((0, 0, 80, 80), fill=255)
                img.paste(avatar_gray, (390, 60), mask)
            
            draw.text((250, 20), "KILLED", fill=(255, 50, 50), font=font_title, anchor="mm")
            draw.text((130, 90), winner_name[:8], fill=(100, 255, 100), font=font_name, anchor="lm")
            draw.text((370, 90), loser_name[:8], fill=(150, 150, 150), font=font_name, anchor="rm")
            draw.text((250, 120), f"[{weapon}]", fill=(255, 200, 50), font=font_weapon, anchor="mm")
            draw.text((250, 100), ">>>", fill=(255, 100, 100), font=font_name, anchor="mm")
            
            buffer = BytesIO()
            img.save(buffer, format='PNG')
            return base64.b64encode(buffer.getvalue()).decode()
        except Exception as e:
            print(f"[Gunfight] Image error: {e}")
            return None
    
    def generate_victory_image(self, winner_id, winner_name, kills, deaths):
        if not HAS_PIL:
            return None
        try:
            img = Image.new('RGB', (400, 250), color=(20, 30, 20))
            draw = ImageDraw.Draw(img)
            
            try:
                font_title = ImageFont.truetype("msyh.ttc", 28)
                font_name = ImageFont.truetype("msyh.ttc", 20)
                font_stats = ImageFont.truetype("msyh.ttc", 16)
            except:
                font_title = ImageFont.load_default()
                font_name = font_title
                font_stats = font_title
            
            avatar_path = self.download_avatar(winner_id)
            if avatar_path and os.path.exists(avatar_path):
                avatar = Image.open(avatar_path).resize((100, 100))
                mask = Image.new('L', (100, 100), 0)
                mask_draw = ImageDraw.Draw(mask)
                mask_draw.ellipse((0, 0, 100, 100), fill=255)
                img.paste(avatar, (150, 50), mask)
            
            draw.text((200, 20), "VICTORY", fill=(255, 215, 0), font=font_title, anchor="mm")
            draw.text((200, 170), winner_name, fill=(100, 255, 100), font=font_name, anchor="mm")
            draw.text((200, 200), f"击杀: {kills}  死亡: {deaths}", fill=(200, 200, 200), font=font_stats, anchor="mm")
            kd = kills / deaths if deaths > 0 else kills
            draw.text((200, 225), f"K/D: {kd:.2f}", fill=(255, 200, 50), font=font_stats, anchor="mm")
            
            buffer = BytesIO()
            img.save(buffer, format='PNG')
            return base64.b64encode(buffer.getvalue()).decode()
        except Exception as e:
            print(f"[Gunfight] Victory image error: {e}")
            return None
    
    def generate_double_kill_image(self, p1_id, p1_name, p2_id, p2_name):
        if not HAS_PIL:
            return None
        try:
            img = Image.new('RGB', (500, 200), color=(50, 20, 20))
            draw = ImageDraw.Draw(img)
            
            try:
                font_title = ImageFont.truetype("msyh.ttc", 32)
                font_name = ImageFont.truetype("msyh.ttc", 18)
            except:
                font_title = ImageFont.load_default()
                font_name = font_title
            
            p1_avatar_path = self.download_avatar(p1_id)
            if p1_avatar_path and os.path.exists(p1_avatar_path):
                avatar = Image.open(p1_avatar_path).resize((80, 80))
                avatar_gray = avatar.convert('L').convert('RGB')
                mask = Image.new('L', (80, 80), 0)
                mask_draw = ImageDraw.Draw(mask)
                mask_draw.ellipse((0, 0, 80, 80), fill=255)
                img.paste(avatar_gray, (50, 60), mask)
            
            p2_avatar_path = self.download_avatar(p2_id)
            if p2_avatar_path and os.path.exists(p2_avatar_path):
                avatar = Image.open(p2_avatar_path).resize((80, 80))
                avatar_gray = avatar.convert('L').convert('RGB')
                mask = Image.new('L', (80, 80), 0)
                mask_draw = ImageDraw.Draw(mask)
                mask_draw.ellipse((0, 0, 80, 80), fill=255)
                img.paste(avatar_gray, (370, 60), mask)
            
            draw.text((250, 30), "DOUBLE KILL", fill=(255, 50, 50), font=font_title, anchor="mm")
            draw.text((250, 70), "同 归 于 尽", fill=(255, 150, 50), font=font_name, anchor="mm")
            draw.text((150, 100), p1_name[:8], fill=(150, 150, 150), font=font_name, anchor="lm")
            draw.text((350, 100), p2_name[:8], fill=(150, 150, 150), font=font_name, anchor="rm")
            draw.text((250, 100), "X", fill=(255, 50, 50), font=font_title, anchor="mm")
            draw.text((250, 160), "双方阵亡", fill=(200, 200, 200), font=font_name, anchor="mm")
            
            buffer = BytesIO()
            img.save(buffer, format='PNG')
            return base64.b64encode(buffer.getvalue()).decode()
        except Exception as e:
            print(f"[Gunfight] Double kill image error: {e}")
            return None
    
    def generate_rank_image(self, players_data):
        if not HAS_PIL:
            return None
        try:
            count = min(len(players_data), 10)
            height = 80 + count * 50
            img = Image.new('RGB', (450, height), color=(25, 25, 30))
            draw = ImageDraw.Draw(img)
            
            try:
                font_title = ImageFont.truetype("msyh.ttc", 24)
                font_rank = ImageFont.truetype("msyh.ttc", 18)
                font_stats = ImageFont.truetype("msyh.ttc", 14)
            except:
                font_title = ImageFont.load_default()
                font_rank = font_title
                font_stats = font_title
            
            draw.text((225, 30), "枪王排行榜", fill=(255, 215, 0), font=font_title, anchor="mm")
            draw.line((20, 55, 430, 55), fill=(100, 100, 100), width=1)
            
            rank_colors = [(255, 215, 0), (192, 192, 192), (205, 127, 50)]
            
            for i, (uid, data) in enumerate(players_data[:10]):
                y = 70 + i * 50
                
                avatar_path = self.download_avatar(uid)
                if avatar_path and os.path.exists(avatar_path):
                    avatar = Image.open(avatar_path).resize((40, 40))
                    mask = Image.new('L', (40, 40), 0)
                    mask_draw = ImageDraw.Draw(mask)
                    mask_draw.ellipse((0, 0, 40, 40), fill=255)
                    img.paste(avatar, (60, y), mask)
                
                color = rank_colors[i] if i < 3 else (200, 200, 200)
                draw.text((25, y + 20), f"{i+1}", fill=color, font=font_rank, anchor="mm")
                
                name = data.get("nickname") or uid[-4:] + "****"
                draw.text((110, y + 12), name[:10], fill=color, font=font_rank, anchor="lm")
                
                kills = data.get("kills", 0)
                deaths = data.get("deaths", 0)
                kd = kills / deaths if deaths > 0 else kills
                draw.text((110, y + 32), f"{kills}杀 | K/D: {kd:.2f}", fill=(150, 150, 150), font=font_stats, anchor="lm")
                
                draw.text((400, y + 20), f"{kills}", fill=(100, 255, 100), font=font_rank, anchor="rm")
            
            buffer = BytesIO()
            img.save(buffer, format='PNG')
            return base64.b64encode(buffer.getvalue()).decode()
        except Exception as e:
            print(f"[Gunfight] Rank image error: {e}")
            return None
    
    def generate_raid_entry_image(self, user_id, nickname, map_name):
        if not HAS_PIL:
            return None
        try:
            img = Image.new('RGB', (450, 200), color=(15, 25, 35))
            draw = ImageDraw.Draw(img)
            
            try:
                font_title = ImageFont.truetype("msyh.ttc", 28)
                font_name = ImageFont.truetype("msyh.ttc", 18)
                font_map = ImageFont.truetype("msyh.ttc", 16)
            except:
                font_title = ImageFont.load_default()
                font_name = font_title
                font_map = font_title
            
            avatar_path = self.download_avatar(user_id)
            if avatar_path and os.path.exists(avatar_path):
                avatar = Image.open(avatar_path).resize((80, 80))
                mask = Image.new('L', (80, 80), 0)
                ImageDraw.Draw(mask).ellipse((0, 0, 80, 80), fill=255)
                img.paste(avatar, (30, 60), mask)
            
            draw.text((225, 25), "DEPLOYING", fill=(50, 200, 255), font=font_title, anchor="mm")
            draw.text((225, 60), f">>> {map_name} <<<", fill=(255, 200, 50), font=font_map, anchor="mm")
            draw.text((130, 100), nickname[:10], fill=(200, 200, 200), font=font_name, anchor="lm")
            draw.text((225, 140), "正在部署中...", fill=(150, 150, 150), font=font_map, anchor="mm")
            draw.text((225, 170), "祝你好运, 干员!", fill=(100, 255, 100), font=font_map, anchor="mm")
            
            buffer = BytesIO()
            img.save(buffer, format='PNG')
            return base64.b64encode(buffer.getvalue()).decode()
        except:
            return None
    
    def generate_loot_image(self, user_id, nickname, loot_item, total_value):
        if not HAS_PIL:
            return None
        try:
            rarity_colors = {"legendary": (255, 165, 0), "epic": (163, 53, 238), "rare": (30, 144, 255), "common": (150, 150, 150)}
            color = rarity_colors.get(loot_item["rarity"], (150, 150, 150))
            
            img = Image.new('RGB', (400, 150), color=(25, 30, 25))
            draw = ImageDraw.Draw(img)
            
            try:
                font_title = ImageFont.truetype("msyh.ttc", 20)
                font_item = ImageFont.truetype("msyh.ttc", 24)
                font_value = ImageFont.truetype("msyh.ttc", 16)
            except:
                font_title = ImageFont.load_default()
                font_item = font_title
                font_value = font_title
            
            avatar_path = self.download_avatar(user_id)
            if avatar_path and os.path.exists(avatar_path):
                avatar = Image.open(avatar_path).resize((60, 60))
                mask = Image.new('L', (60, 60), 0)
                ImageDraw.Draw(mask).ellipse((0, 0, 60, 60), fill=255)
                img.paste(avatar, (20, 45), mask)
            
            draw.text((200, 20), "LOOT FOUND", fill=(100, 255, 100), font=font_title, anchor="mm")
            draw.text((200, 55), loot_item["name"], fill=color, font=font_item, anchor="mm")
            draw.text((200, 90), f"+${loot_item['value']:,}", fill=(255, 215, 0), font=font_value, anchor="mm")
            draw.text((200, 120), f"背包总价值: ${total_value:,}", fill=(150, 150, 150), font=font_value, anchor="mm")
            
            buffer = BytesIO()
            img.save(buffer, format='PNG')
            return base64.b64encode(buffer.getvalue()).decode()
        except:
            return None
    
    def generate_boss_image(self, user_id, nickname, boss, victory, damage_dealt, reward=0):
        if not HAS_PIL:
            return None
        try:
            bg_color = (20, 35, 20) if victory else (40, 20, 20)
            img = Image.new('RGB', (450, 200), color=bg_color)
            draw = ImageDraw.Draw(img)
            
            try:
                font_title = ImageFont.truetype("msyh.ttc", 26)
                font_boss = ImageFont.truetype("msyh.ttc", 20)
                font_info = ImageFont.truetype("msyh.ttc", 16)
            except:
                font_title = ImageFont.load_default()
                font_boss = font_title
                font_info = font_title
            
            avatar_path = self.download_avatar(user_id)
            if avatar_path and os.path.exists(avatar_path):
                avatar = Image.open(avatar_path).resize((70, 70))
                mask = Image.new('L', (70, 70), 0)
                ImageDraw.Draw(mask).ellipse((0, 0, 70, 70), fill=255)
                img.paste(avatar, (30, 65), mask)
            
            if victory:
                draw.text((225, 25), "BOSS DEFEATED", fill=(255, 215, 0), font=font_title, anchor="mm")
                draw.text((225, 60), boss["name"], fill=(255, 100, 100), font=font_boss, anchor="mm")
                draw.text((225, 100), f"造成伤害: {damage_dealt}", fill=(200, 200, 200), font=font_info, anchor="mm")
                draw.text((225, 130), f"奖励: ${reward:,}", fill=(100, 255, 100), font=font_info, anchor="mm")
                draw.text((225, 160), nickname[:10], fill=(200, 200, 200), font=font_info, anchor="mm")
            else:
                draw.text((225, 25), "BOSS FIGHT FAILED", fill=(255, 50, 50), font=font_title, anchor="mm")
                draw.text((225, 60), boss["name"], fill=(255, 100, 100), font=font_boss, anchor="mm")
                draw.text((225, 100), f"造成伤害: {damage_dealt}", fill=(200, 200, 200), font=font_info, anchor="mm")
                draw.text((225, 130), "你被击倒了!", fill=(255, 100, 100), font=font_info, anchor="mm")
                draw.text((225, 160), "失去所有战利品", fill=(150, 150, 150), font=font_info, anchor="mm")
            
            buffer = BytesIO()
            img.save(buffer, format='PNG')
            return base64.b64encode(buffer.getvalue()).decode()
        except:
            return None
    
    def generate_extract_image(self, user_id, nickname, success, loot_value, map_name):
        if not HAS_PIL:
            return None
        try:
            bg_color = (20, 40, 20) if success else (50, 20, 20)
            img = Image.new('RGB', (450, 220), color=bg_color)
            draw = ImageDraw.Draw(img)
            
            try:
                font_title = ImageFont.truetype("msyh.ttc", 32)
                font_info = ImageFont.truetype("msyh.ttc", 18)
                font_value = ImageFont.truetype("msyh.ttc", 22)
            except:
                font_title = ImageFont.load_default()
                font_info = font_title
                font_value = font_title
            
            avatar_path = self.download_avatar(user_id)
            if avatar_path and os.path.exists(avatar_path):
                avatar = Image.open(avatar_path).resize((90, 90))
                if not success:
                    avatar = avatar.convert('L').convert('RGB')
                mask = Image.new('L', (90, 90), 0)
                ImageDraw.Draw(mask).ellipse((0, 0, 90, 90), fill=255)
                img.paste(avatar, (180, 50), mask)
            
            if success:
                draw.text((225, 25), "EXTRACTED", fill=(100, 255, 100), font=font_title, anchor="mm")
                draw.text((225, 155), nickname[:12], fill=(200, 200, 200), font=font_info, anchor="mm")
                draw.text((225, 180), f"从 {map_name} 成功撤离", fill=(150, 150, 150), font=font_info, anchor="mm")
                draw.text((225, 205), f"获得 ${loot_value:,}", fill=(255, 215, 0), font=font_value, anchor="mm")
            else:
                draw.text((225, 25), "MIA", fill=(255, 50, 50), font=font_title, anchor="mm")
                draw.text((225, 155), nickname[:12], fill=(150, 150, 150), font=font_info, anchor="mm")
                draw.text((225, 180), f"在 {map_name} 阵亡", fill=(255, 100, 100), font=font_info, anchor="mm")
                draw.text((225, 205), "失去所有战利品", fill=(150, 150, 150), font=font_info, anchor="mm")
            
            buffer = BytesIO()
            img.save(buffer, format='PNG')
            return base64.b64encode(buffer.getvalue()).decode()
        except:
            return None
    
    def generate_combat_image(self, user_id, nickname, enemy, victory, damage, reward, enemy_damage=0):
        if not HAS_PIL:
            return None
        try:
            bg = (20, 35, 20) if victory else (45, 25, 25)
            img = Image.new('RGB', (420, 160), color=bg)
            draw = ImageDraw.Draw(img)
            
            try:
                font_title = ImageFont.truetype("msyh.ttc", 22)
                font_info = ImageFont.truetype("msyh.ttc", 14)
            except:
                font_title = ImageFont.load_default()
                font_info = font_title
            
            avatar_path = self.download_avatar(user_id)
            if avatar_path and os.path.exists(avatar_path):
                avatar = Image.open(avatar_path).resize((50, 50))
                mask = Image.new('L', (50, 50), 0)
                ImageDraw.Draw(mask).ellipse((0, 0, 50, 50), fill=255)
                img.paste(avatar, (20, 55), mask)
            
            if victory:
                draw.text((210, 20), f"击败 {enemy['name']}", fill=(100, 255, 100), font=font_title, anchor="mm")
                draw.text((210, 55), f"造成 {damage} 伤害", fill=(200, 200, 200), font=font_info, anchor="mm")
                draw.text((210, 80), f"+${reward:,}", fill=(255, 215, 0), font=font_title, anchor="mm")
            else:
                draw.text((210, 20), f"遭遇 {enemy['name']}", fill=(255, 100, 100), font=font_title, anchor="mm")
                draw.text((210, 55), f"造成 {damage} 伤害 | 受到 {enemy_damage} 伤害", fill=(200, 200, 200), font=font_info, anchor="mm")
                draw.text((210, 80), "战斗失败", fill=(255, 80, 80), font=font_info, anchor="mm")
            
            draw.text((210, 120), nickname[:10], fill=(150, 150, 150), font=font_info, anchor="mm")
            
            buffer = BytesIO()
            img.save(buffer, format='PNG')
            return base64.b64encode(buffer.getvalue()).decode()
        except:
            return None
    
    def generate_npc_image(self, user_id, nickname, npc):
        if not HAS_PIL:
            return None
        try:
            img = Image.new('RGB', (380, 130), color=(25, 35, 45))
            draw = ImageDraw.Draw(img)
            
            try:
                font_title = ImageFont.truetype("msyh.ttc", 20)
                font_info = ImageFont.truetype("msyh.ttc", 14)
            except:
                font_title = ImageFont.load_default()
                font_info = font_title
            
            draw.text((190, 25), f"遇到 {npc['name']}", fill=(100, 200, 255), font=font_title, anchor="mm")
            draw.text((190, 60), npc["help"], fill=(255, 200, 100), font=font_info, anchor="mm")
            draw.text((190, 90), nickname[:10], fill=(150, 150, 150), font=font_info, anchor="mm")
            
            buffer = BytesIO()
            img.save(buffer, format='PNG')
            return base64.b64encode(buffer.getvalue()).decode()
        except:
            return None
    
    def generate_trap_image(self, user_id, nickname, damage, hp_left):
        if not HAS_PIL:
            return None
        try:
            img = Image.new('RGB', (380, 130), color=(50, 30, 20))
            draw = ImageDraw.Draw(img)
            
            try:
                font_title = ImageFont.truetype("msyh.ttc", 22)
                font_info = ImageFont.truetype("msyh.ttc", 14)
            except:
                font_title = ImageFont.load_default()
                font_info = font_title
            
            draw.text((190, 25), "触发陷阱!", fill=(255, 100, 50), font=font_title, anchor="mm")
            draw.text((190, 60), f"-{damage} HP", fill=(255, 80, 80), font=font_title, anchor="mm")
            hp_color = (100, 255, 100) if hp_left > 30 else (255, 100, 100)
            draw.text((190, 95), f"剩余 HP: {max(0, hp_left)}", fill=hp_color, font=font_info, anchor="mm")
            
            buffer = BytesIO()
            img.save(buffer, format='PNG')
            return base64.b64encode(buffer.getvalue()).decode()
        except:
            return None
    
    def generate_treasure_image(self, user_id, nickname, value):
        if not HAS_PIL:
            return None
        try:
            img = Image.new('RGB', (400, 140), color=(40, 35, 15))
            draw = ImageDraw.Draw(img)
            
            try:
                font_title = ImageFont.truetype("msyh.ttc", 24)
                font_value = ImageFont.truetype("msyh.ttc", 20)
                font_info = ImageFont.truetype("msyh.ttc", 14)
            except:
                font_title = ImageFont.load_default()
                font_value = font_title
                font_info = font_title
            
            draw.text((200, 25), "发现隐藏宝藏!", fill=(255, 215, 0), font=font_title, anchor="mm")
            draw.text((200, 65), f"+${value:,}", fill=(255, 200, 50), font=font_value, anchor="mm")
            draw.text((200, 105), nickname[:10], fill=(150, 150, 150), font=font_info, anchor="mm")
            
            buffer = BytesIO()
            img.save(buffer, format='PNG')
            return base64.b64encode(buffer.getvalue()).decode()
        except:
            return None
    
    def generate_ambush_image(self, user_id, nickname, enemy_count, kills, damage, hp_left):
        if not HAS_PIL:
            return None
        try:
            img = Image.new('RGB', (420, 150), color=(40, 25, 30))
            draw = ImageDraw.Draw(img)
            
            try:
                font_title = ImageFont.truetype("msyh.ttc", 22)
                font_info = ImageFont.truetype("msyh.ttc", 14)
            except:
                font_title = ImageFont.load_default()
                font_info = font_title
            
            draw.text((210, 20), f"遭遇埋伏! ({enemy_count}名敌人)", fill=(255, 100, 80), font=font_title, anchor="mm")
            draw.text((210, 55), f"击杀 {kills} 人 | 受到 {damage} 伤害", fill=(200, 200, 200), font=font_info, anchor="mm")
            hp_color = (100, 255, 100) if hp_left > 30 else (255, 100, 100)
            draw.text((210, 85), f"剩余 HP: {max(0, hp_left)}", fill=hp_color, font=font_info, anchor="mm")
            draw.text((210, 115), nickname[:10], fill=(150, 150, 150), font=font_info, anchor="mm")
            
            buffer = BytesIO()
            img.save(buffer, format='PNG')
            return base64.b64encode(buffer.getvalue()).decode()
        except:
            return None
    
    def generate_rescue_image(self, user_id, nickname, teammate_name, reward):
        if not HAS_PIL:
            return None
        try:
            img = Image.new('RGB', (400, 140), color=(20, 40, 35))
            draw = ImageDraw.Draw(img)
            
            try:
                font_title = ImageFont.truetype("msyh.ttc", 20)
                font_info = ImageFont.truetype("msyh.ttc", 14)
            except:
                font_title = ImageFont.load_default()
                font_info = font_title
            
            draw.text((200, 25), "救援队友!", fill=(100, 255, 150), font=font_title, anchor="mm")
            draw.text((200, 55), f"成功救起 {teammate_name[:8]}", fill=(200, 255, 200), font=font_info, anchor="mm")
            draw.text((200, 85), f"+${reward:,}", fill=(255, 215, 0), font=font_title, anchor="mm")
            draw.text((200, 115), nickname[:10], fill=(150, 150, 150), font=font_info, anchor="mm")
            
            buffer = BytesIO()
            img.save(buffer, format='PNG')
            return base64.b64encode(buffer.getvalue()).decode()
        except:
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
        
        msg = re.sub(r'\[CQ:[^\]]+\]', '', raw).strip()
        
        if msg in ["/三角洲", "/摸金帮助", "/帮助"]:
            return self.cmd_help(event)
        
        if msg in ["/对枪战绩", "/我的战绩", "/战绩"]:
            return self.cmd_stats(event, nickname)
        
        if msg in ["/对枪排行", "/枪王排行", "/排行榜", "/排行"]:
            return self.cmd_rank(event)
        
        if msg in ["/入场", "/进场", "/开始摸金"]:
            return self.cmd_raid_enter(event, user_id, nickname, group_id)
        
        if msg in ["/背包", "/查看背包", "/状态"]:
            return self.cmd_raid_bag(event, user_id, nickname)
        
        if msg in ["/商店", "/装备商店"]:
            return self.cmd_shop(event, user_id, nickname)
        
        if msg.startswith("/购买 ") or msg.startswith("/购买"):
            item_name = msg.replace("/购买", "").strip()
            return self.cmd_buy(event, user_id, nickname, item_name)
        
        if msg in ["/装备", "/我的装备"]:
            return self.cmd_equipment(event, user_id, nickname)
        
        if msg in ["/任务", "/任务列表"]:
            return self.cmd_missions(event, user_id, nickname)
        
        if msg.startswith("/接取任务 ") or msg.startswith("/接任务 "):
            mission_name = msg.replace("/接取任务", "").replace("/接任务", "").strip()
            return self.cmd_accept_mission(event, user_id, nickname, mission_name)
        
        if msg in ["/创建小队", "/组队"]:
            return self.cmd_create_squad(event, user_id, nickname, group_id)
        
        if msg in ["/加入小队", "/入队"]:
            return self.cmd_join_squad(event, user_id, nickname, group_id)
        
        if msg in ["/小队", "/队伍信息"]:
            return self.cmd_squad_info(event, user_id, nickname, group_id)
        
        if msg in ["/解散小队", "/退出小队"]:
            return self.cmd_leave_squad(event, user_id, nickname, group_id)
        
        if msg in ["/取消2v2", "/退出2v2"]:
            return self.cmd_cancel_2v2(event, user_id, nickname, group_id)
        
        if msg in ["/2v2", "/创建2v2", "/组队对战"]:
            return self.cmd_create_2v2(event, user_id, nickname, group_id)
        
        if msg in ["/加入2v2", "/参战"]:
            return self.cmd_join_2v2(event, user_id, nickname, group_id)
        
        if msg in ["/开始2v2", "/开战"]:
            return self.cmd_start_2v2(event, user_id, nickname, group_id)
        
        if msg.startswith("/定时2v2 "):
            time_str = msg.replace("/定时2v2 ", "").strip()
            return self.cmd_schedule_2v2(event, user_id, nickname, group_id, time_str)
        
        if msg in ["/2v2状态", "/对战状态"]:
            return self.cmd_2v2_status(event, user_id, nickname, group_id)
        
        target_match = re.search(r'/开枪\s*\[CQ:at,qq=(\d+)', raw)
        if not target_match:
            target_match = re.search(r'/对枪\s*\[CQ:at,qq=(\d+)', raw)
        if not target_match:
            target_match = re.search(r'/袭击\s*\[CQ:at,qq=(\d+)', raw)
        if target_match:
            target_id = int(target_match.group(1))
            return self.cmd_shoot(event, user_id, target_id, nickname, raw)
        
        return False
    
    def cmd_help(self, event):
        self.reply(event, """[三角洲行动 v3.0 全自动版]
================
[摸金] /入场 - 自动完成整局摸金
[对枪] /开枪@人 /袭击@人
[装备] /商店 /购买xxx /装备
[任务] /任务 /接任务xxx
[组队] /组队 /入队 /小队
[2v2] /2v2 /加入2v2 /开战 /定时2v2
[战绩] /战绩 /排行榜""")
        return True
    
    def cmd_stats(self, event, nickname):
        user_id = event.get("user_id", 0)
        player = self.get_player(user_id)
        player["nickname"] = nickname
        self.save_data()
        
        kills = player["kills"]
        deaths = player["deaths"]
        kd = kills / deaths if deaths > 0 else kills
        
        self.reply(event, f"""[{nickname} 的战绩]
================
击杀: {kills}
死亡: {deaths}
K/D: {kd:.2f}
胜场: {player['wins']}""")
        return True
    
    def cmd_rank(self, event):
        players = self.data["players"]
        if not players:
            self.reply(event, "[提示] 暂无战绩数据")
            return True
        
        sorted_p = sorted(players.items(), key=lambda x: x[1]["kills"], reverse=True)[:10]
        
        rank_img = self.generate_rank_image(sorted_p)
        if rank_img:
            self.reply(event, f"[CQ:image,file=base64://{rank_img}]")
        else:
            msg = "[枪王排行榜]\n================"
            for i, (uid, d) in enumerate(sorted_p):
                name = d.get("nickname") or uid[-4:] + "****"
                kd = d["kills"] / d["deaths"] if d["deaths"] > 0 else d["kills"]
                msg += f"\n{i+1}. {name[:6]} - {d['kills']}杀 K/D:{kd:.1f}"
            self.reply(event, msg)
        return True
    
    def cmd_shoot(self, event, attacker_id, target_id, attacker_name, raw):
        if attacker_id == target_id:
            self.reply(event, "[错误] 不能对自己开枪")
            return True
        
        import re
        target_name_match = re.search(r'\[CQ:at,qq=' + str(target_id) + r',name=([^\]]+)\]', raw)
        if target_name_match:
            target_name = target_name_match.group(1)
        else:
            target_name = str(target_id)[-4:] + "****"
        
        attacker = self.get_player(attacker_id)
        target = self.get_player(target_id)
        attacker["nickname"] = attacker_name
        target["nickname"] = target_name
        
        weapon = random.choice(self.weapons)
        
        roll = random.randint(1, 100)
        
        if roll <= 45:
            winner_id, winner_name = attacker_id, attacker_name
            loser_id, loser_name = target_id, target_name
            attacker["kills"] += 1
            attacker["wins"] += 1
            target["deaths"] += 1
            self.check_mission_progress(attacker, "kill", 1)
        elif roll <= 90:
            winner_id, winner_name = target_id, target_name
            loser_id, loser_name = attacker_id, attacker_name
            target["kills"] += 1
            target["wins"] += 1
            attacker["deaths"] += 1
            self.check_mission_progress(target, "kill", 1)
        else:
            attacker["deaths"] += 1
            target["deaths"] += 1
            self.save_data()
            
            double_img = self.generate_double_kill_image(attacker_id, attacker_name, target_id, target_name)
            if double_img:
                self.reply(event, f"[CQ:image,file=base64://{double_img}]")
            else:
                self.reply(event, f"[双杀] {attacker_name} 和 {target_name} 同归于尽!")
            return True
        
        self.save_data()
        
        kill_msg = random.choice(self.kill_messages).format(
            winner=winner_name, loser=loser_name, weapon=weapon
        )
        
        self.reply(event, f"[对枪结果]\n{kill_msg}")
        
        kill_img = self.generate_kill_image(winner_id, winner_name, loser_id, loser_name, weapon)
        if kill_img:
            self.reply(event, f"[CQ:image,file=base64://{kill_img}]")
        
        winner = self.get_player(winner_id)
        if winner["kills"] % 5 == 0 and winner["kills"] > 0:
            victory_img = self.generate_victory_image(
                winner_id, winner_name, winner["kills"], winner["deaths"]
            )
            if victory_img:
                self.reply(event, f"[CQ:image,file=base64://{victory_img}]")
        
        return True
    
    def cmd_raid_enter(self, event, user_id, nickname, group_id):
        player = self.get_player(user_id)
        player["nickname"] = nickname
        player["user_id"] = user_id
        
        if player["in_raid"]:
            self.reply(event, f"[提示] 你已经在 {player['raid_map']} 中")
            return True
        
        squad_id = f"{group_id}_squad"
        squad_members = []
        
        if squad_id in self.squads:
            squad = self.squads[squad_id]
            if squad["leader"] == user_id:
                for member in squad["members"]:
                    member_player = self.get_player(member["user_id"])
                    if not member_player["in_raid"]:
                        member_player["nickname"] = member["nickname"]
                        member_player["user_id"] = member["user_id"]
                        squad_members.append({"user_id": member["user_id"], "nickname": member["nickname"], "player": member_player})
                
                if len(squad_members) > 1:
                    self.reply(event, f"[小队出击] {len(squad_members)}名队员一起入场!")
        
        if not squad_members:
            squad_members = [{"user_id": user_id, "nickname": nickname, "player": player}]
        
        self.run_auto_raid(event, user_id, nickname, player, squad_members)
        return True
    
    def run_auto_raid(self, event, user_id, nickname, player, squad_members=None):
        if squad_members is None:
            squad_members = [{"user_id": user_id, "nickname": nickname, "player": player}]
        
        map_data = random.choice(self.maps)
        map_name = map_data["name"]
        danger = map_data["danger"]
        loot_bonus = map_data["loot_bonus"]
        
        if not player.get("class"):
            player["class"] = random.choice(self.classes)["id"]
        
        player_class = next((c for c in self.classes if c["id"] == player["class"]), self.classes[0])
        base_hp = 100 + player_class["hp_bonus"]
        
        for member in squad_members:
            p = member["player"]
            if not p.get("class"):
                p["class"] = random.choice(self.classes)["id"]
            member_class = next((c for c in self.classes if c["id"] == p["class"]), self.classes[0])
            p["in_raid"] = True
            p["raid_map"] = map_name
            p["raid_loot"] = []
            p["hp"] = 100 + member_class["hp_bonus"]
            p["raids"] += 1
            p["squad"] = f"raid_{user_id}"
            p["armor_value"] = 0
            p["skill_cooldown"] = 0
            p["buff"] = None
            p["buff_duration"] = 0
        
        player["in_raid"] = True
        player["raid_map"] = map_name
        player["raid_loot"] = []
        player["hp"] = base_hp
        player["raids"] += 1
        player["armor_value"] = 0
        player["skill_cooldown"] = 0
        player["buff"] = None
        player["buff_duration"] = 0
        
        raid_events = []
        raid_events.append({
            "type": "select_class", 
            "class": player_class,
            "hp": player["hp"]
        })
        raid_events.append({"type": "deploy", "map": map_name, "danger": danger, "squad_count": len(squad_members), "hp": player["hp"]})
        
        rounds = random.randint(6, 12 + danger * 2)
        
        for round_num in range(1, rounds + 1):
            if player["hp"] <= 0:
                break
            
            event_result = self.simulate_raid_event(player, map_data, round_num, squad_members)
            raid_events.append(event_result)
            
            if event_result.get("dead"):
                break
        
        boss_fought = False
        if player["hp"] > 0:
            boss_chance = 20 + danger * 10
            if random.randint(1, 100) <= boss_chance:
                boss_result = self.simulate_boss_fight(player)
                raid_events.append(boss_result)
                boss_fought = True
        
        loot_value = sum(l["value"] for l in player["raid_loot"])
        
        if player["hp"] > 0:
            extract_chance = 70 + (player["hp"] // 5)
            success = random.randint(1, 100) <= extract_chance
            
            if success:
                player["money"] += loot_value
                player["total_loot"] += loot_value
                player["extracts"] += 1
                player["wins"] += 1
                self.check_mission_progress(player, "extract", 1)
                raid_events.append({"type": "extract", "success": True, "value": loot_value, "map": map_name})
            else:
                self.lose_equipment_on_death(player)
                player["deaths"] += 1
                raid_events.append({"type": "extract", "success": False, "value": loot_value, "map": map_name})
        else:
            player["deaths"] += 1
            self.lose_equipment_on_death(player)
            raid_events.append({"type": "death", "value": loot_value, "map": map_name})
        
        for member in squad_members:
            p = member["player"]
            if player["hp"] > 0 and raid_events[-1].get("success", False):
                member_loot = int(loot_value * 0.3) if member["user_id"] != user_id else 0
                p["money"] += member_loot
                p["total_loot"] += member_loot
                p["extracts"] += 1
            p["in_raid"] = False
            p["raid_loot"] = []
            p["squad"] = None
        
        player["in_raid"] = False
        player["raid_loot"] = []
        self.save_data()
        
        gif = self.generate_raid_gif(user_id, nickname, raid_events, player, squad_members)
        if gif:
            self.reply(event, f"[CQ:image,file=base64://{gif}]")
        else:
            final_event = raid_events[-1]
            if final_event["type"] == "extract" and final_event["success"]:
                self.reply(event, f"[撤离成功] {nickname} 从 {map_name} 撤离, 获得 ${loot_value:,}")
            else:
                self.reply(event, f"[任务失败] {nickname} 在 {map_name} 阵亡")
    
    def simulate_raid_event(self, player, map_data, round_num, squad_members=None):
        weights = [e["weight"] for e in self.raid_events]
        event_type = random.choices(self.raid_events, weights=weights, k=1)[0]["type"]
        
        result = {"round": round_num, "type": event_type, "dead": False, "hp": player["hp"]}
        
        if event_type == "loot":
            loot = self.get_random_loot(map_data["loot_bonus"])
            player["raid_loot"].append(loot)
            player["loot_count"] += 1
            self.check_mission_progress(player, "loot", 1)
            result["loot"] = loot
            result["total"] = sum(l["value"] for l in player["raid_loot"])
            
        elif event_type == "enemy":
            enemy = random.choice(self.enemies)
            player_attack = random.randint(30, 60)
            enemy_attack = enemy["attack"] * random.randint(1, 3)
            
            if player_attack >= enemy["hp"]:
                player["raid_loot"].append({"name": f"击杀{enemy['name']}", "value": enemy["reward"], "rarity": "common"})
                result["victory"] = True
                result["reward"] = enemy["reward"]
            else:
                player["hp"] -= enemy_attack
                result["victory"] = False
                result["damage"] = enemy_attack
                if player["hp"] <= 0:
                    result["dead"] = True
            result["enemy"] = enemy
            result["hp"] = player["hp"]
            
        elif event_type == "npc":
            npc = random.choice(self.npcs)
            result["npc"] = npc
            
            if npc["type"] == "medic":
                heal = random.randint(20, 40)
                player["hp"] = min(100, player["hp"] + heal)
                result["heal"] = heal
            elif npc["type"] == "scout":
                bonus_loot = self.get_random_loot(0.5)
                player["raid_loot"].append(bonus_loot)
                result["loot"] = bonus_loot
            elif npc["type"] == "trader":
                player["raid_loot"].append({"name": "弹药补给", "value": 500, "rarity": "common"})
            result["hp"] = player["hp"]
            
        elif event_type == "trap":
            damage = int(random.randint(10, 30) * map_data["danger"])
            player["hp"] -= damage
            result["damage"] = damage
            result["hp"] = player["hp"]
            if player["hp"] <= 0:
                result["dead"] = True
            
        elif event_type == "treasure":
            treasure_value = random.randint(10000, 50000)
            player["raid_loot"].append({"name": "隐藏宝藏", "value": treasure_value, "rarity": "legendary"})
            result["value"] = treasure_value
            
        elif event_type == "ambush":
            enemies_count = random.randint(2, 4)
            total_damage = sum(random.choice(self.enemies)["attack"] for _ in range(enemies_count))
            player["hp"] -= total_damage
            kills = random.randint(1, enemies_count)
            reward = kills * 800
            player["raid_loot"].append({"name": f"埋伏战({kills}杀)", "value": reward, "rarity": "rare"})
            result["enemy_count"] = enemies_count
            result["kills"] = kills
            result["damage"] = total_damage
            result["reward"] = reward
            result["hp"] = player["hp"]
            if player["hp"] <= 0:
                result["dead"] = True
            
        elif event_type == "rescue":
            teammates = []
            if squad_members and len(squad_members) > 1:
                teammates = [m for m in squad_members if m["user_id"] != player.get("user_id")]
            
            if teammates:
                teammate = random.choice(teammates)
                reward = 3000
                player["raid_loot"].append({"name": f"救援奖励", "value": reward, "rarity": "rare"})
                result["teammate"] = teammate["nickname"]
                result["reward"] = reward
                heal = random.randint(15, 30)
                player["hp"] = min(100, player["hp"] + heal)
                result["heal"] = heal
                result["hp"] = player["hp"]
            else:
                result["type"] = "loot"
                loot = self.get_random_loot(0)
                player["raid_loot"].append(loot)
                result["loot"] = loot
        
        elif event_type == "heal_self":
            player_class = next((c for c in self.classes if c["id"] == player.get("class")), self.classes[0])
            action = random.choice(self.actions[:3])
            heal_amount = action["hp_restore"]
            
            if player_class["id"] == "medic":
                heal_amount = int(heal_amount * 1.5)
            
            max_hp = 100 + player_class["hp_bonus"]
            old_hp = player["hp"]
            player["hp"] = min(max_hp, player["hp"] + heal_amount)
            actual_heal = player["hp"] - old_hp
            
            result["action"] = action
            result["heal"] = actual_heal
            result["hp"] = player["hp"]
            result["class"] = player_class
        
        elif event_type == "use_skill":
            player_class = next((c for c in self.classes if c["id"] == player.get("class")), self.classes[0])
            skill = player_class["skill"]
            
            if player.get("skill_cooldown", 0) > 0:
                result["type"] = "loot"
                loot = self.get_random_loot(0)
                player["raid_loot"].append(loot)
                result["loot"] = loot
            else:
                result["skill"] = skill
                result["class"] = player_class
                
                if player_class["id"] == "medic":
                    heal = 40
                    max_hp = 100 + player_class["hp_bonus"]
                    player["hp"] = min(max_hp, player["hp"] + heal)
                    result["heal"] = heal
                elif player_class["id"] == "assault":
                    player["buff"] = "attack_boost"
                    player["buff_duration"] = 1
                    result["buff"] = "攻击力+50%"
                elif player_class["id"] == "recon":
                    player["buff"] = "crit"
                    player["buff_duration"] = 1
                    result["buff"] = "下次必定暴击"
                elif player_class["id"] == "engineer":
                    player["buff"] = "shield"
                    player["buff_duration"] = 2
                    result["buff"] = "伤害减半"
                elif player_class["id"] == "support":
                    bonus_loot = self.get_random_loot(0.3)
                    player["raid_loot"].append(bonus_loot)
                    result["loot"] = bonus_loot
                    result["buff"] = "获得额外物资"
                elif player_class["id"] == "breacher":
                    player["buff"] = "damage_boost"
                    player["buff_duration"] = 2
                    result["buff"] = "爆破伤害+100%"
                
                player["skill_cooldown"] = skill["cooldown"]
                result["hp"] = player["hp"]
        
        elif event_type == "explore":
            explore_results = [
                {"name": "发现补给箱", "type": "supply", "value": random.randint(500, 2000)},
                {"name": "找到隐藏通道", "type": "shortcut", "bonus": "安全"},
                {"name": "发现敌方情报", "type": "intel", "value": random.randint(1000, 3000)},
                {"name": "发现弹药补给", "type": "ammo", "value": 800},
            ]
            explore = random.choice(explore_results)
            result["explore"] = explore
            
            if explore["type"] in ["supply", "intel", "ammo"]:
                value = explore.get("value", 500)
                player["raid_loot"].append({"name": explore["name"], "value": value, "rarity": "common"})
                result["value"] = value
            result["hp"] = player["hp"]
        
        if player.get("skill_cooldown", 0) > 0:
            player["skill_cooldown"] -= 1
        
        if player.get("buff_duration", 0) > 0:
            player["buff_duration"] -= 1
            if player["buff_duration"] <= 0:
                player["buff"] = None
        
        return result
    
    def simulate_boss_fight(self, player):
        boss = random.choice(self.bosses)
        player_attack = random.randint(20, 50)
        damage_dealt = player_attack * random.randint(3, 8)
        
        result = {"type": "boss", "boss": boss, "damage": damage_dealt, "dead": False}
        
        if damage_dealt >= boss["hp"]:
            reward = boss["reward"]
            player["raid_loot"].append({"name": f"BOSS战利品({boss['name']})", "value": reward, "rarity": "legendary"})
            player["boss_kills"] += 1
            self.check_mission_progress(player, "boss", 1)
            self.check_mission_progress(player, "boss_specific", boss["name"])
            result["victory"] = True
            result["reward"] = reward
        else:
            boss_damage = boss["attack"] * random.randint(2, 5)
            player["hp"] -= boss_damage
            result["victory"] = False
            result["boss_damage"] = boss_damage
            if player["hp"] <= 0:
                result["dead"] = True
        
        result["hp"] = player["hp"]
        return result
    
    def generate_raid_gif(self, user_id, nickname, events, player, squad_members=None):
        if not HAS_PIL:
            return None
        try:
            frames = []
            width, height = 520, 450
            squad_count = len(squad_members) if squad_members else 1
            
            rarity_colors = {
                "legendary": (255, 165, 0),
                "epic": (163, 53, 238),
                "rare": (30, 144, 255),
                "common": (150, 150, 150)
            }
            
            rarity_bg = {
                "legendary": (60, 45, 10),
                "epic": (40, 20, 60),
                "rare": (15, 35, 60),
                "common": (35, 35, 35)
            }
            
            try:
                font_title = ImageFont.truetype("msyh.ttc", 26)
                font_event = ImageFont.truetype("msyh.ttc", 16)
                font_small = ImageFont.truetype("msyh.ttc", 12)
                font_big = ImageFont.truetype("msyh.ttc", 32)
            except:
                font_title = ImageFont.load_default()
                font_event = font_title
                font_small = font_title
                font_big = font_title
            
            avatar_path = self.download_avatar(user_id)
            avatar_img = None
            avatar_mask = None
            if avatar_path and os.path.exists(avatar_path):
                avatar_img = Image.open(avatar_path).resize((60, 60))
                avatar_mask = Image.new('L', (60, 60), 0)
                ImageDraw.Draw(avatar_mask).ellipse((0, 0, 60, 60), fill=255)
            
            def draw_base_frame(draw, img, current_hp, map_name, loot_value, event_text="", event_color=(200, 200, 200)):
                if avatar_img:
                    img.paste(avatar_img, (15, 12), avatar_mask)
                draw.text((85, 18), nickname[:10], fill=(220, 220, 220), font=font_event)
                
                hp_color = (100, 255, 100) if current_hp > 50 else (255, 200, 50) if current_hp > 25 else (255, 80, 80)
                draw.text((85, 40), f"HP: {max(0, current_hp)}", fill=hp_color, font=font_small)
                
                if squad_count > 1:
                    draw.text((150, 40), f"小队: {squad_count}人", fill=(100, 200, 255), font=font_small)
                
                draw.text((width - 120, 25), map_name[:8], fill=(150, 150, 150), font=font_small)
                
                hp_bar_x, hp_bar_y = 85, 58
                hp_bar_w, hp_bar_h = 150, 8
                draw.rectangle([hp_bar_x, hp_bar_y, hp_bar_x + hp_bar_w, hp_bar_y + hp_bar_h], fill=(50, 50, 50))
                hp_fill = int(hp_bar_w * max(0, current_hp) / 100)
                draw.rectangle([hp_bar_x, hp_bar_y, hp_bar_x + hp_fill, hp_bar_y + hp_bar_h], fill=hp_color)
                
                draw.line([(0, 75), (width, 75)], fill=(60, 60, 60), width=2)
                
                draw.line([(0, height - 55), (width, height - 55)], fill=(60, 60, 60), width=2)
                draw.text((20, height - 45), f"战利品: ${loot_value:,}", fill=(255, 215, 0), font=font_event)
            
            displayed_events = []
            current_loot_value = 0
            map_name = events[0].get("map", "未知区域") if events else "未知区域"
            
            for evt in events:
                if evt["type"] == "loot":
                    current_loot_value += evt.get("loot", {}).get("value", 0)
                elif evt["type"] == "enemy" and evt.get("victory"):
                    current_loot_value += evt.get("reward", 0)
                elif evt["type"] == "treasure":
                    current_loot_value += evt.get("value", 0)
                elif evt["type"] == "rescue":
                    current_loot_value += evt.get("reward", 0)
                elif evt["type"] == "ambush":
                    current_loot_value += evt.get("reward", 0)
                elif evt["type"] == "boss" and evt.get("victory"):
                    current_loot_value += evt.get("reward", 0)
                evt["_loot_so_far"] = current_loot_value
            
            for event_idx, evt in enumerate(events):
                displayed_events.append(evt)
                current_hp = evt.get("hp", 100)
                loot_so_far = evt.get("_loot_so_far", 0)
                
                if evt["type"] == "select_class":
                    player_class = evt.get("class", {})
                    class_colors = {
                        "assault": (255, 100, 50),
                        "medic": (100, 255, 150),
                        "recon": (150, 100, 255),
                        "engineer": (255, 200, 50),
                        "support": (100, 200, 255),
                        "breacher": (255, 150, 100)
                    }
                    class_color = class_colors.get(player_class.get("id"), (200, 200, 200))
                    
                    for frame in range(6):
                        img = Image.new('RGB', (width, height), color=(20, 25, 35))
                        draw = ImageDraw.Draw(img)
                        draw_base_frame(draw, img, current_hp, "选择角色", 0)
                        
                        draw.text((width // 2, 120), "选择角色", fill=(255, 255, 255), font=font_title, anchor="mm")
                        
                        box_scale = 0.8 + frame * 0.04
                        box_w, box_h = int(200 * box_scale), int(120 * box_scale)
                        box_x = (width - box_w) // 2
                        box_y = 160
                        
                        draw.rectangle([box_x, box_y, box_x + box_w, box_y + box_h], fill=(40, 45, 55), outline=class_color, width=3)
                        
                        draw.text((width // 2, box_y + 30), player_class.get("name", "未知"), fill=class_color, font=font_big, anchor="mm")
                        draw.text((width // 2, box_y + 70), f"技能: {player_class.get('skill', {}).get('name', '无')}", fill=(180, 180, 180), font=font_event, anchor="mm")
                        draw.text((width // 2, box_y + 95), player_class.get('skill', {}).get('desc', ''), fill=(120, 120, 120), font=font_small, anchor="mm")
                        
                        frames.append(img.copy())
                
                elif evt["type"] == "deploy":
                    for scan_frame in range(8):
                        img = Image.new('RGB', (width, height), color=(15, 20, 25))
                        draw = ImageDraw.Draw(img)
                        
                        scan_y = 80 + (scan_frame * 40)
                        draw.rectangle([0, scan_y, width, scan_y + 20], fill=(30, 80, 30, 100))
                        
                        for line_y in range(80, height - 60, 30):
                            alpha = 255 if abs(line_y - scan_y) < 50 else 80
                            line_color = (0, int(100 * alpha / 255), 0)
                            draw.line([(20, line_y), (width - 20, line_y)], fill=line_color, width=1)
                        
                        draw_base_frame(draw, img, 100, map_name, 0)
                        
                        title_y = 120 + (scan_frame % 3)
                        draw.text((width // 2, title_y), "正在部署...", fill=(50, 255, 50), font=font_title, anchor="mm")
                        draw.text((width // 2, 160), f"目标: {map_name}", fill=(100, 200, 100), font=font_event, anchor="mm")
                        draw.text((width // 2, 190), f"危险等级: {'★' * evt.get('danger', 1)}", fill=(255, 200, 50), font=font_event, anchor="mm")
                        
                        if squad_count > 1:
                            draw.text((width // 2, 220), f"小队成员: {squad_count}人", fill=(100, 200, 255), font=font_event, anchor="mm")
                        
                        frames.append(img.copy())
                    
                    for _ in range(3):
                        img = Image.new('RGB', (width, height), color=(25, 30, 35))
                        draw = ImageDraw.Draw(img)
                        draw_base_frame(draw, img, 100, map_name, 0)
                        draw.text((width // 2, 150), "部署完成!", fill=(100, 255, 100), font=font_big, anchor="mm")
                        draw.text((width // 2, 200), "开始搜索...", fill=(150, 150, 150), font=font_event, anchor="mm")
                        frames.append(img.copy())
                
                elif evt["type"] == "loot":
                    loot = evt.get("loot", {})
                    rarity = loot.get("rarity", "common")
                    
                    item_img = None
                    item_img_name = loot.get("img")
                    if item_img_name:
                        item_path = os.path.join(self.item_img_dir, item_img_name)
                        if os.path.exists(item_path):
                            try:
                                item_img = Image.open(item_path).convert("RGBA")
                                item_img = item_img.resize((80, 80), Image.LANCZOS)
                            except:
                                item_img = None
                    
                    for search_frame in range(8):
                        img = Image.new('RGB', (width, height), color=(25, 30, 35))
                        draw = ImageDraw.Draw(img)
                        draw_base_frame(draw, img, current_hp, map_name, loot_so_far - loot.get("value", 0))
                        
                        self._draw_event_log(draw, displayed_events[:-1], rarity_colors, font_event, width)
                        
                        center_x, center_y = width // 2, 270
                        
                        shadow_box = Image.new('RGBA', (120, 120), (0, 0, 0, 0))
                        shadow_draw = ImageDraw.Draw(shadow_box)
                        shadow_draw.rectangle([0, 0, 120, 120], fill=(40, 40, 40, 200))
                        for line_y in range(0, 120, 8):
                            shadow_draw.line([(0, line_y), (120, line_y)], fill=(60, 60, 60, 150), width=1)
                        for line_x in range(0, 120, 8):
                            shadow_draw.line([(line_x, 0), (line_x, 120)], fill=(60, 60, 60, 150), width=1)
                        
                        img.paste(shadow_box, (center_x - 60, center_y - 60), shadow_box)
                        draw = ImageDraw.Draw(img)
                        
                        radius = 45
                        angle = search_frame * 45
                        angle_rad = math.radians(angle)
                        dot_x = center_x + int(radius * math.cos(angle_rad))
                        dot_y = center_y + int(radius * math.sin(angle_rad))
                        
                        for i in range(8):
                            a = math.radians(angle + i * 45)
                            x1 = center_x + int(radius * math.cos(a))
                            y1 = center_y + int(radius * math.sin(a))
                            size = 6 if i == 0 else 3
                            alpha = 255 - i * 25
                            color = (100, 255, 100) if i == 0 else (50, int(150 - i * 15), 50)
                            draw.ellipse([x1 - size, y1 - size, x1 + size, y1 + size], fill=color)
                        
                        draw.text((center_x, center_y + 80), "搜索中...", fill=(150, 150, 150), font=font_small, anchor="mm")
                        frames.append(img.copy())
                    
                    for reveal_frame in range(6):
                        img = Image.new('RGB', (width, height), color=(25, 30, 35))
                        draw = ImageDraw.Draw(img)
                        draw_base_frame(draw, img, current_hp, map_name, loot_so_far)
                        
                        self._draw_event_log(draw, displayed_events, rarity_colors, font_event, width)
                        
                        bg = rarity_bg.get(rarity, (35, 35, 35))
                        border_color = rarity_colors.get(rarity, (150, 150, 150))
                        
                        scale = 1.4 - (reveal_frame * 0.08)
                        box_w, box_h = int(180 * scale), int(140 * scale)
                        box_x = (width - box_w) // 2
                        box_y = 220
                        
                        glow_size = 4 if reveal_frame < 2 else 2
                        draw.rectangle([box_x - glow_size, box_y - glow_size, box_x + box_w + glow_size, box_y + box_h + glow_size], fill=border_color)
                        draw.rectangle([box_x, box_y, box_x + box_w, box_y + box_h], fill=bg)
                        
                        if item_img and reveal_frame >= 1:
                            img_scale = min(1.0, 0.5 + reveal_frame * 0.15)
                            scaled_size = int(80 * img_scale)
                            scaled_item = item_img.resize((scaled_size, scaled_size), Image.LANCZOS)
                            paste_x = box_x + (box_w - scaled_size) // 2
                            paste_y = box_y + 10
                            img.paste(scaled_item, (paste_x, paste_y), scaled_item)
                            draw = ImageDraw.Draw(img)
                        
                        text_y = box_y + box_h - 45 if item_img else box_y + 30
                        draw.text((width // 2, text_y), loot.get("name", "物品"), fill=border_color, font=font_event, anchor="mm")
                        draw.text((width // 2, text_y + 22), f"+${loot.get('value', 0):,}", fill=(255, 215, 0), font=font_small, anchor="mm")
                        
                        frames.append(img.copy())
                
                elif evt["type"] == "enemy":
                    enemy = evt.get("enemy", {})
                    victory = evt.get("victory", False)
                    
                    for battle_frame in range(5):
                        img = Image.new('RGB', (width, height), color=(30, 25, 25) if not victory else (25, 30, 25))
                        draw = ImageDraw.Draw(img)
                        draw_base_frame(draw, img, current_hp, map_name, loot_so_far)
                        
                        self._draw_event_log(draw, displayed_events[:-1], rarity_colors, font_event, width)
                        
                        shake = (battle_frame % 2) * 3
                        draw.text((width // 2 + shake, 250), f"遭遇 {enemy.get('name', '敌人')}!", fill=(255, 100, 100), font=font_title, anchor="mm")
                        
                        if battle_frame >= 3:
                            if victory:
                                draw.text((width // 2, 290), "击败!", fill=(100, 255, 100), font=font_event, anchor="mm")
                            else:
                                draw.text((width // 2, 290), f"-{evt.get('damage', 0)} HP", fill=(255, 80, 80), font=font_event, anchor="mm")
                        
                        frames.append(img.copy())
                    
                    for _ in range(2):
                        img = Image.new('RGB', (width, height), color=(25, 30, 35))
                        draw = ImageDraw.Draw(img)
                        draw_base_frame(draw, img, current_hp, map_name, loot_so_far)
                        self._draw_event_log(draw, displayed_events, rarity_colors, font_event, width)
                        frames.append(img.copy())
                
                elif evt["type"] == "boss":
                    boss = evt.get("boss", {})
                    victory = evt.get("victory", False)
                    
                    for intro_frame in range(4):
                        img = Image.new('RGB', (width, height), color=(40, 20, 20))
                        draw = ImageDraw.Draw(img)
                        draw_base_frame(draw, img, current_hp + evt.get("boss_damage", 0) if not victory else current_hp, map_name, loot_so_far if victory else loot_so_far - evt.get("reward", 0))
                        
                        self._draw_event_log(draw, displayed_events[:-1], rarity_colors, font_event, width)
                        
                        flash = intro_frame % 2
                        color = (255, 50, 50) if flash else (200, 50, 50)
                        draw.text((width // 2, 200), "! BOSS !", fill=color, font=font_big, anchor="mm")
                        draw.text((width // 2, 250), boss.get("name", "BOSS"), fill=(255, 200, 50), font=font_title, anchor="mm")
                        
                        frames.append(img.copy())
                    
                    for battle_frame in range(6):
                        img = Image.new('RGB', (width, height), color=(50, 25, 25))
                        draw = ImageDraw.Draw(img)
                        draw_base_frame(draw, img, current_hp + evt.get("boss_damage", 0) if not victory else current_hp, map_name, loot_so_far if victory else loot_so_far - evt.get("reward", 0))
                        
                        shake = (battle_frame % 2) * 5
                        draw.text((width // 2 + shake, 230), "激战中...", fill=(255, 150, 50), font=font_title, anchor="mm")
                        
                        progress = (battle_frame + 1) / 6
                        bar_w = int(200 * progress)
                        draw.rectangle([160, 280, 160 + bar_w, 295], fill=(255, 100, 50))
                        draw.rectangle([160, 280, 360, 295], outline=(100, 100, 100), width=1)
                        
                        frames.append(img.copy())
                    
                    for result_frame in range(4):
                        img = Image.new('RGB', (width, height), color=(25, 40, 25) if victory else (40, 25, 25))
                        draw = ImageDraw.Draw(img)
                        draw_base_frame(draw, img, current_hp, map_name, loot_so_far)
                        
                        self._draw_event_log(draw, displayed_events, rarity_colors, font_event, width)
                        
                        if victory:
                            draw.text((width // 2, 240), "BOSS击败!", fill=(255, 215, 0), font=font_big, anchor="mm")
                            draw.text((width // 2, 290), f"+${evt.get('reward', 0):,}", fill=(100, 255, 100), font=font_title, anchor="mm")
                        else:
                            draw.text((width // 2, 240), "战斗失败", fill=(255, 80, 80), font=font_big, anchor="mm")
                            draw.text((width // 2, 290), f"-{evt.get('boss_damage', 0)} HP", fill=(255, 100, 100), font=font_title, anchor="mm")
                        
                        frames.append(img.copy())
                
                elif evt["type"] == "extract":
                    success = evt.get("success", False)
                    
                    for countdown_frame in range(6):
                        img = Image.new('RGB', (width, height), color=(25, 35, 45))
                        draw = ImageDraw.Draw(img)
                        draw_base_frame(draw, img, current_hp, map_name, loot_so_far)
                        
                        self._draw_event_log(draw, displayed_events[:-1], rarity_colors, font_event, width)
                        
                        draw.text((width // 2, 200), "撤离中...", fill=(100, 200, 255), font=font_title, anchor="mm")
                        
                        progress = (countdown_frame + 1) / 6
                        bar_w = int(250 * progress)
                        draw.rectangle([135, 250, 135 + bar_w, 270], fill=(50, 150, 200))
                        draw.rectangle([135, 250, 385, 270], outline=(100, 100, 100), width=2)
                        draw.text((width // 2, 260), f"{int(progress * 100)}%", fill=(255, 255, 255), font=font_small, anchor="mm")
                        
                        frames.append(img.copy())
                    
                    for result_frame in range(5):
                        bg_color = (25, 45, 25) if success else (45, 25, 25)
                        img = Image.new('RGB', (width, height), color=bg_color)
                        draw = ImageDraw.Draw(img)
                        draw_base_frame(draw, img, current_hp, map_name, loot_so_far if success else 0)
                        
                        self._draw_event_log(draw, displayed_events, rarity_colors, font_event, width)
                        
                        if success:
                            draw.text((width // 2, 200), "撤离成功!", fill=(100, 255, 100), font=font_big, anchor="mm")
                            draw.text((width // 2, 260), f"获得 ${loot_so_far:,}", fill=(255, 215, 0), font=font_title, anchor="mm")
                        else:
                            draw.text((width // 2, 200), "撤离失败!", fill=(255, 80, 80), font=font_big, anchor="mm")
                            draw.text((width // 2, 260), "损失所有战利品", fill=(255, 100, 100), font=font_event, anchor="mm")
                        
                        frames.append(img.copy())
                
                elif evt["type"] == "death":
                    for death_frame in range(6):
                        alpha = 255 - (death_frame * 30)
                        img = Image.new('RGB', (width, height), color=(50, 20, 20))
                        draw = ImageDraw.Draw(img)
                        draw_base_frame(draw, img, 0, map_name, 0)
                        
                        self._draw_event_log(draw, displayed_events, rarity_colors, font_event, width)
                        
                        draw.text((width // 2, 200), "阵亡", fill=(255, 50, 50), font=font_big, anchor="mm")
                        draw.text((width // 2, 260), "损失所有战利品和装备", fill=(200, 100, 100), font=font_event, anchor="mm")
                        
                        frames.append(img.copy())
                
                elif evt["type"] == "heal_self":
                    action = evt.get("action", {})
                    heal = evt.get("heal", 0)
                    
                    for heal_frame in range(6):
                        img = Image.new('RGB', (width, height), color=(25, 35, 30))
                        draw = ImageDraw.Draw(img)
                        draw_base_frame(draw, img, current_hp, map_name, loot_so_far)
                        
                        self._draw_event_log(draw, displayed_events[:-1], rarity_colors, font_event, width)
                        
                        draw.ellipse([width//2 - 40, 220, width//2 + 40, 300], fill=(30, 50, 40), outline=(100, 200, 100), width=2)
                        
                        if heal_frame < 3:
                            draw.text((width // 2, 250), action.get("name", "治疗"), fill=(100, 255, 150), font=font_event, anchor="mm")
                            draw.text((width // 2, 280), "治疗中...", fill=(150, 150, 150), font=font_small, anchor="mm")
                        else:
                            draw.text((width // 2, 250), f"+{heal} HP", fill=(100, 255, 100), font=font_title, anchor="mm")
                            draw.text((width // 2, 290), action.get("desc", ""), fill=(150, 200, 150), font=font_small, anchor="mm")
                        
                        frames.append(img.copy())
                
                elif evt["type"] == "use_skill":
                    skill = evt.get("skill", {})
                    player_class = evt.get("class", {})
                    buff = evt.get("buff", "")
                    heal = evt.get("heal", 0)
                    skill_loot = evt.get("loot")
                    
                    class_colors = {
                        "assault": (255, 100, 50),
                        "medic": (100, 255, 150),
                        "recon": (150, 100, 255),
                        "engineer": (255, 200, 50),
                        "support": (100, 200, 255),
                        "breacher": (255, 150, 100)
                    }
                    skill_color = class_colors.get(player_class.get("id"), (200, 200, 200))
                    
                    for skill_frame in range(8):
                        img = Image.new('RGB', (width, height), color=(30, 25, 40))
                        draw = ImageDraw.Draw(img)
                        draw_base_frame(draw, img, current_hp, map_name, loot_so_far)
                        
                        self._draw_event_log(draw, displayed_events[:-1], rarity_colors, font_event, width)
                        
                        glow = skill_frame % 4
                        draw.ellipse([width//2 - 50 - glow*2, 210 - glow*2, width//2 + 50 + glow*2, 310 + glow*2], 
                                    outline=skill_color, width=2)
                        
                        draw.text((width // 2, 230), skill.get("name", "技能"), fill=skill_color, font=font_title, anchor="mm")
                        
                        if heal > 0:
                            draw.text((width // 2, 270), f"+{heal} HP", fill=(100, 255, 100), font=font_event, anchor="mm")
                        elif buff:
                            draw.text((width // 2, 270), buff, fill=(255, 200, 100), font=font_event, anchor="mm")
                        
                        draw.text((width // 2, 300), player_class.get("name", ""), fill=(150, 150, 150), font=font_small, anchor="mm")
                        
                        frames.append(img.copy())
                
                elif evt["type"] == "explore":
                    explore = evt.get("explore", {})
                    value = evt.get("value", 0)
                    
                    for explore_frame in range(5):
                        img = Image.new('RGB', (width, height), color=(25, 30, 35))
                        draw = ImageDraw.Draw(img)
                        draw_base_frame(draw, img, current_hp, map_name, loot_so_far)
                        
                        self._draw_event_log(draw, displayed_events[:-1], rarity_colors, font_event, width)
                        
                        draw.text((width // 2, 230), "探索区域", fill=(100, 200, 255), font=font_title, anchor="mm")
                        draw.text((width // 2, 270), explore.get("name", "发现物资"), fill=(200, 200, 100), font=font_event, anchor="mm")
                        
                        if value > 0:
                            draw.text((width // 2, 300), f"+${value:,}", fill=(255, 215, 0), font=font_small, anchor="mm")
                        
                        frames.append(img.copy())
                
                else:
                    frames_count = 4 if evt["type"] in ["trap", "ambush", "treasure"] else 3
                    for _ in range(frames_count):
                        img = Image.new('RGB', (width, height), color=(25, 30, 35))
                        draw = ImageDraw.Draw(img)
                        draw_base_frame(draw, img, current_hp, map_name, loot_so_far)
                        self._draw_event_log(draw, displayed_events, rarity_colors, font_event, width)
                        frames.append(img.copy())
            
            for _ in range(8):
                frames.append(frames[-1].copy())
            
            buffer = BytesIO()
            frames[0].save(
                buffer,
                format='GIF',
                save_all=True,
                append_images=frames[1:],
                duration=150,
                loop=0
            )
            return base64.b64encode(buffer.getvalue()).decode()
        except Exception as e:
            print(f"[DeltaOps] GIF error: {e}")
            import traceback
            traceback.print_exc()
            return None
    
    def _draw_event_log(self, draw, events, rarity_colors, font, width):
        y_offset = 85
        max_display = 5
        start_idx = max(0, len(events) - max_display)
        
        for e in events[start_idx:]:
            if e["type"] == "deploy":
                color = (50, 200, 255)
                text = f">>> 部署至 {e.get('map', '?')}"
            elif e["type"] == "loot":
                loot = e.get("loot", {})
                color = rarity_colors.get(loot.get("rarity", "common"), (150, 150, 150))
                text = f"[搜刮] {loot.get('name', '?')} +${loot.get('value', 0):,}"
            elif e["type"] == "enemy":
                enemy = e.get("enemy", {})
                if e.get("victory"):
                    color = (100, 255, 100)
                    text = f"[战斗] 击败 {enemy.get('name', '?')}"
                else:
                    color = (255, 100, 100)
                    text = f"[战斗] 被击伤 -{e.get('damage', 0)}HP"
            elif e["type"] == "npc":
                npc = e.get("npc", {})
                color = (100, 200, 255)
                text = f"[NPC] {npc.get('name', '?')}"
            elif e["type"] == "trap":
                color = (255, 100, 50)
                text = f"[陷阱] -{e.get('damage', 0)}HP"
            elif e["type"] == "treasure":
                color = (255, 215, 0)
                text = f"[宝藏] +${e.get('value', 0):,}"
            elif e["type"] == "ambush":
                color = (255, 80, 80)
                text = f"[埋伏] {e.get('kills', 0)}杀 -{e.get('damage', 0)}HP"
            elif e["type"] == "rescue":
                color = (100, 255, 150)
                text = f"[救援] {e.get('teammate', '队友')}"
            elif e["type"] == "boss":
                boss = e.get("boss", {})
                if e.get("victory"):
                    color = (255, 215, 0)
                    text = f"[BOSS] 击败 {boss.get('name', '?')}"
                else:
                    color = (255, 50, 50)
                    text = f"[BOSS] 战败"
            elif e["type"] == "extract":
                if e.get("success"):
                    color = (100, 255, 100)
                    text = f">>> 撤离成功"
                else:
                    color = (255, 100, 100)
                    text = f">>> 撤离失败"
            elif e["type"] == "death":
                color = (255, 50, 50)
                text = f">>> 阵亡"
            elif e["type"] == "select_class":
                player_class = e.get("class", {})
                color = (200, 200, 255)
                text = f">>> 选择职业: {player_class.get('name', '?')}"
            elif e["type"] == "heal_self":
                color = (100, 255, 150)
                text = f"[治疗] +{e.get('heal', 0)}HP"
            elif e["type"] == "use_skill":
                skill = e.get("skill", {})
                color = (255, 200, 100)
                text = f"[技能] {skill.get('name', '?')}"
            elif e["type"] == "explore":
                explore = e.get("explore", {})
                color = (100, 200, 255)
                text = f"[探索] {explore.get('name', '?')}"
            else:
                color = (150, 150, 150)
                text = f"[事件]"
            
            draw.text((20, y_offset), text[:30], fill=color, font=font)
            y_offset += 24
    
    def get_random_loot(self, bonus=0):
        roll = random.randint(1, 100)
        bonus_roll = int(bonus * 20)
        roll = max(1, roll - bonus_roll)
        
        if roll <= 5:
            loot = self.loots[0]
        elif roll <= 15:
            loot = random.choice(self.loots[1:3])
        elif roll <= 35:
            loot = random.choice(self.loots[3:5])
        else:
            loot = random.choice(self.loots[5:])
        return loot.copy()
    
    def cmd_raid_bag(self, event, user_id, nickname):
        player = self.get_player(user_id)
        player["nickname"] = nickname
        
        if not player["in_raid"]:
            self.reply(event, f"[{nickname} 状态]\n余额: ${player['money']:,}\n总收益: ${player['total_loot']:,}\n撤离次数: {player['extracts']}")
            return True
        
        loot_list = player["raid_loot"]
        total = sum(l["value"] for l in loot_list)
        
        msg = f"[{nickname} 的背包]\n地图: {player['raid_map']}\nHP: {player['hp']}\n================\n"
        if loot_list:
            for l in loot_list[-5:]:
                msg += f"- {l['name']} (${l['value']:,})\n"
            if len(loot_list) > 5:
                msg += f"...共 {len(loot_list)} 件物品\n"
        else:
            msg += "背包空空如也\n"
        msg += f"================\n总价值: ${total:,}"
        
        self.reply(event, msg)
        return True
    
    def generate_shop_image(self, player_money):
        if not HAS_PIL:
            return None
        try:
            height = 80 + len(self.equipment) * 35
            img = Image.new('RGB', (500, height), color=(30, 30, 35))
            draw = ImageDraw.Draw(img)
            
            try:
                font_title = ImageFont.truetype("msyh.ttc", 22)
                font_item = ImageFont.truetype("msyh.ttc", 14)
            except:
                font_title = ImageFont.load_default()
                font_item = font_title
            
            rarity_colors = {"legendary": (255, 165, 0), "epic": (163, 53, 238), "rare": (30, 144, 255), "common": (150, 150, 150)}
            
            draw.text((250, 25), "装备商店", fill=(255, 215, 0), font=font_title, anchor="mm")
            draw.text((250, 50), f"你的余额: ${player_money:,}", fill=(100, 255, 100), font=font_item, anchor="mm")
            draw.line((20, 65, 480, 65), fill=(80, 80, 80), width=1)
            
            for i, eq in enumerate(self.equipment):
                y = 75 + i * 35
                color = rarity_colors.get(eq["rarity"], (150, 150, 150))
                
                type_names = {"armor": "护甲", "helmet": "头盔", "bag": "背包", "weapon": "武器"}
                type_name = type_names.get(eq["type"], "")
                
                draw.text((30, y), f"[{type_name}]", fill=(100, 100, 100), font=font_item, anchor="lm")
                draw.text((90, y), eq["name"], fill=color, font=font_item, anchor="lm")
                
                if eq["type"] == "bag":
                    draw.text((280, y), f"容量+{eq['capacity']}", fill=(200, 200, 200), font=font_item, anchor="lm")
                elif eq["type"] == "weapon":
                    draw.text((280, y), f"攻击+{eq['attack']}", fill=(255, 100, 100), font=font_item, anchor="lm")
                else:
                    draw.text((280, y), f"防御+{eq['defense']}", fill=(100, 200, 255), font=font_item, anchor="lm")
                
                price_color = (100, 255, 100) if player_money >= eq["price"] else (255, 100, 100)
                draw.text((420, y), f"${eq['price']:,}", fill=price_color, font=font_item, anchor="lm")
            
            buffer = BytesIO()
            img.save(buffer, format='PNG')
            return base64.b64encode(buffer.getvalue()).decode()
        except:
            return None
    
    def generate_mission_image(self, player, missions):
        if not HAS_PIL:
            return None
        try:
            height = 100 + len(missions) * 50
            img = Image.new('RGB', (450, height), color=(25, 30, 40))
            draw = ImageDraw.Draw(img)
            
            try:
                font_title = ImageFont.truetype("msyh.ttc", 22)
                font_item = ImageFont.truetype("msyh.ttc", 14)
                font_small = ImageFont.truetype("msyh.ttc", 12)
            except:
                font_title = ImageFont.load_default()
                font_item = font_title
                font_small = font_title
            
            draw.text((225, 25), "任务列表", fill=(255, 200, 50), font=font_title, anchor="mm")
            
            current = player.get("mission")
            if current:
                draw.text((225, 50), f"当前任务: {current}", fill=(100, 255, 100), font=font_item, anchor="mm")
                draw.text((225, 70), f"进度: {player.get('mission_progress', 0)}", fill=(200, 200, 200), font=font_small, anchor="mm")
            else:
                draw.text((225, 55), "未接取任务", fill=(150, 150, 150), font=font_item, anchor="mm")
            
            draw.line((20, 85, 430, 85), fill=(80, 80, 80), width=1)
            
            for i, m in enumerate(missions):
                y = 100 + i * 50
                draw.text((30, y), m["name"], fill=(255, 200, 100), font=font_item, anchor="lm")
                draw.text((30, y + 20), m["desc"], fill=(150, 150, 150), font=font_small, anchor="lm")
                draw.text((380, y + 10), f"${m['reward']:,}", fill=(100, 255, 100), font=font_item, anchor="rm")
            
            buffer = BytesIO()
            img.save(buffer, format='PNG')
            return base64.b64encode(buffer.getvalue()).decode()
        except:
            return None
    
    def generate_equipment_image(self, user_id, nickname, player):
        if not HAS_PIL:
            return None
        try:
            img = Image.new('RGB', (400, 280), color=(30, 35, 40))
            draw = ImageDraw.Draw(img)
            
            try:
                font_title = ImageFont.truetype("msyh.ttc", 20)
                font_item = ImageFont.truetype("msyh.ttc", 14)
            except:
                font_title = ImageFont.load_default()
                font_item = font_title
            
            rarity_colors = {"legendary": (255, 165, 0), "epic": (163, 53, 238), "rare": (30, 144, 255), "common": (150, 150, 150)}
            
            avatar_path = self.download_avatar(user_id)
            if avatar_path and os.path.exists(avatar_path):
                avatar = Image.open(avatar_path).resize((60, 60))
                mask = Image.new('L', (60, 60), 0)
                ImageDraw.Draw(mask).ellipse((0, 0, 60, 60), fill=255)
                img.paste(avatar, (20, 20), mask)
            
            draw.text((100, 35), nickname[:12], fill=(255, 255, 255), font=font_title, anchor="lm")
            draw.text((100, 60), f"余额: ${player['money']:,}", fill=(100, 255, 100), font=font_item, anchor="lm")
            
            draw.line((20, 95, 380, 95), fill=(80, 80, 80), width=1)
            
            equipped = player.get("equipped", {})
            slots = [("weapon", "武器", 110), ("armor", "护甲", 150), ("helmet", "头盔", 190), ("bag", "背包", 230)]
            
            for slot, name, y in slots:
                draw.text((30, y), f"[{name}]", fill=(100, 100, 100), font=font_item, anchor="lm")
                eq_id = equipped.get(slot)
                if eq_id:
                    eq = next((e for e in self.equipment if e["id"] == eq_id), None)
                    if eq:
                        color = rarity_colors.get(eq["rarity"], (150, 150, 150))
                        draw.text((100, y), eq["name"], fill=color, font=font_item, anchor="lm")
                    else:
                        draw.text((100, y), "无", fill=(100, 100, 100), font=font_item, anchor="lm")
                else:
                    draw.text((100, y), "无", fill=(100, 100, 100), font=font_item, anchor="lm")
            
            buffer = BytesIO()
            img.save(buffer, format='PNG')
            return base64.b64encode(buffer.getvalue()).decode()
        except:
            return None
    
    def generate_squad_image(self, squad_data, group_id):
        if not HAS_PIL:
            return None
        try:
            members = squad_data.get("members", [])
            height = 100 + len(members) * 60
            img = Image.new('RGB', (400, height), color=(25, 35, 30))
            draw = ImageDraw.Draw(img)
            
            try:
                font_title = ImageFont.truetype("msyh.ttc", 22)
                font_name = ImageFont.truetype("msyh.ttc", 16)
                font_info = ImageFont.truetype("msyh.ttc", 12)
            except:
                font_title = ImageFont.load_default()
                font_name = font_title
                font_info = font_title
            
            draw.text((200, 30), "小队信息", fill=(100, 255, 150), font=font_title, anchor="mm")
            draw.text((200, 55), f"队长: {squad_data.get('leader_name', '???')}", fill=(255, 200, 100), font=font_info, anchor="mm")
            draw.line((20, 75, 380, 75), fill=(80, 80, 80), width=1)
            
            for i, m in enumerate(members):
                y = 90 + i * 60
                uid = m.get("user_id")
                
                avatar_path = self.download_avatar(uid)
                if avatar_path and os.path.exists(avatar_path):
                    avatar = Image.open(avatar_path).resize((45, 45))
                    mask = Image.new('L', (45, 45), 0)
                    ImageDraw.Draw(mask).ellipse((0, 0, 45, 45), fill=255)
                    img.paste(avatar, (30, y), mask)
                
                name = m.get("nickname", "???")[:10]
                draw.text((90, y + 15), name, fill=(200, 200, 200), font=font_name, anchor="lm")
                
                status = "在线" if m.get("in_raid") else "待机"
                color = (100, 255, 100) if m.get("in_raid") else (150, 150, 150)
                draw.text((90, y + 35), status, fill=color, font=font_info, anchor="lm")
            
            buffer = BytesIO()
            img.save(buffer, format='PNG')
            return base64.b64encode(buffer.getvalue()).decode()
        except:
            return None
    
    def cmd_shop(self, event, user_id, nickname):
        player = self.get_player(user_id)
        img = self.generate_shop_image(player["money"])
        if img:
            self.reply(event, f"[CQ:image,file=base64://{img}]")
        else:
            msg = "[装备商店]\n================\n"
            for eq in self.equipment:
                msg += f"{eq['name']} - ${eq['price']:,}\n"
            self.reply(event, msg)
        return True
    
    def cmd_buy(self, event, user_id, nickname, item_name):
        if not item_name:
            self.reply(event, "[提示] 请输入要购买的装备名称")
            return True
        
        player = self.get_player(user_id)
        eq = next((e for e in self.equipment if e["name"] == item_name), None)
        
        if not eq:
            self.reply(event, f"[错误] 找不到装备: {item_name}")
            return True
        
        if player["money"] < eq["price"]:
            self.reply(event, f"[错误] 余额不足, 需要 ${eq['price']:,}")
            return True
        
        player["money"] -= eq["price"]
        player["equipped"][eq["type"]] = eq["id"]
        self.save_data()
        
        self.reply(event, f"[购买成功] 已装备 {eq['name']}\n余额: ${player['money']:,}")
        return True
    
    def cmd_equipment(self, event, user_id, nickname):
        player = self.get_player(user_id)
        player["nickname"] = nickname
        
        img = self.generate_equipment_image(user_id, nickname, player)
        if img:
            self.reply(event, f"[CQ:image,file=base64://{img}]")
        else:
            equipped = player.get("equipped", {})
            msg = f"[{nickname} 的装备]\n================\n"
            for slot, name in [("weapon", "武器"), ("armor", "护甲"), ("helmet", "头盔"), ("bag", "背包")]:
                eq_id = equipped.get(slot)
                eq = next((e for e in self.equipment if e["id"] == eq_id), None) if eq_id else None
                msg += f"{name}: {eq['name'] if eq else '无'}\n"
            self.reply(event, msg)
        return True
    
    def cmd_missions(self, event, user_id, nickname):
        player = self.get_player(user_id)
        
        img = self.generate_mission_image(player, self.missions)
        if img:
            self.reply(event, f"[CQ:image,file=base64://{img}]")
        else:
            msg = "[任务列表]\n================\n"
            for m in self.missions:
                msg += f"{m['name']}: {m['desc']} (奖励${m['reward']:,})\n"
            self.reply(event, msg)
        return True
    
    def cmd_accept_mission(self, event, user_id, nickname, mission_name):
        if not mission_name:
            self.reply(event, "[提示] 请输入任务名称")
            return True
        
        player = self.get_player(user_id)
        
        if player.get("mission"):
            self.reply(event, f"[提示] 你已有任务: {player['mission']}, 完成后再接新任务")
            return True
        
        mission = next((m for m in self.missions if m["name"] == mission_name), None)
        if not mission:
            self.reply(event, f"[错误] 找不到任务: {mission_name}")
            return True
        
        player["mission"] = mission["name"]
        player["mission_progress"] = 0
        self.save_data()
        
        self.reply(event, f"[接取成功] {mission['name']}\n目标: {mission['desc']}\n奖励: ${mission['reward']:,}")
        return True
    
    def cmd_create_squad(self, event, user_id, nickname, group_id):
        player = self.get_player(user_id)
        squad_id = f"{group_id}_squad"
        
        if squad_id in self.squads:
            squad = self.squads[squad_id]
            is_member = any(m["user_id"] == user_id for m in squad["members"])
            if is_member:
                self.reply(event, "[提示] 你已经在小队中")
                return True
            else:
                self.reply(event, f"[提示] 当前群已有小队, 队长: {squad['leader_name']}, 使用 /入队 加入")
                return True
        
        self.squads[squad_id] = {
            "leader": user_id,
            "leader_name": nickname,
            "members": [{"user_id": user_id, "nickname": nickname, "in_raid": player.get("in_raid", False)}],
            "group_id": group_id
        }
        player["squad"] = squad_id
        self.save_data()
        
        self.reply(event, f"[小队创建成功] 队长: {nickname}\n其他人可以使用 /入队 加入")
        return True
    
    def cmd_join_squad(self, event, user_id, nickname, group_id):
        player = self.get_player(user_id)
        squad_id = f"{group_id}_squad"
        
        if squad_id not in self.squads:
            self.reply(event, "[提示] 当前群没有小队, 使用 /组队 创建")
            return True
        
        squad = self.squads[squad_id]
        
        is_member = any(m["user_id"] == user_id for m in squad["members"])
        if is_member:
            self.reply(event, "[提示] 你已经在小队中")
            return True
        
        if len(squad["members"]) >= 4:
            self.reply(event, "[提示] 小队已满 (最多4人)")
            return True
        
        squad["members"].append({"user_id": user_id, "nickname": nickname, "in_raid": player.get("in_raid", False)})
        player["squad"] = squad_id
        self.save_data()
        
        self.reply(event, f"[加入成功] 已加入 {squad['leader_name']} 的小队 ({len(squad['members'])}/4)")
        return True
    
    def cmd_squad_info(self, event, user_id, nickname, group_id):
        squad_id = f"{group_id}_squad"
        
        if squad_id not in self.squads:
            self.reply(event, "[提示] 当前群没有小队, 使用 /组队 创建")
            return True
        
        squad = self.squads[squad_id]
        
        img = self.generate_squad_image(squad, group_id)
        if img:
            self.reply(event, f"[CQ:image,file=base64://{img}]")
        else:
            msg = f"[小队信息]\n队长: {squad['leader_name']}\n成员:\n"
            for m in squad["members"]:
                msg += f"- {m['nickname']}\n"
            self.reply(event, msg)
        return True
    
    def cmd_leave_squad(self, event, user_id, nickname, group_id):
        squad_id = f"{group_id}_squad"
        
        if squad_id not in self.squads:
            self.reply(event, "[提示] 当前群没有小队")
            return True
        
        squad = self.squads[squad_id]
        
        is_member = any(m["user_id"] == user_id for m in squad["members"])
        if not is_member:
            self.reply(event, "[提示] 你不在小队中")
            return True
        
        if squad["leader"] == user_id:
            for m in squad["members"]:
                p = self.get_player(m["user_id"])
                p["squad"] = None
            del self.squads[squad_id]
            self.save_data()
            self.reply(event, "[小队已解散]")
        else:
            squad["members"] = [m for m in squad["members"] if m["user_id"] != user_id]
            player = self.get_player(user_id)
            player["squad"] = None
            self.save_data()
            self.reply(event, f"[已退出小队] 当前小队剩余 {len(squad['members'])} 人")
        return True
    
    def truncate_name(self, name, max_len=8):
        if len(name) <= max_len:
            return name
        return name[:max_len-2] + ".."
    
    def cmd_create_2v2(self, event, user_id, nickname, group_id):
        battle_id = f"{group_id}_2v2"
        
        if battle_id in self.battles_2v2:
            battle = self.battles_2v2[battle_id]
            total = len(battle["team_a"]) + len(battle["team_b"])
            self.reply(event, f"[提示] 已有2v2对战等待中 ({total}/4人)\n使用 /加入2v2 参战")
            return True
        
        self.battles_2v2[battle_id] = {
            "team_a": [{"user_id": user_id, "nickname": self.truncate_name(nickname)}],
            "team_b": [],
            "group_id": group_id,
            "creator": user_id,
            "created_at": time.time(),
            "scheduled_time": None
        }
        
        img = self.generate_2v2_lobby_image(self.battles_2v2[battle_id])
        if img:
            self.reply(event, f"[CQ:image,file=base64://{img}]")
        else:
            self.reply(event, f"[2v2对战创建成功]\n队伍A: {self.truncate_name(nickname)}\n队伍B: 等待中\n使用 /加入2v2 参战")
        return True
    
    def cmd_join_2v2(self, event, user_id, nickname, group_id):
        battle_id = f"{group_id}_2v2"
        
        if battle_id not in self.battles_2v2:
            self.reply(event, "[提示] 当前没有2v2对战, 使用 /2v2 创建")
            return True
        
        battle = self.battles_2v2[battle_id]
        
        all_members = battle["team_a"] + battle["team_b"]
        if any(m["user_id"] == user_id for m in all_members):
            self.reply(event, "[提示] 你已经在对战中")
            return True
        
        if len(all_members) >= 4:
            self.reply(event, "[提示] 对战人数已满")
            return True
        
        if len(battle["team_a"]) <= len(battle["team_b"]):
            battle["team_a"].append({"user_id": user_id, "nickname": self.truncate_name(nickname)})
            team_name = "A"
        else:
            battle["team_b"].append({"user_id": user_id, "nickname": self.truncate_name(nickname)})
            team_name = "B"
        
        total = len(battle["team_a"]) + len(battle["team_b"])
        
        img = self.generate_2v2_lobby_image(battle)
        if img:
            self.reply(event, f"[CQ:image,file=base64://{img}]")
        else:
            self.reply(event, f"[加入成功] {self.truncate_name(nickname)} 加入队伍{team_name} ({total}/4)")
        
        if total == 4:
            self.reply(event, "[人数已满] 队长可使用 /开战 开始对战, 或 /定时2v2 HH:MM 设置开始时间")
        
        return True
    
    def cmd_start_2v2(self, event, user_id, nickname, group_id):
        battle_id = f"{group_id}_2v2"
        
        if battle_id not in self.battles_2v2:
            self.reply(event, "[提示] 当前没有2v2对战")
            return True
        
        battle = self.battles_2v2[battle_id]
        
        if battle["creator"] != user_id:
            self.reply(event, "[提示] 只有创建者可以开始对战")
            return True
        
        if len(battle["team_a"]) < 1 or len(battle["team_b"]) < 1:
            self.reply(event, "[提示] 每队至少需要1人")
            return True
        
        self.run_2v2_battle(event, battle, group_id)
        del self.battles_2v2[battle_id]
        return True
    
    def cmd_schedule_2v2(self, event, user_id, nickname, group_id, time_str):
        battle_id = f"{group_id}_2v2"
        
        if battle_id not in self.battles_2v2:
            self.reply(event, "[提示] 当前没有2v2对战")
            return True
        
        battle = self.battles_2v2[battle_id]
        
        if battle["creator"] != user_id:
            self.reply(event, "[提示] 只有创建者可以设置时间")
            return True
        
        try:
            parts = time_str.split(":")
            hour, minute = int(parts[0]), int(parts[1])
            now = time.localtime()
            scheduled = time.mktime(time.struct_time((now.tm_year, now.tm_mon, now.tm_mday, hour, minute, 0, 0, 0, -1)))
            if scheduled < time.time():
                scheduled += 86400
            battle["scheduled_time"] = scheduled
            self.reply(event, f"[定时成功] 2v2对战将在 {hour:02d}:{minute:02d} 自动开始")
        except:
            self.reply(event, "[格式错误] 请使用 /定时2v2 HH:MM 格式")
        return True
    
    def cmd_2v2_status(self, event, user_id, nickname, group_id):
        battle_id = f"{group_id}_2v2"
        
        if battle_id not in self.battles_2v2:
            self.reply(event, "[提示] 当前没有2v2对战")
            return True
        
        battle = self.battles_2v2[battle_id]
        img = self.generate_2v2_lobby_image(battle)
        if img:
            self.reply(event, f"[CQ:image,file=base64://{img}]")
        else:
            team_a = ", ".join([m["nickname"] for m in battle["team_a"]]) or "空"
            team_b = ", ".join([m["nickname"] for m in battle["team_b"]]) or "空"
            self.reply(event, f"[2v2对战状态]\n队伍A: {team_a}\n队伍B: {team_b}")
        return True
    
    def cmd_cancel_2v2(self, event, user_id, nickname, group_id):
        battle_id = f"{group_id}_2v2"
        
        if battle_id not in self.battles_2v2:
            self.reply(event, "[提示] 当前没有2v2对战")
            return True
        
        battle = self.battles_2v2[battle_id]
        
        if battle["creator"] != user_id:
            self.reply(event, "[提示] 只有创建者可以取消对战")
            return True
        
        del self.battles_2v2[battle_id]
        self.reply(event, "[2v2对战已取消]")
        return True
    
    def generate_2v2_lobby_image(self, battle):
        if not HAS_PIL:
            return None
        try:
            img = Image.new('RGB', (500, 280), color=(25, 30, 40))
            draw = ImageDraw.Draw(img)
            
            try:
                font_title = ImageFont.truetype("msyh.ttc", 26)
                font_team = ImageFont.truetype("msyh.ttc", 18)
                font_name = ImageFont.truetype("msyh.ttc", 16)
            except:
                font_title = ImageFont.load_default()
                font_team = font_title
                font_name = font_title
            
            draw.text((250, 25), "2v2 组队对战", fill=(255, 200, 50), font=font_title, anchor="mm")
            draw.line((30, 50, 470, 50), fill=(100, 100, 100), width=1)
            
            draw.rectangle([30, 70, 230, 200], fill=(40, 50, 35), outline=(100, 200, 100), width=2)
            draw.text((130, 90), "队伍 A", fill=(100, 255, 100), font=font_team, anchor="mm")
            
            for i, m in enumerate(battle["team_a"][:2]):
                y = 120 + i * 35
                avatar_path = self.download_avatar(m["user_id"])
                if avatar_path and os.path.exists(avatar_path):
                    avatar = Image.open(avatar_path).resize((30, 30))
                    mask = Image.new('L', (30, 30), 0)
                    ImageDraw.Draw(mask).ellipse((0, 0, 30, 30), fill=255)
                    img.paste(avatar, (50, y - 15), mask)
                draw.text((90, y), m["nickname"][:8], fill=(200, 255, 200), font=font_name, anchor="lm")
            
            for i in range(len(battle["team_a"]), 2):
                y = 120 + i * 35
                draw.text((90, y), "等待中...", fill=(100, 100, 100), font=font_name, anchor="lm")
            
            draw.rectangle([270, 70, 470, 200], fill=(50, 35, 35), outline=(200, 100, 100), width=2)
            draw.text((370, 90), "队伍 B", fill=(255, 100, 100), font=font_team, anchor="mm")
            
            for i, m in enumerate(battle["team_b"][:2]):
                y = 120 + i * 35
                avatar_path = self.download_avatar(m["user_id"])
                if avatar_path and os.path.exists(avatar_path):
                    avatar = Image.open(avatar_path).resize((30, 30))
                    mask = Image.new('L', (30, 30), 0)
                    ImageDraw.Draw(mask).ellipse((0, 0, 30, 30), fill=255)
                    img.paste(avatar, (290, y - 15), mask)
                draw.text((330, y), m["nickname"][:8], fill=(255, 200, 200), font=font_name, anchor="lm")
            
            for i in range(len(battle["team_b"]), 2):
                y = 120 + i * 35
                draw.text((330, y), "等待中...", fill=(100, 100, 100), font=font_name, anchor="lm")
            
            draw.text((250, 145), "VS", fill=(255, 215, 0), font=font_title, anchor="mm")
            
            total = len(battle["team_a"]) + len(battle["team_b"])
            status = f"人数: {total}/4"
            if battle.get("scheduled_time"):
                st = time.localtime(battle["scheduled_time"])
                status += f" | 开始时间: {st.tm_hour:02d}:{st.tm_min:02d}"
            draw.text((250, 230), status, fill=(180, 180, 180), font=font_name, anchor="mm")
            
            draw.text((250, 260), "/加入2v2 参战 | /开战 开始", fill=(120, 120, 120), font=font_name, anchor="mm")
            
            buffer = BytesIO()
            img.save(buffer, format='PNG')
            return base64.b64encode(buffer.getvalue()).decode()
        except Exception as e:
            print(f"[2v2] Lobby image error: {e}")
            return None
    
    def run_2v2_battle(self, event, battle, group_id):
        team_a = battle["team_a"]
        team_b = battle["team_b"]
        
        events = []
        team_a_hp = {m["user_id"]: 100 for m in team_a}
        team_b_hp = {m["user_id"]: 100 for m in team_b}
        
        events.append({"type": "start", "team_a": team_a, "team_b": team_b})
        
        round_num = 0
        max_rounds = 10
        
        while round_num < max_rounds:
            round_num += 1
            
            alive_a = [m for m in team_a if team_a_hp[m["user_id"]] > 0]
            alive_b = [m for m in team_b if team_b_hp[m["user_id"]] > 0]
            
            if not alive_a or not alive_b:
                break
            
            attacker = random.choice(alive_a + alive_b)
            
            if attacker in alive_a:
                target = random.choice(alive_b)
                damage = random.randint(20, 50)
                team_b_hp[target["user_id"]] -= damage
                events.append({
                    "type": "attack",
                    "attacker": attacker,
                    "target": target,
                    "damage": damage,
                    "target_hp": max(0, team_b_hp[target["user_id"]]),
                    "attacker_team": "A"
                })
            else:
                target = random.choice(alive_a)
                damage = random.randint(20, 50)
                team_a_hp[target["user_id"]] -= damage
                events.append({
                    "type": "attack",
                    "attacker": attacker,
                    "target": target,
                    "damage": damage,
                    "target_hp": max(0, team_a_hp[target["user_id"]]),
                    "attacker_team": "B"
                })
        
        alive_a = [m for m in team_a if team_a_hp[m["user_id"]] > 0]
        alive_b = [m for m in team_b if team_b_hp[m["user_id"]] > 0]
        
        if len(alive_a) > len(alive_b):
            winner = "A"
            winner_team = team_a
        elif len(alive_b) > len(alive_a):
            winner = "B"
            winner_team = team_b
        else:
            total_hp_a = sum(team_a_hp.values())
            total_hp_b = sum(team_b_hp.values())
            winner = "A" if total_hp_a >= total_hp_b else "B"
            winner_team = team_a if winner == "A" else team_b
        
        events.append({"type": "end", "winner": winner, "winner_team": winner_team, "team_a_hp": team_a_hp, "team_b_hp": team_b_hp})
        
        for m in winner_team:
            player = self.get_player(m["user_id"])
            player["wins"] += 1
            player["kills"] += 1
        
        loser_team = team_b if winner == "A" else team_a
        for m in loser_team:
            player = self.get_player(m["user_id"])
            player["deaths"] += 1
        
        self.save_data()
        
        gif = self.generate_2v2_battle_gif(events, team_a, team_b)
        if gif:
            self.reply(event, f"[CQ:image,file=base64://{gif}]")
        else:
            winner_names = ", ".join([m["nickname"] for m in winner_team])
            self.reply(event, f"[2v2对战结束]\n获胜队伍: {winner}\n队员: {winner_names}")
    
    def generate_2v2_battle_gif(self, events, team_a, team_b):
        if not HAS_PIL:
            return None
        try:
            width, height = 520, 320
            frames = []
            
            try:
                font_title = ImageFont.truetype("msyh.ttc", 24)
                font_info = ImageFont.truetype("msyh.ttc", 16)
                font_name = ImageFont.truetype("msyh.ttc", 14)
            except:
                font_title = ImageFont.load_default()
                font_info = font_title
                font_name = font_title
            
            def draw_teams(draw, img, team_a, team_b, hp_a, hp_b):
                draw.text((130, 30), "队伍A", fill=(100, 255, 100), font=font_info, anchor="mm")
                draw.text((390, 30), "队伍B", fill=(255, 100, 100), font=font_info, anchor="mm")
                
                for i, m in enumerate(team_a[:2]):
                    y = 60 + i * 50
                    hp = hp_a.get(m["user_id"], 100)
                    avatar_path = self.download_avatar(m["user_id"])
                    if avatar_path and os.path.exists(avatar_path):
                        avatar = Image.open(avatar_path).resize((40, 40))
                        if hp <= 0:
                            avatar = avatar.convert('L').convert('RGB')
                        mask = Image.new('L', (40, 40), 0)
                        ImageDraw.Draw(mask).ellipse((0, 0, 40, 40), fill=255)
                        img.paste(avatar, (30, y), mask)
                    
                    name_color = (150, 150, 150) if hp <= 0 else (200, 255, 200)
                    draw.text((80, y + 10), m["nickname"][:6], fill=name_color, font=font_name, anchor="lm")
                    
                    hp_color = (100, 100, 100) if hp <= 0 else (100, 255, 100)
                    hp_width = max(0, int(80 * hp / 100))
                    draw.rectangle([80, y + 25, 160, y + 35], fill=(50, 50, 50))
                    draw.rectangle([80, y + 25, 80 + hp_width, y + 35], fill=hp_color)
                    draw.text((170, y + 30), f"{max(0,hp)}", fill=hp_color, font=font_name, anchor="lm")
                
                for i, m in enumerate(team_b[:2]):
                    y = 60 + i * 50
                    hp = hp_b.get(m["user_id"], 100)
                    avatar_path = self.download_avatar(m["user_id"])
                    if avatar_path and os.path.exists(avatar_path):
                        avatar = Image.open(avatar_path).resize((40, 40))
                        if hp <= 0:
                            avatar = avatar.convert('L').convert('RGB')
                        mask = Image.new('L', (40, 40), 0)
                        ImageDraw.Draw(mask).ellipse((0, 0, 40, 40), fill=255)
                        img.paste(avatar, (450, y), mask)
                    
                    name_color = (150, 150, 150) if hp <= 0 else (255, 200, 200)
                    draw.text((440, y + 10), m["nickname"][:6], fill=name_color, font=font_name, anchor="rm")
                    
                    hp_color = (100, 100, 100) if hp <= 0 else (255, 100, 100)
                    hp_width = max(0, int(80 * hp / 100))
                    draw.rectangle([360, y + 25, 440, y + 35], fill=(50, 50, 50))
                    draw.rectangle([360, y + 25, 360 + hp_width, y + 35], fill=hp_color)
                    draw.text((350, y + 30), f"{max(0,hp)}", fill=hp_color, font=font_name, anchor="rm")
            
            hp_a = {m["user_id"]: 100 for m in team_a}
            hp_b = {m["user_id"]: 100 for m in team_b}
            
            for evt in events:
                if evt["type"] == "start":
                    for _ in range(3):
                        img = Image.new('RGB', (width, height), color=(25, 30, 40))
                        draw = ImageDraw.Draw(img)
                        draw_teams(draw, img, team_a, team_b, hp_a, hp_b)
                        draw.text((260, 200), "2v2 对战开始!", fill=(255, 215, 0), font=font_title, anchor="mm")
                        draw.text((260, 240), "VS", fill=(255, 100, 50), font=font_title, anchor="mm")
                        frames.append(img.copy())
                
                elif evt["type"] == "attack":
                    if evt["attacker_team"] == "A":
                        hp_b[evt["target"]["user_id"]] = evt["target_hp"]
                    else:
                        hp_a[evt["target"]["user_id"]] = evt["target_hp"]
                    
                    for _ in range(2):
                        img = Image.new('RGB', (width, height), color=(25, 30, 40))
                        draw = ImageDraw.Draw(img)
                        draw_teams(draw, img, team_a, team_b, hp_a, hp_b)
                        
                        atk_name = evt["attacker"]["nickname"][:6]
                        tgt_name = evt["target"]["nickname"][:6]
                        dmg = evt["damage"]
                        
                        draw.text((260, 200), f"{atk_name} 攻击 {tgt_name}", fill=(255, 200, 100), font=font_info, anchor="mm")
                        draw.text((260, 230), f"-{dmg} HP", fill=(255, 80, 80), font=font_title, anchor="mm")
                        
                        if evt["target_hp"] <= 0:
                            draw.text((260, 270), f"{tgt_name} 阵亡!", fill=(255, 50, 50), font=font_info, anchor="mm")
                        
                        frames.append(img.copy())
                
                elif evt["type"] == "end":
                    for _ in range(4):
                        img = Image.new('RGB', (width, height), color=(25, 30, 40))
                        draw = ImageDraw.Draw(img)
                        draw_teams(draw, img, team_a, team_b, hp_a, hp_b)
                        
                        winner = evt["winner"]
                        winner_color = (100, 255, 100) if winner == "A" else (255, 100, 100)
                        draw.text((260, 200), f"队伍 {winner} 获胜!", fill=winner_color, font=font_title, anchor="mm")
                        
                        winner_names = ", ".join([m["nickname"][:6] for m in evt["winner_team"]])
                        draw.text((260, 250), winner_names, fill=(255, 215, 0), font=font_info, anchor="mm")
                        
                        frames.append(img.copy())
            
            buffer = BytesIO()
            frames[0].save(buffer, format='GIF', save_all=True, append_images=frames[1:], duration=500, loop=0)
            return base64.b64encode(buffer.getvalue()).decode()
        except Exception as e:
            print(f"[2v2] Battle GIF error: {e}")
            return None

plugin = GunfightPlugin()
register_plugin(plugin)
