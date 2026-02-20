// src/main.cpp - ФИНАЛЬНАЯ ВЕРСИЯ
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include "FS.h"
#include "SPIFFS.h"
#include "MakitaBMS.h"
#include <HardwareSerial.h> // 強制包含硬體串口定義
#include <Update.h>
#if !defined(Serial)
#define Serial Serial
#endif
// --- 設定和全域物件 ---
#define ONEWIRE_PIN 4
#define ENABLE_PIN 5

bool enableVerifiedRead = false; // 除錯開關：預設關閉，由 Serial 輸入控制

// 優化 --- 狀態控制變數 ---
unsigned long lastHeartbeat = 0;  // 用於偵錯變數
bool shouldUpdateData = false;    // 控制是否需要執行讀取任務
bool shouldClearErrors = false;   // 控制是否需要清除錯誤
bool shouldReadStatic = false;    // 控制是否需要讀取靜態資料
bool skipMcuCsvLog = false;       // 新增：用於跳過單次 MCU CSV 紀錄的旗標
unsigned long lastUpdateTick = 0; // 用於計時自動更新
static BatteryData cached_data;   // 全域資料緩存，用於避免在請求動態資料時遺失靜態數據

// --- CSV 紀錄相關 ---
//const char *password = "12345678";   // 已關閉密碼，開放熱點Wi-Fi ，熱點密碼可由此設定
String currentClientTime = "";    // 儲存前端傳來的時間戳記
const char* LOG_PATH = "/datalog.csv";
const int MAX_LOG_LINES = 800;    // 最大紀錄筆數
// 原本
const char *ssid = "Makita_BMS_Tool";
DNSServer dnsServer;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
MakitaBMS bms(ONEWIRE_PIN, ENABLE_PIN); // 用於儲存靜態資料的全域緩存，以避免在請求動態資料時遺失靜態數據

// --- 前向宣告 (Forward Declarations) ---
void sendFeedback(const String &type, const String &message);
void sendPresence(bool is_present);
void logToClients(const String &message, LogLevel level);

/// --- 透過 WebSocket 傳送訊息給客戶端的函數 ---
void sendJsonResponse(const String &type, const BatteryData &data, const SupportedFeatures *features)
{
    if (ws.count() == 0)
        return;

    // 優化 1: 縮減緩衝區大小 (1024 bytes 對於目前的結構已足夠，節省 1KB Heap)
    DynamicJsonDocument doc(1024);
    doc["type"] = type;

    JsonObject dataObj = doc.createNestedObject("data");

    // --- 基礎資訊 ---
    dataObj["model"] = data.model;
    dataObj["serial"] = data.serial;
    dataObj["rom_id"] = data.rom_id;
    dataObj["fw_ver"] = data.fw_ver;
    dataObj["prod_date"] = data.prod_date;
    dataObj["capacity"] = data.capacity;
    dataObj["battery_type"] = data.battery_type;

    // --- 狀態與診斷 (文字 + 數字整合) ---
    // 優化：直接傳送數字，讓前端透過語言包翻譯 (LOCK_0, LOCK_1)
    dataObj["lock_status"] = data.lock_status;

    // 2. 狀態碼：
    // 為了配合你的 app.js (if (data.status_code))，我們統一 key 名稱
    dataObj["status_code"] = data.status_code_raw; // 傳送原始數字 (0, 10, 96...)
    dataObj["status_hex"] = data.status_code_hex;  // 保留備用的十六進位字串
    // 優化 2: 移除 status_raw (與 status_code 重複)，減少傳輸量

    // --- 計數器與健康指標 ---
    dataObj["charge_cycles"] = data.charge_cycles;
    dataObj["over_discharge"] = data.over_discharge;
    dataObj["over_load"] = data.over_load;
    dataObj["err_cnt_04"] = data.err_cnt_04;
    dataObj["err_cnt_05"] = data.err_cnt_05;
    dataObj["err_cnt_06"] = data.err_cnt_06;
    dataObj["err_cnt_07"] = data.err_cnt_07;
    dataObj["fuse_blown"] = data.fuse_blown;


    // --- 電壓與溫度數據 ---
    dataObj["pack_voltage"] = data.pack_voltage;
    JsonArray cellV = dataObj.createNestedArray("cell_voltages");
    for (int i = 0; i < 5; i++)
        cellV.add(data.cell_voltages[i]);

    dataObj["cell_diff"] = data.cell_diff;
    dataObj["temp1"] = data.temp1;
    dataObj["temp2"] = data.temp2;
    dataObj["temp3"] = data.temp3;

    // --- 功能支援標記 ---
    if (features)
    {
        JsonObject featuresObj = doc.createNestedObject("features");
        featuresObj["read_dynamic"] = features->read_dynamic;
        featuresObj["led_test"] = features->led_test;
        featuresObj["clear_errors"] = features->clear_errors;
    }

    String output;
    // 優化 3: 預先分配記憶體，避免序列化過程中的多次重分配 (Reallocation)
    output.reserve(1024);
    serializeJson(doc, output);
    ws.textAll(output);
}
// 封裝 WebSocket 通知邏輯

