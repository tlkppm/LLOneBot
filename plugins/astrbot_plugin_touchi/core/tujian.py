import os
import random
import math
import aiosqlite
from PIL import Image, ImageDraw, ImageFont
from datetime import datetime
from astrbot.api import logger

# 定义路径
script_dir = os.path.dirname(os.path.abspath(__file__))
items_dir = os.path.join(script_dir, "items")
output_dir = os.path.join(script_dir, "output")
os.makedirs(output_dir, exist_ok=True)

# 定义颜色常量
BACKGROUND_COLOR = (40, 40, 45)  # 深灰背景色
GRID_LINE_COLOR = (80, 80, 85)    # 网格线颜色
GRID_TEXT_COLOR = (180, 180, 180) # 网格文字颜色
ITEM_BORDER_COLOR = (100, 100, 110) # 物品边框颜色

# 定义物品背景色（带透明度）
background_colors = {
    "purple": (50, 43, 97, 80), 
    "blue": (49, 91, 126, 80), 
    "gold": (153, 116, 22, 80), 
    "red": (139, 35, 35, 80)
}

# 定义等级优先级
LEVEL_PRIORITY = {
    "red": 4,     # 最高级
    "gold": 3,
    "purple": 2,
    "blue": 1,
    "green": 0     # 最低级
}

# 只显示这些等级的物品
DISPLAY_LEVELS = {"red", "gold"}

# --- Helper Functions ---

def get_size(size_str):
    if 'x' in size_str:
        parts = size_str.split('x')
        if len(parts) == 2 and parts[0].isdigit() and parts[1].isdigit():
            return int(parts[0]), int(parts[1])
    return 1, 1

def place_items(items):
    """
    计算所需网格大小并放置物品
    返回: (放置的物品列表, 网格宽度, 网格高度)
    """
    # 计算总占用面积
    total_area = sum(item["grid_width"] * item["grid_height"] for item in items)
    
    # 计算初始网格大小 (正方形)
    grid_size = max(5, math.ceil(math.sqrt(total_area * 1.5)))
    
    # 尝试放置物品，如果放不下则增加网格大小
    while True:
        grid = [[0] * grid_size for _ in range(grid_size)]
        placed = []
        
        # 按等级优先级从高到低排序，相同等级再按面积从大到小排序
        sorted_items = sorted(
            items,
            key=lambda x: (LEVEL_PRIORITY.get(x["level"], 0), x["grid_width"] * x["grid_height"]),
            reverse=True
        )
        
        # 尝试放置每个物品
        for item in sorted_items:
            orientations = [(item["grid_width"], item["grid_height"], False)]
            if item["grid_width"] != item["grid_height"]:
                orientations.append((item["grid_height"], item["grid_width"], True))
            
            placed_successfully = False
            
            # 尝试每个方向
            for width, height, rotated in orientations:
                if placed_successfully:
                    break
                    
                # 按顺序遍历网格位置
                for y in range(grid_size - height + 1):
                    if placed_successfully:
                        break
                    for x in range(grid_size - width + 1):
                        # 检查位置是否可用
                        can_place = True
                        for i in range(height):
                            for j in range(width):
                                if grid[y+i][x+j] != 0:
                                    can_place = False
                                    break
                            if not can_place:
                                break
                        
                        # 如果位置可用，放置物品
                        if can_place:
                            for i in range(height):
                                for j in range(width):
                                    grid[y+i][x+j] = 1
                            
                            placed.append({
                                "item": item,
                                "x": x,
                                "y": y,
                                "width": width,
                                "height": height,
                                "rotated": rotated
                            })
                            placed_successfully = True
                            break
        
        # 检查是否所有物品都已放置
        if len(placed) == len(items):
            return placed, grid_size, grid_size
        
        # 如果放不下，增加网格大小
        grid_size += 1

