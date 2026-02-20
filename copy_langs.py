import os
import shutil
import json
import locale
import sys
from SCons.Script import Import

Import("env")

def minify_json(src, dst):
    """讀取 JSON 並去除空白後寫入目標路徑 (壓縮檔案)"""
    try:
        with open(src, 'r', encoding='utf-8') as f:
            data = json.load(f)
        with open(dst, 'w', encoding='utf-8') as f:
            # separators=(',', ':') 會去除多餘空格與換行
            json.dump(data, f, ensure_ascii=False, separators=(',', ':'))
        return True
    except Exception as e:
        print(f"[Auto-Lang] JSON 壓縮失敗 {src}: {e}")
        return False

def auto_copy_languages(source, target, env):
    project_dir = env.subst("$PROJECT_DIR")
    lang_dir = os.path.join(project_dir, "lang")
    data_dir = os.path.join(project_dir, "data")

    print("-" * 40)

    if not os.path.exists(lang_dir):
        print("[Auto-Lang] ⚠️ 找不到 'lang' 資料夾，跳過。")
        return

    # 0. 掃描可用語言並詢問
    available_langs = set()
    for f in os.listdir(lang_dir):
        if f.startswith("lang_") and f.endswith(".json"):
            parts = f.split('.')[0].split('_')
            if len(parts) >= 2:
                available_langs.add(parts[-1])
    
    print(f"[Auto-Lang] 發現可用語言: {', '.join(sorted(available_langs))}")

    # 0.1 偵測系統語言並設定預設值
    default_langs = ['en'] # 預設總是包含英文作為基底
    try:
        # 取得系統語系，例如 ('zh_TW', 'cp950')
        sys_loc = locale.getdefaultlocale()[0]
        if sys_loc:
            sys_loc = sys_loc.lower()
            detected_code = None
            # 簡單的映射邏輯：將系統語系對應到專案的語言代碼
            if 'zh' in sys_loc and ('tw' in sys_loc or 'hk' in sys_loc):
                detected_code = 'tw'
            elif 'ru' in sys_loc:
                detected_code = 'ru'
            elif 'ja' in sys_loc:
                detected_code = 'jp'
            elif 'de' in sys_loc:
                detected_code = 'de'
            elif 'es' in sys_loc:
                detected_code = 'es'
            
            # 如果偵測到的語言存在於可用列表中，且不是英文(已加)，則加入預設值
            if detected_code and detected_code in available_langs and detected_code != 'en':
                default_langs.append(detected_code)
    except:
        pass

    default_str = ', '.join(default_langs)
    print(f"[Auto-Lang] 系統偵測建議: [{default_str}]")

    # 檢查是否在互動式終端機中執行 (避免在 CI/CD 或非互動環境卡住)
    if sys.stdin and sys.stdin.isatty():
        print(f"[Auto-Lang] 請輸入要打包的語言代碼 (用逗號分隔)，直接按 Enter 使用建議值")
        try:
            user_input = input("請輸入 > ").strip()
        except:
            user_input = ""
    else:
        print(f"[Auto-Lang] 非互動模式 (或無法讀取輸入)，自動使用建議值")
        user_input = ""

    included_langs = [x.strip() for x in user_input.split(',')] if user_input else default_langs

    print(f"[Auto-Lang] 正在最佳化語言檔...")
    print(f"[Auto-Lang] 保留語言清單: {included_langs}")

    if not os.path.exists(data_dir):
        os.makedirs(data_dir)
        print(f"[Auto-Lang] 📂 已建立 'data' 資料夾")

    # 1. 清理 data 資料夾中現有的語言檔 (防止舊檔殘留佔用空間)
    # 假設語言檔以 lang_ 或 attributions_ 開頭
    print("[Auto-Lang] --- 開始清理舊語言檔 ---")
    for f in os.listdir(data_dir):
        if (f.startswith("lang_") or f.startswith("attributions_")) and f.endswith(".json"):
            file_path = os.path.join(data_dir, f)
            try:
                os.remove(file_path)
                print(f"[Auto-Lang] 🧹 已刪除: {f}")
            except Exception as e:
                print(f"[Auto-Lang] ❌ 刪除失敗: {f} ({e})")
    print("[Auto-Lang] --- 清理完成 ---")

    # 2. 篩選、壓縮並複製
    copied_count = 0
    total_size = 0
    
    for filename in os.listdir(lang_dir):
        if not filename.endswith(".json"):
            print(f"[Auto-Lang] ⏭️ 跳過非 JSON 檔案: {filename}")
            continue
            
        # 解析語言代碼 (例如 lang_en.json -> en)
        # 邏輯：取檔名中最後一個底線後的字串作為代碼
        parts = filename.split('.')[0].split('_')
        if len(parts) < 2: 
            print(f"[Auto-Lang] ❓ 檔名格式不符，跳過: {filename}")
            continue
        
        lang_code = parts[-1] 
        
        if lang_code in included_langs:
            src = os.path.join(lang_dir, filename)
            dst = os.path.join(data_dir, filename)
            original_size = os.path.getsize(src)
            
            # 執行壓縮複製
            if minify_json(src, dst):
                minified_size = os.path.getsize(dst)
                total_size += minified_size
                reduction = original_size - minified_size
                reduction_pct = (reduction / original_size * 100) if original_size > 0 else 0
                print(f"[Auto-Lang] ✅ 已壓縮打包: {filename} ({original_size} -> {minified_size} bytes, 節省 {reduction_pct:.1f}%)")
                copied_count += 1
            else:
                # 壓縮失敗則直接複製
                shutil.copy2(src, dst)
                final_size = os.path.getsize(dst)
                total_size += final_size
                print(f"[Auto-Lang] ⚠️ 僅複製 (壓縮失敗): {filename} ({final_size} bytes)")
                copied_count += 1
        else:
            # 這裡可以選擇是否顯示跳過的檔案
            print(f"[Auto-Lang] ⏭️ 跳過 (不在保留清單中): {filename}")

    print(f"[Auto-Lang] 完成！共打包 {copied_count} 個檔案，總大小: {total_size} bytes")
    print("-" * 40)

# 將此腳本掛載到 SPIFFS 建置流程前
env.AddPreAction("$BUILD_DIR/spiffs.bin", auto_copy_languages)
env.AddPreAction("uploadfs", auto_copy_languages)