void notifyClients()
{

    // 這裡調用您原有的 sendJsonResponse
    // 第二個參數傳入緩存的 cached_data
    sendJsonResponse("dynamic_data", cached_data, nullptr);
}

// --- CSV 檔案處理函數 ---
void manageLogLimit() {
    if (!SPIFFS.exists(LOG_PATH)) return;

    File f = SPIFFS.open(LOG_PATH, "r");
    if (!f) return;

    // 簡單檢查：如果檔案太小，就不需要花時間算行數 (假設每行約 200 bytes, 800行約 160KB)
    if (f.size() < (MAX_LOG_LINES * 100)) {
        f.close();
        return;
    }

    int lines = 0;
    while (f.available()) {
        if (f.read() == '\n') lines++;
    }
    f.close();

    // 如果超過限制 (保留 header，所以是 MAX + 1)
    if (lines >= MAX_LOG_LINES) {
        Serial.println("[LOG] Log full, trimming oldest record...");
        SPIFFS.rename(LOG_PATH, "/datalog.tmp");
        File fIn = SPIFFS.open("/datalog.tmp", "r");
        File fOut = SPIFFS.open(LOG_PATH, "w");
        
        if (fIn && fOut) {
            // 1. 保留標頭
            String header = fIn.readStringUntil('\n');
            fOut.println(header);
            
            // 2. 丟棄第一筆資料 (最舊的)
            fIn.readStringUntil('\n');
            
            // 3. 複製剩餘資料
            while (fIn.available()) {
                fOut.write(fIn.read());
            }
        }
        if (fIn) fIn.close();
        if (fOut) fOut.close();
        SPIFFS.remove("/datalog.tmp");
    }
}

void appendToLog(const BatteryData &data, String ts) {
    // 1. 先檢查並處理容量限制
    manageLogLimit();

    // 修正：在開啟檔案前先檢查是否存在，確保標頭寫入邏輯正確
    bool isNewFile = !SPIFFS.exists(LOG_PATH);

    File f = SPIFFS.open(LOG_PATH, "a");
    if (!f) return;

    // 2. 如果是新檔案 (或空檔案)，寫入標頭
    if (isNewFile || f.size() == 0) {
        Serial.println("[LOG] Writing CSV Header...");
        const uint8_t BOM[] = {0xEF, 0xBB, 0xBF}; // 加入 UTF-8 BOM 解決 Excel 中文亂碼
        f.write(BOM, 3);
        f.println("Timestamp,Model,Serial,ROM ID,Capacity,Prod_Date,Pack Voltage,Cell 1,Cell 2,Cell 3,Cell 4,Cell 5,Cell Diff,Temp 1,Temp 2,Temp 3,Status Code,Lock Status,Charge Cycles,Over Discharge,Over Load,Err 04,Err 05,Err 06,Err 07,Fuse Blown,SOH (%)");
    }

    // 3. 計算 SOH (複製 JS 邏輯)
    float soh = 100.0;
    soh -= data.charge_cycles * 0.05;
    soh -= data.over_discharge * 0.1;
    soh -= data.over_load * 0.1;
    soh -= (data.err_cnt_04 + data.err_cnt_05 + data.err_cnt_06 + data.err_cnt_07) * 20.0;
    if (soh < 0) soh = 0;

    // 4. 寫入資料
    f.printf("\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",%.2f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f,%.1f,%.1f,\"%s\",%d,%d,%d,%d,%d,%d,%d,%d,%d,%.0f\n",
        ts.c_str(), data.model.c_str(), data.serial.c_str(), data.rom_id.c_str(), data.capacity.c_str(), data.prod_date.c_str(),
        data.pack_voltage, data.cell_voltages[0], data.cell_voltages[1], data.cell_voltages[2], data.cell_voltages[3], data.cell_voltages[4], data.cell_diff,
        data.temp1, data.temp2, data.temp3, data.status_code_hex.c_str(), data.lock_status, data.charge_cycles, data.over_discharge, data.over_load,
        data.err_cnt_04, data.err_cnt_05, data.err_cnt_06, data.err_cnt_07, data.fuse_blown, soh);
    f.close();
    Serial.println("[LOG] Data saved to SPIFFS.");
}

