import requests
from typing import List, Dict, Optional, Callable, Union
import re
import asyncio
from datetime import datetime, time
import json
import os
import sys
import logging

# 独立运行的日志配置
class Logger:
    def __init__(self):
        self.logger = logging.getLogger('mima_standalone')
        if not self.logger.handlers:
            handler = logging.StreamHandler()
            formatter = logging.Formatter('[%(levelname)s] %(message)s')
            handler.setFormatter(formatter)
            self.logger.addHandler(handler)
            self.logger.setLevel(logging.INFO)
    
    def info(self, msg):
        self.logger.info(msg)
    
    def error(self, msg):
        self.logger.error(msg)
    
    def warning(self, msg):
        self.logger.warning(msg)

# 全局日志实例
logger = Logger()

class YuafengSJZApi:
    """
    每日三角洲密码API接口类
    使用http://api-v2.yuafeng.cn/API/sjzmm.php接口
    """

    def __init__(self):
        self.base_url = "http://api-v2.yuafeng.cn/API/sjzmm.php"
        self.timeout = 30  # 请求超时时间（秒）

    async def map_pwd_daily(self) -> Dict:
        """
        获取每日三角洲密码数据
        返回格式: {地图名称: {"password": 密码, "date": 日期}}
        """
        try:
            # 使用同步的requests库，因为API很简单不需要异步
            import requests
            
            logger.info("正在从API获取密码数据...")
            
            # 调用API接口
            response = requests.get(
                self.base_url,
                params={'type': 'json'},
                timeout=self.timeout
            )
            
            # 检查响应状态
            response.raise_for_status()
            
            # 解析JSON响应
            api_data = response.json()
            
            # 检查API返回的状态
            if api_data.get('code') != 0 or api_data.get('status') != 'success':
                error_msg = api_data.get('msg', 'API返回错误')
                logger.error(f"API错误: {error_msg}")
                raise Exception(f"API错误: {error_msg}")
            
            # 提取数据
            data = api_data.get('data', {})
            items = data.get('items', [])
            
            if not items:
                logger.warning("API返回的数据为空")
                return {}
            
            # 转换数据格式，充分利用新API返回的丰富信息
            map_data = {}
            current_date = data.get('update_time', datetime.now().strftime('%Y-%m-%d %H:%M:%S'))
            
            for item in items:
                map_name = item.get('map_name', '未知地图')
                password = item.get('password', '未知密码')
                location = item.get('location', '')
                sort = item.get('sort', 0)
                image_urls = item.get('image_urls', [])
                
                # 使用地图名称作为键，保持与原有格式兼容
                map_data[map_name] = {
                    "password": password,
                    "date": current_date,
                    "location": location,
                    "image_urls": image_urls,
                    "sort": sort  # 新增排序信息
                }
                
                logger.info(f"获取到地图 {map_name} 的密码: {password} (位置: {location})")
            
            logger.info(f"成功获取 {len(map_data)} 个地图的密码数据")
            return map_data
            
        except requests.exceptions.RequestException as e:
            logger.error(f"网络请求错误: {e}")
            if "timeout" in str(e).lower():
                raise Exception("请求超时，请检查网络连接")
            elif "connection" in str(e).lower():
                raise Exception("网络连接失败，请检查网络连接")
            else:
                raise Exception(f"网络请求失败: {str(e)}")
                
        except json.JSONDecodeError as e:
            logger.error(f"JSON解析错误: {e}")
            raise Exception("API返回数据格式错误")
            
        except Exception as e:
            logger.error(f"获取密码数据时出错: {e}")
            raise


