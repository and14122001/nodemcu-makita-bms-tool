/**
 * Makita BMS Diagnostic System - Final Integrated Edition
 * 整合功能：0V過濾、0xB6熔絲清除、自動語言對接、數據深度合併
 */

let lastData = {};
let lastFeatures = null;
let sessionHistory = []; // 用於儲存本次連線的歷史數據

function bindActions() {
    console.log("Binding actions...");

    // 1. 讀取資訊
    const btn1 = el('btnReadStatic');
    if (btn1) {
        // 按鍵 1 初始為藍色 (可用)
        btn1.classList.add('btn-blue');

        btn1.onclick = () => {
            setButtonLoading('btnReadStatic', true, 'reading');
            log(`${t('readStatic')}...`); // 1. 操作說明
            WSClient.send('read_static');
        };
    }

    // 2. 更新數據
    const btn2 = el('btnReadDynamic');
    if (btn2) {
        // 按鍵 2 初始為灰色 (不可用)
        btn2.classList.add('btn-gray');

        btn2.onclick = () => {
            setButtonLoading('btnReadDynamic', true, 'reading');
            log(`${t('readDynamic')}...`); // 1. 操作說明
            WSClient.send('read_dynamic');
        };
    }

    // 3. 清除故障碼
    const btnClearErrors = el('btnClearErrors');
    if (btnClearErrors) {
        // 按鍵 3 初始為灰色
        btnClearErrors.classList.add('btn-gray');

        btnClearErrors.onclick = () => {
            setButtonLoading('btnClearErrors', true, 'clearing');
            log(`${t('clearErrors')}...`); // 1. 操作說明
            WSClient.send('clear_errors');
        };
    }

    // 4. 測試 LED (連動多國語言)
    const btnLed = document.getElementById('btnLed');
    if (btnLed) {
        let isLedOn = false;

        // 按鍵 4 初始為灰色
        btnLed.classList.add('btn-gray');

        btnLed.onclick = function () {
            isLedOn = !isLedOn;
            const actionStatus = isLedOn ? 'on' : 'off';

            // 修正：補上按鍵操作日誌
            log(t('testing') || 'Testing LED...');

            WSClient.send(actionStatus === 'on' ? 'led_on' : 'led_off');

            // --- 視覺與文字更新 (使用妳提供的 ledOn/ledOff) ---
            if (isLedOn) {
                btnLed.style.background = "var(--success)"; // 改用變數
                btnLed.style.color = "#fff";
                // 使用 t() 函數讀取妳定義的 "LED 已開啟"
                btnLed.innerText = typeof t === 'function' ? t('ledOn') : "LED ON";
            } else {
                btnLed.style.background = ""; // 恢復原色
                btnLed.style.color = "";
                // 使用 t() 函數讀取妳定義的 "LED 已關閉"
                btnLed.innerText = typeof t === 'function' ? t('ledOff') : "LED OFF";
            }

            // 同步更新下方 Hint
            const hintLed = document.getElementById('hintLed');
            if (hintLed) {
                hintLed.innerText = isLedOn ? (typeof t === 'function' ? t('testing_short') : "Testing...") : "...";
            }
        };
    }

    // 5. 匯出 CSV
    const btnExport = el('btnExport');
    if (btnExport) {
        // 按鍵 5 初始為灰色 (無數據)
        btnExport.classList.add('btn-gray');
        btnExport.onclick = exportToCSV;
    }

    // 6. 下載 MCU 紀錄
    const btnMcuDl = el('btnMcuDownload');
    if (btnMcuDl) {
        btnMcuDl.onclick = () => {
            window.location.href = '/datalog.csv';
        };
    }

    // 7. 刪除 MCU 紀錄
    const btnMcuDel = el('btnMcuDelete');
    if (btnMcuDel) {
        btnMcuDel.onclick = async () => {
            if (confirm(t('confirm_delete_mcu_log'))) {
                await fetch('/api/delete_log');
                alert(t('log_deleted_success'));
            }
        };
    }

    // 8. 儲存空間更新按鈕
    const refreshBtn = el('refreshStorage');
    if (refreshBtn) {
        refreshBtn.onclick = () => {
            WSClient.send('get_fs_info');
            // 加上一個旋轉的視覺回饋
            refreshBtn.style.transition = 'transform 0.5s';
            refreshBtn.style.transform = 'rotate(360deg)';
            setTimeout(() => {
                refreshBtn.style.transform = 'rotate(0deg)';
            }, 500);
        };
    }
}