// 優化 修正後的 WebSocket 事件處理
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
    {
        data[len] = 0;
        Serial.printf("[DEBUG] WebSocket 原始收到資料: %s\n", (char *)data);

        DynamicJsonDocument doc(512);
        DeserializationError error = deserializeJson(doc, (char *)data);
        if (error)
            return;

        // --- 關鍵修正：將 "type" 改為 "command" ---
        String cmd = doc["command"];
        Serial.printf("[DEBUG] 解析後的指令類型: %s\n", cmd.c_str());

        // 擷取時間戳記 (如果有的話)
        if (doc.containsKey("timestamp")) {
            currentClientTime = doc["timestamp"].as<String>();
        }
        // 修正：若指令沒帶時間，保留舊值，避免變成 N/A

        if (cmd == "read_static")
        {
            shouldReadStatic = true;
            Serial.println("[DEBUG] 已標記 shouldReadStatic = true");
        }
        else if (cmd == "read_dynamic")
        {
            shouldUpdateData = true;
            Serial.println("[DEBUG] 已標記 shouldUpdateData = true");
        }
        else if (cmd == "clear_errors")
        {
            shouldClearErrors = true;
            Serial.println("[DEBUG] 已標記 shouldClearErrors = true");
        }
        else if (cmd == "led_on")
        {
            // 修正：不直接發送舊數據，而是觸發一次數據更新
            bms.ledTest(true);
            skipMcuCsvLog = true; // 標記下一次更新跳過紀錄
            shouldUpdateData = true;
        }
        else if (cmd == "led_off")
        {
            // 修正：不直接發送舊數據，而是觸發一次數據更新
            bms.ledTest(false);
            skipMcuCsvLog = true; // 標記下一次更新跳過紀錄
            shouldUpdateData = true;
        }
        else if (cmd == "ping")
        {
            // 回應心跳包，讓客戶端知道連線正常
            sendFeedback("pong", "OK");
            // Serial.println("[WS] Ping received, Pong sent."); // 可選：取消註解以在序列埠監控心跳
        }
        else if (cmd == "get_fs_info")
        {
            if (ws.count() > 0) {
                DynamicJsonDocument doc(256);
                doc["type"] = "fs_info";
                doc["total"] = SPIFFS.totalBytes();
                doc["used"] = SPIFFS.usedBytes();
                String output;
                serializeJson(doc, output);
                ws.textAll(output);
            }
        }
        else
        {
            Serial.println("[WARNING] 指令欄位匹配失敗！");
        }
    }
}

void sendFeedback(const String &type, const String &message)
{
    if (ws.count() == 0)
        return;
    DynamicJsonDocument doc(512);
    doc["type"] = type;
    doc["message"] = message;
    String output;
    serializeJson(doc, output);
    ws.textAll(output);
}

void sendPresence(bool is_present)
{
    if (ws.count() == 0)
        return;
    DynamicJsonDocument doc(64);
    doc["type"] = "presence";
    doc["present"] = is_present;
    String output;
    serializeJson(doc, output);
    ws.textAll(output);
}

void logToClients(const String &message, LogLevel level)
{
    Serial.println(message);
    String prefix = (level == LOG_LEVEL_DEBUG) ? "[DBG] " : "";
    sendFeedback("debug", prefix + message);
}