class MimaCache:
    """
    密码缓存管理类，实现缓存到晚上12点自动丢弃的逻辑
    """

    def __init__(self):
        # 使用当前目录下的 data 文件夹
        current_dir = os.path.dirname(os.path.abspath(__file__))
        self.data_dir = os.path.join(current_dir, "data", "mima_standalone")
        os.makedirs(self.data_dir, exist_ok=True)
        self.cache_file = os.path.join(self.data_dir, "mima_cache.json")
        
        # TXT文件保存路径
        self.output_dir = os.path.join(current_dir, "core", "output")
        os.makedirs(self.output_dir, exist_ok=True)
        self.txt_file = os.path.join(self.output_dir, "mima_passwords.txt")
        
        self.api = YuafengSJZApi()

    def _is_cache_expired(self, cache_time: str) -> bool:
        """
        检查缓存是否已过期（是否已过晚上12点）
        学习鼠鼠限时的获取时间信息逻辑
        """
        try:
            # 解析缓存时间
            cache_datetime = datetime.fromisoformat(cache_time)
            current_datetime = datetime.now()
            
            # 如果缓存时间和当前时间不是同一天，说明已过12点
            if cache_datetime.date() != current_datetime.date():
                return True
            
            # 如果是同一天，检查是否已过晚上12点
            midnight = datetime.combine(current_datetime.date(), time(0, 0, 0))
            if current_datetime >= midnight and cache_datetime < midnight:
                return True
                
            return False
        except Exception as e:
            logger.error(f"检查缓存过期时间出错: {e}")
            return True  # 出错时认为已过期

    def _load_cache(self) -> Optional[Dict]:
        """
        加载缓存数据
        """
        try:
            if not os.path.exists(self.cache_file):
                return None
                
            with open(self.cache_file, 'r', encoding='utf-8') as f:
                cache_data = json.load(f)
                
            # 检查缓存是否过期
            if self._is_cache_expired(cache_data.get('cache_time', '')):
                logger.info("密码缓存已过期，将重新获取")
                self._clear_cache()
                return None
                
            return cache_data
        except Exception as e:
            logger.error(f"加载密码缓存出错: {e}")
            return None

    def _save_cache(self, data: Dict) -> None:
        """
        保存缓存数据
        """
        try:
            cache_data = {
                'cache_time': datetime.now().isoformat(),
                'data': data
            }
            
            with open(self.cache_file, 'w', encoding='utf-8') as f:
                json.dump(cache_data, f, ensure_ascii=False, indent=2)
                
            # 同时保存到TXT文件
            self._save_txt_file(data)
                
            logger.info("密码缓存已保存")
        except Exception as e:
            logger.error(f"保存密码缓存出错: {e}")
    
    def _save_txt_file(self, data: Dict) -> None:
        """
        保存密码数据到TXT文件，包含完整的API信息
        """
        try:
            # 清理过期的TXT文件
            self._cleanup_old_txt_files()
            
            current_time = datetime.now()
            txt_content = []
            txt_content.append(f"# 每日三角洲密码数据")
            txt_content.append(f"# 生成时间: {current_time.strftime('%Y-%m-%d %H:%M:%S')}")
            txt_content.append(f"# 有效期至: {current_time.strftime('%Y-%m-%d')} 23:59:59")
            txt_content.append("")
            
            # 按sort字段排序保存
            sorted_items = sorted(data.items(), key=lambda x: x[1].get('sort', 999))
            
            for map_name, info in sorted_items:
                password = info.get('password', '未知密码')
                date = info.get('date', '未知日期')
                location = info.get('location', '')
                image_urls = info.get('image_urls', [])
                sort = info.get('sort', 0)
                
                txt_content.append(f"地图: {map_name}")
                txt_content.append(f"密码: {password}")
                txt_content.append(f"日期: {date}")
                txt_content.append(f"排序: {sort}")
                
                if location:
                    txt_content.append(f"位置: {location}")
                
                if image_urls:
                    txt_content.append(f"图片数量: {len(image_urls)}")
                    for i, url in enumerate(image_urls, 1):
                        txt_content.append(f"图片{i}: {url}")
                
                txt_content.append("---")
            
            with open(self.txt_file, 'w', encoding='utf-8') as f:
                f.write('\n'.join(txt_content))
                
            logger.info(f"密码TXT文件已保存到: {self.txt_file}")
            
            # 下载并保存图片
            self._download_and_save_images(data)
            
        except Exception as e:
            logger.error(f"保存密码TXT文件出错: {e}")
    
    def _cleanup_old_txt_files(self) -> None:
        """
        清理过期的TXT文件（第二天删除）
        """
        try:
            if os.path.exists(self.txt_file):
                # 获取文件修改时间
                file_mtime = os.path.getmtime(self.txt_file)
                file_date = datetime.fromtimestamp(file_mtime).date()
                current_date = datetime.now().date()
                
                # 如果文件不是今天创建的，删除它
                if file_date < current_date:
                    os.remove(self.txt_file)
                    logger.info("已删除过期的密码TXT文件")
        except Exception as e:
            logger.error(f"清理过期TXT文件出错: {e}")

    def _download_and_save_images(self, data: Dict) -> None:
        """
        下载并保存图片到本地
        """
        try:
            # 创建图片保存目录
            images_dir = os.path.join(self.output_dir, "mima_images")
            os.makedirs(images_dir, exist_ok=True)
            
            # 按sort字段排序处理
            sorted_items = sorted(data.items(), key=lambda x: x[1].get('sort', 999))
            
            downloaded_files = []
            
            for map_name, info in sorted_items:
                image_urls = info.get('image_urls', [])
                map_name_clean = re.sub(r'[^\w\-_\.]', '_', map_name)  # 清理文件名中的特殊字符
                
                for i, url in enumerate(image_urls):
                    try:
                        # 下载图片
                        response = requests.get(url, timeout=10)
                        response.raise_for_status()
                        
                        # 确定文件扩展名
                        content_type = response.headers.get('content-type', '')
                        if 'jpeg' in content_type or 'jpg' in content_type:
                            ext = '.jpg'
                        elif 'png' in content_type:
                            ext = '.png'
                        elif 'webp' in content_type:
                            ext = '.webp'
                        else:
                            # 从URL中推断扩展名
                            if url.lower().endswith(('.jpg', '.jpeg', '.png', '.webp', '.gif')):
                                ext = os.path.splitext(url)[1].lower()
                            else:
                                ext = '.jpg'  # 默认
                        
                        # 保存图片
                        filename = f"{map_name_clean}_{i+1}{ext}"
                        filepath = os.path.join(images_dir, filename)
                        
                        with open(filepath, 'wb') as f:
                            f.write(response.content)
                        
                        downloaded_files.append(filepath)
                        logger.info(f"已下载图片: {filename} ({len(response.content)} bytes)")
                        
                    except Exception as e:
                        logger.error(f"下载图片失败 {url}: {e}")
                        continue
            
            # 创建图片索引文件
            if downloaded_files:
                self._create_image_index(sorted_items, downloaded_files, images_dir)
            
        except Exception as e:
            logger.error(f"下载图片时出错: {e}")

    def _create_image_index(self, sorted_items, downloaded_files, images_dir):
        """
        创建图片索引文件
        """
        try:
            index_file = os.path.join(images_dir, "image_index.txt")
            
            with open(index_file, 'w', encoding='utf-8') as f:
                f.write("# 每日三角洲密码图片索引\n")
                f.write(f"# 生成时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n\n")
                
                for map_name, info in sorted_items:
                    image_urls = info.get('image_urls', [])
                    map_name_clean = re.sub(r'[^\w\-_\.]', '_', map_name)
                    
                    f.write(f"地图: {map_name}\n")
                    f.write(f"密码: {info.get('password', '未知密码')}\n")
                    f.write(f"位置: {info.get('location', '')}\n")
                    
                    for i, url in enumerate(image_urls):
                        filename = f"{map_name_clean}_{i+1}"
                        for downloaded_file in downloaded_files:
                            if filename in downloaded_file:
                                f.write(f"图片{i+1}: {os.path.basename(downloaded_file)}\n")
                                break
                    
                    f.write("---\n")
            
            logger.info(f"图片索引文件已创建: {index_file}")
            
        except Exception as e:
            logger.error(f"创建图片索引文件出错: {e}")

    def _clear_cache(self) -> None:
        """
        清除缓存文件
        """
        try:
            if os.path.exists(self.cache_file):
                os.remove(self.cache_file)
                logger.info("密码缓存已清除")
            if os.path.exists(self.txt_file):
                os.remove(self.txt_file)
                logger.info("密码TXT文件已清除")
            
            # 清理图片文件夹
            images_dir = os.path.join(self.output_dir, "mima_images")
            if os.path.exists(images_dir):
                import shutil
                shutil.rmtree(images_dir)
                logger.info("密码图片文件夹已清除")
        except Exception as e:
            logger.error(f"清除密码缓存出错: {e}")
    
    def read_txt_file(self) -> Optional[str]:
        """
        读取TXT文件内容，供main.py调用
        """
        try:
            # 先清理过期文件
            self._cleanup_old_txt_files()
            
            if os.path.exists(self.txt_file):
                with open(self.txt_file, 'r', encoding='utf-8') as f:
                    content = f.read()
                logger.info("从TXT文件读取密码数据")
                return content
            else:
                logger.warning("TXT文件不存在")
                return None
        except Exception as e:
            logger.error(f"读取TXT文件出错: {e}")
            return None

    async def get_passwords(self) -> Dict:
        """
        获取密码数据，优先从缓存获取，缓存过期则重新获取
        """
        # 尝试从缓存加载
        cache_data = self._load_cache()
        if cache_data and cache_data.get('data'):
            logger.info("从缓存获取密码数据")
            return cache_data['data']
        
        # 缓存不存在或已过期，重新获取
        try:
            logger.info("正在从网络获取密码数据...")
            password_data = await self.api.map_pwd_daily()
            
            if password_data:
                # 保存到缓存
                self._save_cache(password_data)
                logger.info("密码数据获取成功并已缓存")
                return password_data
            else:
                logger.warning("获取到的密码数据为空")
                return {}
                
        except ImportError as e:
            logger.error(f"模块导入失败: {e}")
            raise ImportError("需要安装必要的依赖")
        except Exception as e:
            error_msg = str(e).lower()
            if any(keyword in error_msg for keyword in ['network', 'connection', 'timeout']):
                logger.error(f"网络连接错误: {e}")
                raise Exception("网络连接失败，请检查网络连接")
            else:
                logger.error(f"获取密码数据出错: {e}")
                raise

    def format_password_message(self, password_data: Dict, error_context: str = None) -> str:
        """
        格式化密码信息为用户友好的消息，充分利用新API返回的丰富信息
        """
        if not password_data:
            if error_context:
                return f"🐭 {error_context}"
            return "🐭 暂时无法获取密码信息，请稍后再试"
        
        # 按sort字段排序显示
        sorted_items = sorted(password_data.items(), key=lambda x: x[1].get('sort', 999))
        
        message_lines = ["🗝️ 每日三角洲密码 🗝️"]
        message_lines.append("")
        
        for map_name, info in sorted_items:
            password = info.get('password', '未知密码')
            location = info.get('location', '')
            image_urls = info.get('image_urls', [])
            sort = info.get('sort', 0)
            
            message_lines.append(f"📍 {map_name}")
            message_lines.append(f"🔑 密码: {password}")
            
            if location:
                message_lines.append(f"🎯 位置: {location}")
            
            # 不再显示参考图片信息
            
            message_lines.append("")
        
        # 添加缓存和提示信息
        current_time = datetime.now().strftime("%H:%M:%S")
        current_date = datetime.now().strftime("%Y-%m-%d")
        message_lines.append(f"⏰ 获取时间: {current_time}")
        message_lines.append(f"📅 有效日期: {current_date}")
        message_lines.append("💡 密码每日更新，缓存至晚上12点自动失效")
        message_lines.append("🔍 如需查看详细图片，请访问相关游戏社区")
        
        return "\n".join(message_lines)