// --- 主題切換邏輯 ---
function initTheme() {
    const btn = document.getElementById('btnTheme');
    if (!btn) return;

    // 1. 判斷初始狀態 (優先讀取 localStorage，否則跟隨系統)
    const saved = localStorage.getItem('theme');
    const sysDark = window.matchMedia('(prefers-color-scheme: dark)').matches;
    let isDark = saved === 'dark' || (!saved && sysDark);

    const apply = (dark) => {
        document.body.classList.remove('light-mode', 'dark-mode');
        document.body.classList.add(dark ? 'dark-mode' : 'light-mode');
        btn.textContent = dark ? '🌙' : '☀️'; // 切換圖示
    };

    apply(isDark);

    btn.onclick = () => {
        isDark = !isDark;
        apply(isDark);
        localStorage.setItem('theme', isDark ? 'dark' : 'light');
    };
}

window.addEventListener('load', async () => {
    try {
        initTheme(); // 初始化主題
        if (typeof initLanguage === 'function') {
            await initLanguage(); // 等待語言載入並套用
        }
        
        // 翻譯載入後，設定標準 data-lang-key 無法觸及的元素
        document.title = t('app_title');
        const themeBtn = el('btnTheme');
        if(themeBtn) themeBtn.setAttribute('aria-label', t('theme_toggle_label'));

        log(t('log_initializing'));
        bindActions();
        // 使用新模組初始化，並在連線成功時自動查詢空間
        WSClient.init({
            onMessage: handleMessage,
            onOpen: () => {
                WSClient.send('get_fs_info');
            }
        });
    } catch (e) {
        console.error("Initialization failed", e);
    }
});

// 新增：集中計算衍生數據 (SOH, 顏色狀態)，避免重複邏輯
function calculateDerivedData(data) {
    // 1. SOH 計算
    const cycles = Number(data.charge_cycles) || 0;
    const overDis = Number(data.over_discharge) || 0;
    const overLoad = Number(data.over_load) || 0;
    const err04 = Number(data.err_cnt_04) || 0; // 過放錯誤 (嚴重)
    const err05 = Number(data.err_cnt_05) || 0; // 過熱錯誤 (嚴重)
    const err06 = Number(data.err_cnt_06) || 0; // 充電錯誤 (嚴重)
    const err07 = Number(data.err_cnt_07) || 0; // 過流錯誤 (嚴重)

    // 優化後的 SOH 算法：
    // 1. 循環次數：每 100 次扣 5% (假設壽命 2000 次)
    // 2. 過放紀錄 (歷史)：每 10 次扣 1% (這是正常使用耗損，權重降低)
    // 3. 過載紀錄 (歷史)：每 10 次扣 1%
    // 4. 錯誤計數 (嚴重)：每個錯誤扣 20% (這些才是導致鎖定的主因)
    let soh = 100;
    soh -= cycles * 0.05;
    soh -= overDis * 0.1;
    soh -= overLoad * 0.1;
    soh -= (err04 + err05 + err06 + err07) * 20;

    if (soh < 0) soh = 0;

    data.health_soh = soh.toFixed(0); // 注入字串
    data._sohColor = soh > 85 ? 'var(--success)' :
        soh > 60 ? 'var(--warning)' : // 改用變數
            'var(--warn)';

    // 2. 鎖定狀態顏色
    data._lockColor = 'inherit';
    // lock_status 現在是數字：0=正常(綠), >0=鎖定(紅)
    if (data.lock_status > 0) data._lockColor = 'var(--warn)';
    else data._lockColor = 'var(--success)';
}

