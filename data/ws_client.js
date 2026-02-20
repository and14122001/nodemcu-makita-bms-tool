/**
 * WebSocket Client Module
 * 負責處理連線生命週期、自動重連、狀態顯示與指令發送
 */
const WSClient = {
    ws: null,
    messageHandler: null,
    openHandler: null,
    heartbeatInterval: null,

    // 初始化並開始連線
    init(options) {
        this.messageHandler = options.onMessage;
        this.openHandler = options.onOpen;
        this.connect();
    },

    connect() {
        this.updateStatus('ws_connecting', 'status-warn');
        this.ws = new WebSocket(`ws://${location.host}/ws`);
        
        this.ws.onopen = () => {
            console.log("WebSocket 已連線");
            this.updateStatus('ws_connected', 'status-ok');
            if (this.openHandler) this.openHandler();
            this.startHeartbeat();
        };

        this.ws.onmessage = (event) => {
            if (this.messageHandler) this.messageHandler(event);
        };

        this.ws.onclose = () => {
            console.log("WebSocket 已關閉，將在 3 秒後嘗試重新連線...");
            this.updateStatus('ws_disconnected', 'status-error');
            this.stopHeartbeat();
            this.ws = null;
            setTimeout(() => this.connect(), 3000);
        };
        
        this.ws.onerror = (err) => {
             console.error("WebSocket Error:", err);
             this.updateStatus('ws_error', 'status-error');
        };
    },

    // 發送指令 (自動附加時間戳記)
    send(cmd, payload = {}) {
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            const data = {
                command: cmd,
                timestamp: getFormattedTimestamp(), // 來自 constants.js
                ...payload
            };
            this.ws.send(JSON.stringify(data));
        } else {
            console.warn("WebSocket not ready, command ignored:", cmd);
        }
    },

    // 開始心跳包
    startHeartbeat() {
        this.stopHeartbeat(); // 先停止可能存在的計時器
        this.heartbeatInterval = setInterval(() => {
            this.send('ping');
        }, 30000); // 每 30 秒發送一次
        console.log("Heartbeat started.");
    },

    // 停止心跳包
    stopHeartbeat() {
        clearInterval(this.heartbeatInterval);
        this.heartbeatInterval = null;
        console.log("Heartbeat stopped.");
    },

    // 更新 UI 狀態燈
    updateStatus(key, colorClass) {
        const statusWidget = el('statusWidget');
        const statusText = el('statusText');
        if (!statusWidget || !statusText) return;

        // 嘗試翻譯，若無 t() 則顯示 key
        statusText.textContent = (typeof t === 'function') ? t(key) : key;

        statusWidget.className = 'header-status'; // 重置
        if (colorClass) {
            statusWidget.classList.add(colorClass);
        }
    }
};