class MimaTools:
    """
    鼠鼠密码工具类
    """

    def __init__(self):
        self.cache = MimaCache()

    async def get_mima_info(self) -> str:
        """
        获取密码信息
        """
        try:
            password_data = await self.cache.get_passwords()
            return self.cache.format_password_message(password_data)
        except ImportError as e:
            logger.error(f"模块导入失败: {e}")
            return "🐭 获取密码功能需要必要的依赖\n\n🔧 解决方案:\n1. 检查网络连接\n2. 重新安装必要的依赖包"
        except Exception as e:
            error_msg = str(e).lower()
            logger.error(f"获取密码信息出错: {e}")
            
            if any(keyword in error_msg for keyword in ['network', 'connection', 'timeout']):
                return "🐭 获取密码信息失败\n\n🔧 可能的解决方案:\n1. 检查网络连接是否正常\n2. 稍后再试"
            else:
                return "🐭 获取密码信息时发生错误，请稍后再试"

    async def refresh_mima_cache(self) -> str:
        """
        强制刷新密码缓存
        """
        try:
            # 清除现有缓存
            self.cache._clear_cache()
            
            # 重新获取
            password_data = await self.cache.get_passwords()
            
            if password_data:
                return "🔄 密码缓存已刷新\n\n" + self.cache.format_password_message(password_data)
            else:
                return "🐭 刷新密码缓存失败，请稍后再试"
                
        except ImportError as e:
            logger.error(f"模块导入失败: {e}")
            return "🐭 刷新密码功能需要必要的依赖\n\n🔧 解决方案:\n1. 检查网络连接\n2. 重新安装必要的依赖包"
        except Exception as e:
            error_msg = str(e).lower()
            logger.error(f"刷新密码缓存出错: {e}")
            
            if any(keyword in error_msg for keyword in ['network', 'connection', 'timeout']):
                return "🐭 刷新密码缓存失败\n\n🔧 可能的解决方案:\n1. 检查网络连接是否正常\n2. 稍后再试"
            else:
                return "🐭 刷新密码缓存时发生错误，请稍后再试"