// 數據表格渲染 (請對齊 Langs_TW.js 的 Key)
function renderDataTable(data) {
    const table = el('data-table');
    if (!table) return;

    // 組合翻譯鍵值：LOCK_0, LOCK_1...
    const lockVal = (data.lock_status !== undefined) ? t(`LOCK_${data.lock_status}`) : '--';

    // --- 2. 定義表格行數據 ---
    const rows = [
        { key: 'model', val: data.model || t('data_not_available') },
        { key: 'serial', val: data.serial || t('data_not_available') },
        { key: 'chip_rom_id', val: data.chip_rom_id || data.rom_id || t('data_not_available'), color: 'var(--primary)' }, // 改用變數
        { key: 'capacity', val: data.capacity || t('data_not_available') },
        { key: 'prod_date', val: data.prod_date || t('data_not_available') },
        { key: 'cycles', val: `${data.charge_cycles || 0} ${t('times')}`, color: 'var(--success)' },
        { key: 'lock_status', val: lockVal, color: data._lockColor }
    ];

    // --- 3. 渲染主表格 ---
    table.innerHTML = rows.map(r => `
        <div class="kv-item">
            <span class="key" data-lang-key="${r.key}">${t(r.key)}</span>
            <span class="value" style="color: ${r.color || 'inherit'};">${r.val}</span>
        </div>
    `).join('');
}

function renderAdvancedData(data) {
    const hasDynamic =
        Array.isArray(data.cell_voltages) &&
        data.cell_voltages.some(v => v > 0.1);

    if (!hasDynamic) return;

    // 2. 通用渲染邏輯：掃描所有帶有 data-field 的元素
    document.querySelectorAll('[data-field]').forEach(el => {
        const key = el.dataset.field;
        const val = data[key];

        // 如果數據不存在，跳過 (保持預設值或上次的值)
        if (val === undefined || val === null) return;

        // A. 特殊類型處理：保險絲 (Fuse)
        if (el.dataset.type === 'fuse') {
            const isBlown = (val === 1 || val === true);
            el.textContent = isBlown ? t('fuse_triggered') : t('fuse_ok');
            el.style.color = isBlown ? "var(--warn)" : "var(--success)";
            el.style.fontWeight = isBlown ? "bold" : "";
            return;
        }

        // B. 數值警告處理 (例如錯誤計數 > 0 變紅)
        if (el.dataset.warnGt) {
            const limit = parseFloat(el.dataset.warnGt);
            const isWarn = Number(val) > limit;
            el.style.color = isWarn ? 'var(--warn)' : '';
            el.style.fontWeight = isWarn ? 'bold' : '';
        }

        // C. 單位與翻譯處理
        let displayVal = val;
        if (el.dataset.unit) {
            // 嘗試翻譯單位 (例如 "times" -> "次")，如果沒有翻譯則直接顯示原單位 (例如 "°C")
            const unitKey = el.dataset.unit;
            const unitText = (typeof t === 'function') ? t(unitKey) : unitKey;

            // 修正：針對溫度欄位，格式化為小數點後一位
            if (key === 'temp1' || key === 'temp2' || key === 'temp3') {
                displayVal = `${parseFloat(val).toFixed(1)} ${unitText}`;
            } else {
                displayVal = `${val} ${unitText}`;
            }
        }

        // D. 支援動態顏色引用 (例如 SOH 的顏色)
        if (el.dataset.colorRef) {
            const colorVar = data[el.dataset.colorRef];
            if (colorVar) {
                el.style.color = colorVar;
                el.style.fontWeight = 'bold';
            }
        }

        // E. SOH 進度條視覺化 (新增)
        if (key === 'health_soh') {
            const pct = parseInt(val) || 0;
            const barColor = el.style.color || 'var(--success)';
            const flashClass = pct < 50 ? 'flash-warn' : ''; // < 50% 時加入閃爍 class
            el.innerHTML = `${displayVal}<div class="mini-progress-bar"><div class="mini-progress-fill ${flashClass}" style="width:${pct}%; background-color:${barColor}"></div></div>`;
        } else {
            el.textContent = displayVal;
        }
    });

    console.log("收到的 data:", data);
}