def render_tujian_image(placed_items, grid_width, grid_height, cell_size=100):
    # 计算图片大小 (移除文字区域后减小高度)
    img_width = grid_width * cell_size + 100  # 保留边距
    img_height = grid_height * cell_size + 50  # 减小高度
    
    # 创建图片
    tujian_img = Image.new("RGB", (img_width, img_height), BACKGROUND_COLOR)
    draw = ImageDraw.Draw(tujian_img)
    
    # 计算网格起始位置 (居中)
    grid_start_x = (img_width - grid_width * cell_size) // 2
    grid_start_y = 20  # 上移起始位置
    
    # 绘制网格线
    for i in range(grid_width + 1):
        x = grid_start_x + i * cell_size
        draw.line([(x, grid_start_y), (x, grid_start_y + grid_height * cell_size)], 
                  fill=GRID_LINE_COLOR, width=1)
    
    for i in range(grid_height + 1):
        y = grid_start_y + i * cell_size
        draw.line([(grid_start_x, y), (grid_start_x + grid_width * cell_size, y)], 
                  fill=GRID_LINE_COLOR, width=1)
    
    # 绘制每个物品
    for placed in placed_items:
        item = placed["item"]
        x0 = grid_start_x + placed["x"] * cell_size
        y0 = grid_start_y + placed["y"] * cell_size
        x1 = x0 + placed["width"] * cell_size
        y1 = y0 + placed["height"] * cell_size
        
        # 获取背景色
        bg_color = background_colors.get(item["level"], (128, 128, 128, 80))
        
        # 创建半透明层
        overlay = Image.new('RGBA', (placed["width"] * cell_size, placed["height"] * cell_size), bg_color)
        
        # 将半透明层粘贴到主图像上
        tujian_img.paste(overlay, (x0, y0), overlay)
        
        # 绘制物品边框
        draw.rectangle([x0, y0, x1, y1], outline=ITEM_BORDER_COLOR, width=1)
        
        # 添加物品图片
        try:
            with Image.open(item["path"]).convert("RGBA") as item_img:
                if placed["rotated"]:
                    item_img = item_img.rotate(90, expand=True)
                
                # 缩放图片以适应格子
                max_width = placed["width"] * cell_size - 20
                max_height = placed["height"] * cell_size - 20
                item_img.thumbnail((max_width, max_height), Image.LANCZOS)
                
                # 居中放置图片
                paste_x = x0 + (placed["width"] * cell_size - item_img.width) // 2
                paste_y = y0 + (placed["height"] * cell_size - item_img.height) // 2
                tujian_img.paste(item_img, (paste_x, paste_y), item_img)
        except Exception as e:
            logger.error(f"图鉴渲染：无法加载物品图片 {item['path']}, 错误: {e}")
            
            # 绘制错误占位符
            cross_size = min(placed["width"], placed["height"]) * cell_size // 3
            center_x = x0 + placed["width"] * cell_size // 2
            center_y = y0 + placed["height"] * cell_size // 2
            draw.line([(center_x-cross_size, center_y-cross_size),
                      (center_x+cross_size, center_y+cross_size)],
                     fill=(255, 50, 50), width=3)
            draw.line([(center_x-cross_size, center_y+cross_size),
                      (center_x+cross_size, center_y-cross_size)],
                     fill=(255, 50, 50), width=3)
    
    return tujian_img

# --- Main Class ---