# 独立调用接口
async def get_mima_async():
    """
    异步版本的密码获取函数，供其他模块调用
    """
    mima_tools = MimaTools()
    return await mima_tools.get_mima_info()


async def get_mima_with_fallback():
    """
    带降级处理的密码获取函数，优先从网络获取，失败则从TXT文件读取
    """
    try:
        # 尝试从网络获取最新数据
        mima_tools = MimaTools()
        return await mima_tools.get_mima_info()
    except Exception as e:
        logger.error(f"网络获取失败，尝试从TXT文件读取: {e}")
        # 网络获取失败，尝试从TXT文件读取
        txt_result = get_mima_from_txt()
        if txt_result:
            return txt_result
        else:
            error_msg = str(e).lower()
            if any(keyword in error_msg for keyword in ['network', 'connection', 'timeout']):
                return "🐭 网络连接错误\n\n🔧 可能的解决方案:\n1. 检查网络连接\n2. 稍后再试"
            else:
                return f"🐭 获取密码信息失败: {str(e)}"


def get_mima_sync():
    """
    同步版本的密码获取函数，供其他模块调用
    """
    try:
        # 尝试获取正在运行的事件循环
        loop = asyncio.get_event_loop()
        if loop.is_running():
            # 如果事件循环正在运行，创建任务并返回
            task = loop.create_task(get_mima_async())
            return task
        else:
            # 如果事件循环存在但没有运行，运行它
            return loop.run_until_complete(get_mima_async())
    except RuntimeError:
        # 没有事件循环，创建新的并运行
        return asyncio.run(get_mima_async())