function handleMessage(event) {
    try {
        const msg = JSON.parse(event.data);
        let dataSummary = "";

        // 優化：忽略 presence 和 pong 訊息，避免干擾日誌
        if (msg.type === 'presence' || msg.type === 'pong') return;

        // --- 新增：處理後端回傳的狀態訊息 (結果與錯誤) ---
        if (msg.type === 'success') {
            log(`✅ ${t(msg.message)}`); // 3. 完成結果 (支援翻譯)
            resetAllButtons();
            return;
        } else if (msg.type === 'error') {
            log(`❌ ${t('log_error')}: ${t(msg.message)}`); // 4. 錯誤提示 (支援翻譯)
            resetAllButtons();
            return;
        } else if (msg.type === 'info') {
            log(`ℹ️ ${msg.message}`);
            return;
        } else if (msg.type === 'debug') {
            // 新增：顯示後端傳來的除錯/原始數據
            log(`🔧 ${msg.message}`);
            return;
        } else if (msg.type === 'fs_info') {
            const usedKB = (msg.used / 1024).toFixed(1);
            const totalKB = (msg.total / 1024).toFixed(1);
            const usedEl = el('storageUsed');
            const totalEl = el('storageTotal');
            if (usedEl) usedEl.textContent = usedKB;
            if (totalEl) totalEl.textContent = totalKB;
            log(`ℹ️ 檔案系統: 已使用 ${usedKB}KB / 共 ${totalKB}KB`);
            return;
        }
        // ------------------------------------------------

        // 優化：在渲染 UI 之前先重置按鈕 loading 狀態
        // 這樣可以避免 resetAllButtons 覆蓋掉 updateButtonStates 設定的正確狀態
        resetAllButtons();

        if (msg.type === 'static_data') {
            // 按鍵 1：只更新靜態部分，確保電壓欄位是空的或不被渲染
            lastData = { ...lastData, ...msg.data };
            lastFeatures = msg.features;
            // 整理靜態數據摘要
            const t_unknown = typeof t === 'function' ? t('unknown') : "Unknown";

            const model = msg.data.model || t_unknown;
            const serial = msg.data.serial || t_unknown;
            const rom = msg.data.rom_id || t_unknown;
            const cap = msg.data.capacity || t_unknown;
            const date = msg.data.prod_date || t_unknown;
            const cycles = msg.data.charge_cycles || 0;
            const lockCode = msg.data.lock_status;
            const lockVal = (typeof t === 'function' && lockCode !== undefined) ? t(`LOCK_${lockCode}`) : lockCode;
            const timesUnit = typeof t === 'function' ? t('times') : "";

            // 優化：顯示完整的解析結果 (名稱+數據)
            const details = [
                `${t('model')}: ${model}`, `${t('serial')}: ${serial}`, `${t('chip_rom_id')}: ${rom}`,
                `${t('capacity')}: ${cap}`, `${t('prod_date')}: ${date}`,
                `${t('cycles')}: ${cycles} ${timesUnit}`, `${t('lock_status')}: ${lockVal}`
            ];
            dataSummary = ` [${details.join(', ')}]`;

            // 強制清除可能殘留的舊電壓顯示邏輯（可選）
            renderUI(lastData, msg.features, 'static_data');
        }
        else if (msg.type === 'dynamic_data') {
            // 按鍵 2：疊加動態與進階數據
            lastData = { ...lastData, ...msg.data };

            // 修正：擴充動態數據日誌摘要，使其更完整
            const v1_val = msg.data.cell_voltages ? msg.data.cell_voltages[0].toFixed(2) : "--";
            const t1_val = msg.data.temp1 ? msg.data.temp1.toFixed(1) : "--";
            const t2_val = msg.data.temp2 ? msg.data.temp2.toFixed(1) : "--";
            const t3_val = msg.data.temp3 ? msg.data.temp3.toFixed(1) : "--";
            const od_val = msg.data.over_discharge !== undefined ? msg.data.over_discharge : "--";
            const ol_val = msg.data.over_load !== undefined ? msg.data.over_load : "--";
            const fuse_val = msg.data.fuse_blown ? "YES" : "NO";
            const errs_val = [
                msg.data.err_cnt_04,
                msg.data.err_cnt_05,
                msg.data.err_cnt_06,
                msg.data.err_cnt_07
            ].map(e => e !== undefined ? e : '-').join(',');

            dataSummary = ` [V1=${v1_val}V, T1=${t1_val}°C, T2=${t2_val}°C, T3=${t3_val}°C, OD=${od_val}, OL=${ol_val}, Err=[${errs_val}], Fuse=${fuse_val}]`;

            renderUI(lastData, lastFeatures, 'dynamic_data');

            // --- 記錄歷史數據 (用於 CSV 匯出) ---
            sessionHistory.push({
                ts: getFormattedTimestamp(), // 修正：統一時間格式
                ...lastData // 修正：儲存完整的合併後數據 (包含靜態和動態)
            });

            // 更新匯出按鈕狀態 (有數據變藍色)
            const btnExport = el('btnExport');
            if (btnExport && sessionHistory.length > 0) {
                btnExport.classList.remove('btn-gray');
                btnExport.classList.add('btn-blue');
            }
            // ----------------------------------
        }
        // 在介面運行日誌顯示
        log(`${t('log_data_received')} ${dataSummary}`); // 2. 讀取的數據 (簡化顯示)
        // resetAllButtons(); // 移至上方執行
    } catch (e) {
        log(`Error: ${e.message}`);
        resetAllButtons();
    }
}