// 優化前
/*
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
    if (type == WS_EVT_CONNECT)
    {
        Serial.printf("WS client #%u connected\n", client->id());
        sendPresence(bms.isPresent());
    }
    else if (type == WS_EVT_DISCONNECT)
    {
        Serial.printf("WS client #%u disconnected\n", client->id());
    }
    else if (type == WS_EVT_DATA)
    {
        DynamicJsonDocument doc(256);
        if (deserializeJson(doc, (char *)data) != DeserializationError::Ok)
            return;

        String command = doc["command"];
        String error_msg;

        if (command == "presence")
        {
            sendPresence(bms.isPresent());
        }
        else if (command == "read_static")
        {
            BatteryData fresh_data;
            SupportedFeatures fresh_features;
            error_msg = bms.readStaticData(fresh_data, fresh_features);
            if (error_msg.indexOf("OK") != -1 || error_msg == "") // 💡 邏輯重構：不再比對字串內容，而是看它是否包含 "OK" 字樣
            {

                cached_data = fresh_data; // 只要回傳值包含 OK，就視為成功發送數據
                sendJsonResponse("static_data", cached_data, &fresh_features);
                sendPresence(true);
                sendFeedback("success", "Battery identified: " + fresh_data.model); // 同時發送一個成功的反饋給前端日誌
            }
            else
            {
                sendFeedback("error", error_msg); //  優化：顯示真正的錯誤原因（例如 "No response" 或 "Timeout"）
            }
        }
        else if (command == "read_dynamic")
        {
            error_msg = bms.readDynamicData(cached_data);
            if (error_msg == "")
            {
                sendJsonResponse("dynamic_data", cached_data, nullptr);
            }
            else
            {
                sendFeedback("error", error_msg);
            }
        }
        else if (command == "led_on")
        {
            error_msg = bms.ledTest(true);
            if (error_msg == "")
                sendFeedback("success", "LEDs ON command sent.");
            else
                sendFeedback("error", error_msg);
        }
        else if (command == "led_off")
        {
            error_msg = bms.ledTest(false);
            if (error_msg == "")
                sendFeedback("success", "LEDs OFF command sent.");
            else
                sendFeedback("error", error_msg);
        }
        else if (command == "clear_errors")
        {
            error_msg = bms.clearErrors();
            if (error_msg == "")
                sendFeedback("success", "Clear Errors command sent.");
            else
                sendFeedback("error", error_msg);
        }
        else if (command == "set_logging")
        {
            bool enabled = doc["enabled"];
            bms.setLogLevel(enabled ? LOG_LEVEL_DEBUG : LOG_LEVEL_INFO);
            logToClients(String("Log level set to ") + (enabled ? "DEBUG" : "INFO"), LOG_LEVEL_INFO);
        }
    }
}
*/

// 優化後
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
                      void *arg, uint8_t *data, size_t len)
{
    switch (type)
    {
    case WS_EVT_CONNECT:
        Serial.printf("WebSocket client #%u connected\n", client->id());
        break;
    case WS_EVT_DATA:
        handleWebSocketMessage(arg, data, len);
        break;
    default:
        break;
    }
}

class CaptiveRequestHandler : public AsyncWebHandler
{
public:
    CaptiveRequestHandler() {}
    virtual ~CaptiveRequestHandler() {}

    bool canHandle(AsyncWebServerRequest *request)
    {
        // 攔截所有非本機 IP 的請求 (例如 captive.apple.com, connectivitycheck.gstatic.com)
        // 使用 WiFi.softAPIP() 動態取得 IP，比寫死更靈活
        if (request->host() != WiFi.softAPIP().toString())
        {
            return true;
        }
        return false;
    }

    void handleRequest(AsyncWebServerRequest *request)
    {
        // 記錄攔截到的請求 (方便除錯 Apple CNA 行為)
        Serial.printf("[Captive] Redirecting %s%s to Web UI\n", request->host().c_str(), request->url().c_str());
        // 強制重導向到 ESP32 的 IP 根目錄
        request->redirect("http://" + WiFi.softAPIP().toString() + "/");
    }
};