class TujianTools:
    def __init__(self, db_path):
        self.db_path = db_path
        self.all_items = self._load_all_item_definitions()

    def _load_all_item_definitions(self):
        items = []
        valid_extensions = ['.png', '.jpg', '.jpeg', '.gif', '.bmp']
        
        # 定义要扫描的文件夹列表
        directories_to_scan = [items_dir]
        
        # 添加 xinwuzi 文件夹到扫描列表
        xinwuzi_dir = os.path.join(script_dir, "xinwuzi")
        if os.path.exists(xinwuzi_dir):
            directories_to_scan.append(xinwuzi_dir)
        else:
            logger.warning("图鉴工具：xinwuzi 文件夹不存在，只加载 items 文件夹的物资")
        
        # 扫描所有指定文件夹
        for scan_dir in directories_to_scan:
            if not os.path.exists(scan_dir):
                logger.error(f"图鉴工具：文件夹 {scan_dir} 不存在！")
                continue
                
            for filename in os.listdir(scan_dir):
                file_path = os.path.join(scan_dir, filename)
                if os.path.isfile(file_path) and any(filename.lower().endswith(ext) for ext in valid_extensions):
                    item_name = os.path.splitext(filename)[0]
                    parts = item_name.split('_')
                    
                    # 判断是否为新物资格式（等级_大小_名称_价格）
                    if len(parts) >= 4 and parts[-1].isdigit():
                        # 新物资格式：等级_大小_名称_价格
                        level = parts[0].lower()
                        size_str = parts[1]
                        # 物品名称为除了等级、大小和价格之外的部分
                        name_parts = parts[2:-1]
                        # 构建基础名称（用于与数据库记录匹配）
                        base_name = f"{level}_{size_str}_{'_'.join(name_parts)}"
                    else:
                        # 原有格式：等级_大小_名称
                        if len(parts) >= 2:
                            level = parts[0].lower()
                            size_str = parts[1]
                        else:
                            level = "purple"
                            size_str = "1x1"
                        base_name = item_name
                    
                    width, height = get_size(size_str)
                    items.append({
                        "path": file_path,
                        "name": item_name,
                        "base_name": base_name,  # 添加基础名称用于匹配
                        "level": level,
                        "size": size_str,
                        "grid_width": width,
                        "grid_height": height
                    })
        
        logger.info(f"图鉴工具：成功加载 {len(items)} 个物资定义（来自 {len(directories_to_scan)} 个文件夹）")
        return items

    async def generate_tujian(self, user_id: str):
        if not self.db_path:
            return "数据库路径未配置，无法查询图鉴。"
        
        records = []
        try:
            async with aiosqlite.connect(self.db_path) as db:
                db.row_factory = aiosqlite.Row
                async with db.execute(
                    "SELECT DISTINCT item_name FROM user_touchi_collection WHERE user_id = ?",
                    (user_id,)
                ) as cursor:
                    records = await cursor.fetchall()
        except Exception as e:
            logger.error(f"查询用户 {user_id} 图鉴时出错: {e}")
            return "查询图鉴时数据库出错。"

        if not records:
            return "您还没有收集到任何物品！"

        user_item_names = {rec[0] for rec in records}
        
        # 只加载金色和红色的物品，支持多种匹配方式
        user_items_to_render = []
        for item in self.all_items:
            if item["level"] not in DISPLAY_LEVELS:
                continue
                
            # 尝试多种匹配方式
            match_found = False
            
            # 方式1：直接匹配完整名称
            if item["name"] in user_item_names:
                match_found = True
            # 方式2：匹配基础名称（处理xinwuzi格式）
            elif item.get("base_name", item["name"]) in user_item_names:
                match_found = True
            # 方式3：反向匹配（数据库中的名称是否匹配当前物品的任意名称）
            else:
                for db_item_name in user_item_names:
                    # 检查数据库中的名称是否是当前物品的基础名称
                    if db_item_name == item.get("base_name", item["name"]):
                        match_found = True
                        break
                    # 检查是否为xinwuzi格式的物资（去掉价格部分）
                    db_parts = db_item_name.split('_')
                    if len(db_parts) >= 4 and db_parts[-1].isdigit():
                        db_base_name = '_'.join(db_parts[:-1])  # 去掉价格部分
                        if db_base_name == item.get("base_name", item["name"]):
                            match_found = True
                            break
            
            if match_found:
                user_items_to_render.append(item)
        
        logger.info(f"用户 {user_id} 的图鉴：找到 {len(user_items_to_render)} 个匹配的物资（从 {len(user_item_names)} 个数据库记录中）")

        if not user_items_to_render:
            return "您还没有收集到金色或红色品质的物品！"
        
        # 放置物品并获取网格大小
        placed_items, grid_width, grid_height = place_items(user_items_to_render)
        
        if not placed_items:
            return "生成图鉴图片时发生布局错误。"

        # 渲染图鉴图片
        tujian_image = render_tujian_image(placed_items, grid_width, grid_height, cell_size=100)

        # 保存图片
        timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
        output_path = os.path.join(output_dir, f"tujian_{user_id}_{timestamp}.png")
        tujian_image.save(output_path)
        
        logger.info(f"成功为用户 {user_id} 生成图鉴: {output_path}")

        return output_path