function renderUI(data, features, msgType) {
    console.log("【渲染】開始，類型:", msgType);

    // 0️⃣ 預先計算衍生數據 (SOH, 顏色)
    calculateDerivedData(data);

    // 1️⃣ 基本表格
    try { renderDataTable(data); }
    catch (e) { console.error("DataTable 錯誤:", e); }

    // 2️⃣ 判斷顯示條件
    const isDynamic = (msgType === 'dynamic_data');
    const hasVolts =
        Array.isArray(data.cell_voltages) &&
        data.cell_voltages.some(v => v > 0.1);

    const shouldShowAdvanced = isDynamic || hasVolts;

    console.log("isDynamic:", isDynamic);
    console.log("hasVolts:", hasVolts);
    console.log("shouldShowAdvanced:", shouldShowAdvanced);

    // 3️⃣ 顯示主卡片
    const card = el('overviewCard');
    if (card) card.style.display = 'block';

    const advSec = el('advancedSection');
    const batteryContainer = document.querySelector('.battery-container');

    // 4️⃣ 進階區塊控制
    if (shouldShowAdvanced) {
        if (advSec) advSec.style.display = 'block';
        if (batteryContainer) batteryContainer.style.display = 'block';


        try { renderAdvancedData(data); }
        catch (e) { console.error("renderAdvancedData 錯誤:", e); }

        try { renderCells(data); }
        catch (e) { console.error("renderCells 錯誤:", e); }

    } else {
        if (advSec) advSec.style.display = 'none';
        if (batteryContainer) batteryContainer.style.display = 'none';
    }

    // 5️⃣ 更新按鈕狀態（只呼叫一次）
    if (features) {
        try { updateButtonStates(features, shouldShowAdvanced); }
        catch (e) { console.error("updateButtonStates 錯誤:", e); }
    }
    window.lastData = data;
}

// 輔助函數：設定按鈕顏色與狀態
function setBtnState(btn, colorClass, isEnabled) {
    if (!btn) return;
    btn.disabled = !isEnabled;
    // 清除舊顏色
    btn.classList.remove('btn-blue', 'btn-red', 'btn-yellow', 'btn-gray');
    // 設定新顏色 (如果啟用則用指定顏色，否則用灰色)
    btn.classList.add(isEnabled ? colorClass : 'btn-gray');
}