void setup()
{
    // 1. 強制攔截所有不明請求並導向你的 IP (Captive Portal 核心)
    // 移除上方的 connecttest.txt 與 generate_204 處理，讓手機認為需要登入，從而觸發自動跳轉
    server.onNotFound([](AsyncWebServerRequest *request)
                      { request->redirect("http://" + WiFi.softAPIP().toString() + "/"); });
    Serial.begin(115200);

    // --- 新增：開機時詢問是否開啟雙重驗證 ---
    Serial.println("\n\n--- Boot Configuration ---");
    Serial.println("Enable Double-Read Verification? (Input 'Y' for Yes, 'N' for No)");
    Serial.println("Waiting 3 seconds... (Default: No)");
    
    unsigned long startWait = millis();
    bool inputReceived = false;
    while(millis() - startWait < 3000) {
        if(Serial.available()) {
            char c = Serial.read();
            if(c == 'Y' || c == 'y') {
                enableVerifiedRead = true;
                Serial.println("[Config] Double-Read Verification: ENABLED");
                inputReceived = true;
                break;
            } else if (c == 'N' || c == 'n') {
                enableVerifiedRead = false;
                Serial.println("[Config] Double-Read Verification: DISABLED");
                inputReceived = true;
                break;
            } else if (c == '\r' || c == '\n') {
                // 優化：偵測到 Enter 鍵時直接使用預設值 (關閉)，不需空等 3 秒
                Serial.println("[Config] Default selected (Enter): DISABLED");
                inputReceived = true; // 標記為已接收，避免後續顯示 Timeout
                break;
            }
        }
        delay(10);
    }
    
    if (!inputReceived) {
         Serial.println("[Config] Timeout. Using default: DISABLED");
    }
    
    // 將設定傳遞給 BMS 物件
    bms.setVerifyReads(enableVerifiedRead);
 
    Serial.println("\nStarting Makita BMS Tool...");

    if (!SPIFFS.begin(true))
    {
        Serial.println("An Error has occurred while mounting SPIFFS");
        return;
    }
    Serial.println("SPIFFS mounted successfully.");

    bms.setLogCallback(logToClients);

    WiFi.softAP(ssid); // 設定 WiFi.softAP(ssid, password); 
    Serial.print("Access Point '");
    Serial.print(ssid);
    Serial.print("' started at IP: ");
    Serial.println(WiFi.softAPIP());

    ws.onEvent(onWebSocketEvent);
    server.addHandler(&ws);

    server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
    
    // 修正：強制下載 CSV 檔案並指定編碼，解決直接開啟與亂碼問題
    server.on("/datalog.csv", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (SPIFFS.exists(LOG_PATH)) {
            AsyncWebServerResponse *response = request->beginResponse(SPIFFS, LOG_PATH, "text/csv; charset=utf-8");
            response->addHeader("Content-Disposition", "attachment; filename=\"datalog.csv\"");
            request->send(response);
        } else {
            request->send(404, "text/plain", "Log file not found");
        }
    });
    
    // 新增：刪除 CSV 檔案的 API
    server.on("/api/delete_log", HTTP_GET, [](AsyncWebServerRequest *request) {
        SPIFFS.remove(LOG_PATH);
        request->send(200, "text/plain", "Log deleted");
        Serial.println("[LOG] Log file deleted by user.");
    });

    dnsServer.start(53, "*", WiFi.softAPIP());
    server.addHandler(new CaptiveRequestHandler());

    server.begin();

    Serial.println("HTTP server with WebSocket is ready.");

    // --- 新增：自動掃描語言包 API ---
    // 前端呼叫此 API 時，會回傳 SPIFFS 中所有 "lang_*.json" 的檔案列表
    server.on("/api/langs", HTTP_GET, [](AsyncWebServerRequest *request) {
        String json = "[";
        File root = SPIFFS.open("/");
        File file = root.openNextFile();
        bool first = true;
        Serial.println("[SPIFFS] Scanning for language files:"); // Debug
        while (file) {
            String fname = String(file.name());
            // 統一檔名格式：確保以 / 開頭 (不同 ESP32 Core 版本行為可能不同)
            if (!fname.startsWith("/")) fname = "/" + fname;
            
            Serial.printf("  Found: %s (%d bytes)\n", fname.c_str(), file.size()); // Debug

            if (fname.startsWith("/lang_") && fname.endsWith(".json")) {
                if (!first) json += ",";
                json += "\"" + fname + "\"";
                first = false;
            }
            file = root.openNextFile();
        }
        json += "]";
        Serial.print("[SPIFFS] Response: "); Serial.println(json); // Debug
        request->send(200, "application/json", json);
    });

    // --- 新增：OTA 韌體更新處理 ---
    server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {
        // 上傳完成後的回應
        bool shouldReboot = !Update.hasError();
        AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", shouldReboot ? "OK" : "FAIL");
        response->addHeader("Connection", "close");
        request->send(response);
        if (shouldReboot) {
            delay(100);
            ESP.restart();
        }
    }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        // 處理上傳過程
        if (!index) {
            Serial.printf("Update Start: %s\n", filename.c_str());
            // 如果檔名是 spiffs.bin 則更新檔案系統，否則更新韌體
            int cmd = (filename == "spiffs.bin") ? U_SPIFFS : U_FLASH;
            if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) {
                Update.printError(Serial);
            }
        }
        if (Update.write(data, len) != len) {
            Update.printError(Serial);
        }
        if (final) {
            if (Update.end(true)) {
                Serial.printf("Update Success: %uB\n", index + len);
            } else {
                Update.printError(Serial);
            }
        }
    });
}

