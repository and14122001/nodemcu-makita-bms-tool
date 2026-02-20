/**
 * Makita BMS Diagnostic - i18n Manager
 * 自動掃描與動態載入 JSON 語言包
 */

let currentTranslations = {};

// 全域翻譯函數
window.t = function(key) {
    return currentTranslations[key] || key;
};

async function initLanguage() {
    console.log("i18n: 開始掃描語言包...");
    const select = document.getElementById('langSelect');
    if (!select) return;

    try {
        // 1. 請求後端列出所有 lang_*.json 檔案
        const resp = await fetch('/api/langs');
        const files = await resp.json(); // 例如 ["/lang_tw.json", "/lang_en.json"]
        
        if (!Array.isArray(files) || files.length === 0) {
            console.warn("未發現任何語言檔");
            return;
        }

        // 2. 平行下載所有語言檔以獲取 "lang_name" (檔案很小，這比逐個讀取快)
        const promises = files.map(async (file) => {
            try {
                const r = await fetch(file);
                const data = await r.json();
                return { 
                    file: file, 
                    name: data.lang_name || file.replace('/lang_', '').replace('.json', ''),
                    data: data 
                };
            } catch (e) {
                console.error(`無法載入 ${file}`, e);
                return null;
            }
        });

        const loadedLangs = (await Promise.all(promises)).filter(x => x);
        
        // 3. 填充下拉選單
        select.innerHTML = '';
        loadedLangs.forEach(lang => {
            const opt = document.createElement('option');
            opt.value = lang.file;
            
            // 根據檔名判斷國旗 Emoji
            let flag = '🌐';
            if (lang.file.includes('_tw')) flag = '🇹🇼';
            else if (lang.file.includes('_en')) flag = '🇺🇸';
            else if (lang.file.includes('_jp')) flag = '🇯🇵';
            else if (lang.file.includes('_de')) flag = '🇩🇪';
            else if (lang.file.includes('_ru')) flag = '🇷🇺';
            else if (lang.file.includes('_es')) flag = '🇪🇸';

            opt.textContent = `${flag} ${lang.name}`;
            select.appendChild(opt);
        });

        // 4. 決定預設語言
        const savedFile = localStorage.getItem('user_lang_file');
        let target = null;

        // 4a. 優先從 localStorage 讀取使用者先前的選擇
        if (savedFile) {
            target = loadedLangs.find(l => l.file === savedFile);
        }

        // 4b. 如果沒有儲存的設定，則根據瀏覽器語言偵測
        if (!target) {
            const browserLangs = navigator.languages || [navigator.language];
            console.log("i18n: Browser languages:", browserLangs);

            for (const browserLang of browserLangs) {
                const lang = browserLang.toLowerCase();
                let codeToFind = lang.split('-')[0]; // 預設使用主要語言代碼 (e.g., 'en' from 'en-US')

                // 針對中文的特殊處理
                if (lang.startsWith('zh') && (lang.includes('tw') || lang.includes('hk'))) {
                    codeToFind = 'tw';
                }

                target = loadedLangs.find(l => l.file.includes(`_${codeToFind}.json`));
                if (target) break; // 找到符合的就跳出迴圈
            }
        }
        
        // 4c. 如果以上都找不到，使用預設後備 (英文 -> 繁中 -> 第一個)
        if (!target) {
            target = loadedLangs.find(l => l.file.includes('_en')) || loadedLangs.find(l => l.file.includes('_tw')) || loadedLangs[0];
        }

        if (target) {
            select.value = target.file;
            applyTranslations(target);
            loadAndApplyAttributions(target.file);
        }

        // 5. 綁定切換事件
        select.onchange = (e) => {
            const file = e.target.value;
            localStorage.setItem('user_lang_file', file);
            const selectedLang = loadedLangs.find(l => l.file === file);
            if (selectedLang) {
                applyTranslations(selectedLang);
                loadAndApplyAttributions(selectedLang.file);
            }
        };

    } catch (e) {
        console.error("i18n 初始化失敗:", e);
    }
}

function applyTranslations(langObject) {
    const data = langObject.data;
    currentTranslations = data;

    // 從檔案路徑中提取語言代碼 (例如 /lang_tw.json -> tw)
    // 並更新 <html> 標籤的 lang 屬性
    const langCodeMatch = langObject.file.match(/lang_([a-zA-Z_]+)\.json/);
    if (langCodeMatch && langCodeMatch[1]) {
        document.documentElement.lang = langCodeMatch[1].replace('_', '-');
    }

    console.log("套用語言:", data.lang_name);

    document.querySelectorAll('[data-lang-key]').forEach(el => {
        const key = el.getAttribute('data-lang-key');
        if (data[key]) {
            el.innerHTML = data[key]; // 修正：使用 innerHTML 以支援 <br> 和 <b> 標籤
        }
    });

    // 觸發 UI 更新 (如果有的話)
    if (typeof updateStatusText === 'function') updateStatusText('uiReady');
}
window.initLanguage = initLanguage;

async function loadAndApplyAttributions(langFilePath) {
    const refContainer = document.getElementById('ref-content');
    if (!refContainer) return;

    // 預設顯示載入中訊息
    refContainer.innerHTML = `<ul><li>${t('loading_references')}</li></ul>`;

    try {
        const attrFilePath = langFilePath.replace('/lang_', '/attributions_');
        const resp = await fetch(attrFilePath);
        if (!resp.ok) throw new Error(`HTTP error! status: ${resp.status}`);
        const attrData = await resp.json();

        if (attrData.html) {
            refContainer.innerHTML = attrData.html;
        }
    } catch (e) {
        console.error(`Failed to load attributions from ${langFilePath}`, e);
        refContainer.innerHTML = `<ul><li>${t('failed_references')}</li></ul>`;
    }
}