function updateButtonStates(features, shouldShowAdvanced) {
    // 修正：移除通用的“正在更新按鈕狀態”日誌，避免混淆。
    // 按鈕點擊時的具體操作已在 bindActions 中記錄。
    // 關鍵修正：對接 index.html 中真正的 ID
    const btnReadDynamic = el('btnReadDynamic');
    const btnLed = el('btnLed');
    const btnClear = el('btnClearErrors');
    const serviceBlock = el('serviceActions'); // 下方按鈕總區塊
    // 3. 更新按鈕 2 (更新數據) 的狀態
    // 按鍵 2：可用時變藍色
    setBtnState(btnReadDynamic, 'btn-blue', features.read_dynamic);

    // 4. 控制下方服務區塊 (清除故障/LED) 的顯示
    if (serviceBlock) {
        serviceBlock.style.display = shouldShowAdvanced ? 'block' : 'none';
    }
    // 5. 更新 LED 測試按鈕 (按鍵 4)：可用時變黃色
    setBtnState(btnLed, 'btn-yellow', features.led_test);

    // 6. 更新清除故障按鈕 (按鍵 3)：可用時變紅色
    setBtnState(btnClear, 'btn-red', features.clear_errors);
}




/**
 * 整合版電池視覺化渲染
 * @param {Object} data 完整的數據物件，包含 voltages 與 soc
 */
function renderCells(data) {
    const container = document.getElementById('cells-container');
    const summaryContainer = document.getElementById('packSummary');
    if (!container || !data) return;

    const voltages = data.cell_voltages || [];
    if (voltages.length === 0) {
        container.innerHTML = '';
        if (summaryContainer) summaryContainer.innerHTML = '';
        return;
    }

    // 1. 計算統計數據
    const maxV = Math.max(...voltages);
    const minV = Math.min(...voltages);
    const totalV = voltages.reduce((a, b) => a + b, 0);
    const diffV = maxV - minV;

    // 2. 渲染總結資訊 (注入到 packSummary)
    if (summaryContainer) {
        const diffColor = diffV > 0.050 ? 'var(--warn)' : 'var(--success)';
        const t_total = typeof t === 'function' ? t('total_voltage') : 'Total Voltage';
        const t_diff = typeof t === 'function' ? t('max_diff') : 'Max Diff';

        summaryContainer.innerHTML = `
            <div class="summary-item">
                <span class="label">${t_total}:</span>
                <span class="value">${totalV.toFixed(2)} V</span>
            </div>
            <div class="summary-item">
                <span class="label">${t_diff}:</span>
                <span class="value" style="color: ${diffColor}">${diffV.toFixed(3)} V</span>
            </div>
        `;
    }

    // 3. 渲染電芯圖示
    const html = voltages.map((v, i) => {
        const isReversed = (i % 2 !== 0);
        const pct = Math.max(0, Math.min(100, ((v - 2.5) / 1.7) * 100));

        // 顏色判斷
        let color = "var(--success)";
        if (v < 3.0) color = "var(--caution)"; // 改用變數 (黃色)
        if (v < 2.5) color = "var(--warn)"; // 紅色

        // 通用鎳片邏輯：偶數索引在上方連接下一顆，奇數索引在下方連接下一顆
        let bridgeClass = '';
        if (i < voltages.length - 1) { // 最後一顆不連
            bridgeClass = (i % 2 === 0) ? 'has-bridge-top' : 'has-bridge-bottom';
        }

        return `
            <div class="battery-cell-wrapper">
                <div class="cell-body ${isReversed ? 'downward' : 'upward'} ${bridgeClass}">
                    <div class="inner-label label-top ${isReversed ? 'neg-color' : 'pos-color'}">${isReversed ? '−' : '+'}</div>
                    <div class="inner-label label-bottom ${isReversed ? 'pos-color' : 'neg-color'}">${isReversed ? '+' : '−'}</div>
                    ${isReversed ? '' : '<div class="pos-tip tip-top"></div>'}
                    ${isReversed ? '<div class="pos-tip tip-bottom"></div>' : ''}
                    <div class="cell-fill" style="height: ${pct}%; background-color: ${color};"></div>
                    <div class="cell-voltage-text">${v.toFixed(3)}V</div>
                </div>
            </div>
        `;
    }
    ).join('');

    container.innerHTML = `<div class="battery-layout-box">${html}</div>`;
}