// 優化前
/*
void loop()
{
    dnsServer.processNextRequest();
}
    */

// 優化後
void loop()
{
    // 1. 核心網路任務
    dnsServer.processNextRequest();
    ws.cleanupClients();

    // 2. 處理「1. 讀取資訊」(靜態)
    if (shouldReadStatic)
    {
        shouldReadStatic = false;
        Serial.println("[COM3] >>> 開始執行 readStaticData...");

        SupportedFeatures features;
        // 這裡建議統一使用 cached_data，除非您有特殊用途
        String res = bms.readStaticData(cached_data, features);

        if (res.indexOf("OK") != -1) // 檢查是否成功
        {
            delay(20);
            sendJsonResponse("static_data", cached_data, &features);
            sendFeedback("success", "log_static_success"); // 發送成功提示 (Key)
            Serial.println("[COM3] <<< 靜態資訊推送完成");
        }
        else
        {
            sendFeedback("error", res); // 發送錯誤提示
        }
    }

    // 3. 處理「2. 更新數據」(動態)
    if (shouldUpdateData)
    {
        shouldUpdateData = false;
        Serial.println("[COM3] >>> 開始執行 readDynamicData...");

        String err = "";
        // 1. 讀取電壓、溫度、循環次數 (33h 指令)
        String res = bms.readDynamicData(cached_data);
        if (res != "") err = res;

        // 2. 【關鍵！】讀取進階診斷：錯誤 04, 05, 07 與熔絲 (11h/EEPROM 指令)
        // 如果沒有這行，你的前端 mapping ['err04', 'err05'...] 就會拿不到值
        // 注意：如果 readDynamicData 已經失敗，這裡可能也會失敗，但我們還是嘗試讀取
        bms.readAdvancedDiagnostics(cached_data);

        // 3. 在 Serial 印出獲取的數據摘要，方便 Debug
        if (cached_data.cell_voltages[0] > 0.1)
        {
            char buf[128]; // 增加緩衝區以容納更多溫度數據
            // 優化：顯示完整診斷資訊 (Err04-07, Temp, Fuse)
            // 修正：確保日誌中包含 T1, T2, T3
            sprintf(buf, "Data OK: V1=%.2fV, T1=%.1fC, T2=%.1fC, T3=%.1fC, OD=%d, OL=%d, Err=[%d,%d,%d,%d], Fuse=%s",
                cached_data.cell_voltages[0], cached_data.temp1, cached_data.temp2, cached_data.temp3,
                cached_data.over_discharge, cached_data.over_load,
                cached_data.err_cnt_04, cached_data.err_cnt_05, cached_data.err_cnt_06, cached_data.err_cnt_07,
                cached_data.fuse_blown ? "YES" : "NO");
            logToClients(String(buf), LOG_LEVEL_INFO);
        }
        else
        {
            Serial.println("[COM3] ⚠️ 數據獲取異常: 電壓為 0，請檢查連接");
        }

        if (err == "")
        {
            delay(20);
            // 修正：將 "dynamic_update" 改為 "dynamic_data" 以匹配 app.js
            sendJsonResponse("dynamic_data", cached_data, nullptr);
            sendFeedback("success", "log_dynamic_success"); // 補上成功提示
            
            // 新增：讀取成功後，寫入 CSV 到 MCU
            if (!skipMcuCsvLog) {
                appendToLog(cached_data, currentClientTime);
            }
            skipMcuCsvLog = false; // 無論是否寫入，都重置旗標

            Serial.println("[COM3] <<< 動態數據推送完成");
        }
        else
        {
            sendFeedback("error", err);
        }
    }

    // 4. 處理「清除錯誤」
    if (shouldClearErrors)
    {
        shouldClearErrors = false;
        Serial.println("[COM3] >>> 執行清除錯誤程序...");

        String res = bms.clearErrors();
        if (res == "")
        {
            // 清除後刷新數據
            bms.readDynamicData(cached_data);
            // 修正：補上進階數據讀取，確保 temp3 等數據在清除後能被刷新
            bms.readAdvancedDiagnostics(cached_data);

            delay(20);
            // 修正：同樣改為 "dynamic_data"
            sendJsonResponse("dynamic_data", cached_data, nullptr);
            sendFeedback("success", "log_clear_success"); // 明確告知清除成功 (Key)
            Serial.println("[COM3] <<< 清除指令完成");
        }
        else
        {
            sendFeedback("error", res);
        }
    }

    yield();
}