def get_mima_for_plugin():
    """
    专门为插件调用的同步函数，带有完善的错误处理
    """
    try:
        # 首先尝试从TXT文件读取（最快且最稳定）
        txt_result = get_mima_from_txt()
        if txt_result:
            return txt_result
        
        # TXT文件不存在，尝试异步获取
        try:
            loop = asyncio.get_event_loop()
        except RuntimeError:
            loop = asyncio.new_event_loop()
            asyncio.set_event_loop(loop)
        
        if loop.is_running():
            # 在已经运行的事件循环中创建任务
            task = loop.create_task(get_mima_with_fallback())
            return task
        else:
            # 运行新的事件循环
            return loop.run_until_complete(get_mima_with_fallback())
            
    except Exception as e:
        logger.error(f"插件调用出错: {e}")
        return "🐭 密码功能暂时不可用，请检查系统环境或联系管理员"


def get_mima_images() -> List[str]:
    """
    获取已下载的图片文件路径列表，供main.py调用显示图片
    """
    try:
        current_dir = os.path.dirname(os.path.abspath(__file__))
        images_dir = os.path.join(current_dir, "core", "output", "mima_images")
        
        if not os.path.exists(images_dir):
            return []
        
        image_files = []
        for file in os.listdir(images_dir):
            if file.lower().endswith(('.jpg', '.jpeg', '.png', '.webp', '.gif')):
                file_path = os.path.join(images_dir, file)
                # 检查文件是否在今天创建（基于修改时间）
                file_mtime = os.path.getmtime(file_path)
                file_date = datetime.fromtimestamp(file_mtime).date()
                current_date = datetime.now().date()
                
                if file_date == current_date:
                    image_files.append(file_path)
        
        # 按文件名排序
        image_files.sort()
        
        logger.info(f"找到 {len(image_files)} 个今日密码图片文件")
        return image_files
        
    except Exception as e:
        logger.error(f"获取密码图片文件列表出错: {e}")
        return []