// --- CSV 匯出功能 ---
// 修正：重新整合，匯出更完整的數據
function exportToCSV() {
    if (sessionHistory.length === 0) {
        alert(t('err_no_history'));
        return;
    }

    // 擴充後的 CSV 欄位
    const headers = [
        t('csv_timestamp'), t('csv_model'), t('csv_serial'), t('csv_rom_id'), t('csv_capacity'), t('csv_prod_date'),
        t('csv_pack_voltage'),
        t('csv_cell_1'), t('csv_cell_2'), t('csv_cell_3'), t('csv_cell_4'), t('csv_cell_5'), t('csv_cell_diff'),
        t('csv_temp_1'), t('csv_temp_2'), t('csv_temp_3'),
        t('csv_status_code'), t('csv_lock_status'), t('csv_charge_cycles'),
        t('csv_over_discharge'), t('csv_over_load'),
        t('csv_err_04'), t('csv_err_05'), t('csv_err_06'), t('csv_err_07'),
        t('csv_fuse_blown'), t('csv_soh')
    ];

    // 轉換數據
    const rows = sessionHistory.map(d => {
        // 確保每一筆歷史數據都有 SOH 可以匯出
        calculateDerivedData(d);

        const cells = d.cell_voltages || [0, 0, 0, 0, 0];
        // 優化：更安全的 CSV 轉義函數
        const escapeCSV = (value) => {
            const val = (value === undefined || value === null) ? '' : String(value);
            return `"${val.replace(/"/g, '""')}"`;
        };

        return [
            `"${d.ts}"`,
            escapeCSV(d.model),
            escapeCSV(d.serial),
            escapeCSV(d.rom_id),
            escapeCSV(d.capacity),
            escapeCSV(d.prod_date),
            escapeCSV(d.pack_voltage),
            cells[0], cells[1], cells[2], cells[3], cells[4],
            escapeCSV(d.cell_diff),
            escapeCSV(d.temp1), escapeCSV(d.temp2), escapeCSV(d.temp3),
            escapeCSV(d.status_hex), // 使用十六進位狀態碼
            escapeCSV(d.lock_status),
            escapeCSV(d.charge_cycles),
            escapeCSV(d.over_discharge),
            escapeCSV(d.over_load),
            escapeCSV(d.err_cnt_04), escapeCSV(d.err_cnt_05), escapeCSV(d.err_cnt_06), escapeCSV(d.err_cnt_07),
            escapeCSV(d.fuse_blown),
            escapeCSV(d.health_soh)
        ].join(",");
    });

    // 組合 CSV 內容 (加入 BOM 以支援 Excel 中文顯示)
    let csvContent = "\uFEFF";
    csvContent += headers.join(",") + "\n" + rows.join("\n");

    // 觸發下載
    const blob = new Blob([csvContent], { type: 'text/csv;charset=utf-8;' });
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.setAttribute("href", url);
    link.setAttribute("download", `makita_bms_log_${new Date().toISOString().slice(0, 10)}.csv`);
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
}

// --- 通用工具 ---

function log(s) {
    const l = el('log'); if (!l) return;
    l.textContent += `[${new Date().toLocaleTimeString()}] ${s} \n`;
    // 優化：使用 requestAnimationFrame 確保 DOM 更新完成後才執行捲動
    requestAnimationFrame(() => {
        l.scrollTop = l.scrollHeight;
    });
}

function setButtonLoading(id, isLoading, langKey) {
    const b = el(id); if (!b) return;
    b.disabled = isLoading;
    if (isLoading) b.innerHTML = `<span class="spinner"></span> ${t(langKey) || '...'} `;
    else b.textContent = t(b.getAttribute('data-lang-key'));
}

function resetAllButtons() {
    ['btnReadStatic', 'btnReadDynamic', 'btnClearErrors', 'btnLed'].forEach(id => setButtonLoading(id, false));
}

function updateStatusText(key) { const s = el('statusText'); if (s) s.textContent = t(key); }