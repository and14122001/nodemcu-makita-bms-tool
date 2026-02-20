/**
 * 共用常數與工具函數
 */

// 簡化 document.getElementById 的寫法
const el = id => document.getElementById(id);

/**
 * 獲取格式化的時間戳記 (YYYY/MM/DD HH:mm:ss)
 */
function getFormattedTimestamp() {
    const d = new Date();
    return d.toLocaleString('sv-SE', { hour12: false }).replace(/-/g, '/');
}