def get_mima_from_txt() -> Optional[str]:
    """
    从TXT文件读取密码信息，解析完整的API数据供main.py调用
    """
    try:
        cache = MimaCache()
        txt_content = cache.read_txt_file()
        
        if txt_content:
            # 解析TXT内容并格式化为用户友好的消息
            lines = txt_content.split('\n')
            message_lines = ["🗝️ 每日三角洲密码 🗝️"]
            message_lines.append("")
            
            current_map = None
            current_password = None
            current_date = None
            current_location = None
            current_image_count = 0
            
            for line in lines:
                line = line.strip()
                
                # 跳过注释行和空行
                if line.startswith('#') or not line:
                    continue
                
                if line.startswith('地图: '):
                    current_map = line.replace('地图: ', '')
                elif line.startswith('密码: '):
                    current_password = line.replace('密码: ', '')
                elif line.startswith('日期: '):
                    current_date = line.replace('日期: ', '')
                elif line.startswith('位置: '):
                    current_location = line.replace('位置: ', '')
                elif line.startswith('图片数量: '):
                    try:
                        current_image_count = int(line.replace('图片数量: ', ''))
                    except ValueError:
                        current_image_count = 0
                elif line == '---' and current_map and current_password:
                    # 输出一个完整的密码条目
                    message_lines.append(f"📍 {current_map}")
                    message_lines.append(f"🔑 密码: {current_password}")
                    message_lines.append(f"📅 日期: {current_date}")
                    
                    if current_location:
                        message_lines.append(f"🎯 位置: {current_location}")
                    
            # 不再显示参考图片信息
                    
                    message_lines.append("")
                    
                    # 重置当前条目数据
                    current_map = current_password = current_date = None
                    current_location = None
                    current_image_count = 0
            
            # 添加提示信息
            current_time = datetime.now().strftime("%H:%M:%S")
            current_date = datetime.now().strftime("%Y-%m-%d")
            message_lines.append(f"⏰ 读取时间: {current_time}")
            message_lines.append(f"📅 有效日期: {current_date}")
            message_lines.append("💡 密码数据来自TXT文件缓存")
            
            return "\n".join(message_lines)
        else:
            return None
    except Exception as e:
        logger.error(f"从TXT文件获取密码信息出错: {e}")
        return None


# 插件调用的主要入口函数
def plugin_get_mima():
    """
    供插件调用的主要函数
    """
    return get_mima_for_plugin()


# 测试函数，用于验证插件环境
def test_plugin_environment():
    """
    测试插件环境是否正常
    """
    try:
        # 测试基本导入
        import requests
        import asyncio
        print("✓ 基本模块导入成功")
        
        # 测试异步环境
        try:
            loop = asyncio.get_event_loop()
            print("✓ 事件循环获取成功")
        except:
            loop = asyncio.new_event_loop()
            asyncio.set_event_loop(loop)
            print("✓ 新事件循环创建成功")
            
        return True
    except Exception as e:
        print(f"✗ 环境测试失败: {e}")
        return False


async def main():
    """
    独立运行的主函数
    """
    import argparse
    
    parser = argparse.ArgumentParser(description='鼠鼠密码获取工具（完全独立版本）')
    parser.add_argument('--refresh', action='store_true', help='强制刷新缓存')
    parser.add_argument('--json', action='store_true', help='输出JSON格式')
    parser.add_argument('--raw', action='store_true', help='输出原始数据')
    parser.add_argument('--test', action='store_true', help='测试插件环境')
    
    args = parser.parse_args()
    
    logger.info("完全独立运行模式")
    
    # 环境测试
    if args.test:
        if test_plugin_environment():
            print("环境测试通过！")
        else:
            print("环境测试失败！")
        return
    
    try:
        mima_tools = MimaTools()
        
        if args.refresh:
            result = await mima_tools.refresh_mima_cache()
        else:
            result = await mima_tools.get_mima_info()
        
        if args.raw and args.json:
            # 输出原始JSON数据
            password_data = await mima_tools.cache.get_passwords()
            print(json.dumps(password_data, ensure_ascii=False, indent=2))
        elif args.json:
            # 输出格式化的JSON
            print(json.dumps({"message": result}, ensure_ascii=False, indent=2))
        else:
            # 输出格式化文本
            print(result)
            
    except Exception as e:
        logger.error(f"运行出错: {e}")
        print("🐭 程序运行出错，请检查网络连接或稍后再试")


if __name__ == "__main__":
    asyncio.run(main())
