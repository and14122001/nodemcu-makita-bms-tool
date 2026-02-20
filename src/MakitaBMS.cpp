#include "MakitaBMS.h"

MakitaBMS::MakitaBMS(uint8_t onewire_pin, uint8_t enable_pin)
    : makita(onewire_pin), _enable_pin(enable_pin)
{
    pinMode(_enable_pin, OUTPUT);
    digitalWrite(_enable_pin, HIGH); // NPN: HIGH = OFF
}

// --- 工具函數 ---

void MakitaBMS::setLogCallback(LogCallback callback) { _log = callback; }
void MakitaBMS::setLogLevel(LogLevel level) { _logLevel = level; }
void MakitaBMS::logger(const String &message, LogLevel level)
{  
    if (_log && level <= _logLevel)
        _log(message, level);
}

void MakitaBMS::setVerifyReads(bool on) {
 _verifyReads = on;
 Serial.print("setVerifyReads");
 Serial.println(on ? "true" : "false");
}

void MakitaBMS::log_hex(const String &prefix, const byte *data, int len)
{
    if (_logLevel < LOG_LEVEL_DEBUG)
        return;
    String hex_str = prefix;
    if (data && len > 0)
    {
        for (int i = 0; i < len; i++)
        {
            char buf[4];
            sprintf(buf, "%02X ", data[i]);
            hex_str += buf;
        }
    }
    logger(hex_str, LOG_LEVEL_DEBUG);
}

byte MakitaBMS::nibble_swap(byte b)
{
    return ((b & 0xF0) >> 4) | ((b & 0x0F) << 4);
}

void MakitaBMS::cmd_and_read_cc(const byte *cmd, uint8_t cmd_len, byte *rsp, uint8_t rsp_len)
{
    makita.reset();
    delayMicroseconds(400);
    makita.write(0xcc);
    if (cmd != nullptr)
    {
        for (int i = 0; i < cmd_len; i++)
        {
            makita.write(cmd[i]);
            delayMicroseconds(90);
        }
    }
    if (rsp != nullptr)
    {
        for (int i = 0; i < rsp_len; i++)
        {
            rsp[i] = makita.read();
            delayMicroseconds(90);
        }
    }
}

void MakitaBMS::cmd_and_read_33(const byte *cmd, uint8_t cmd_len, byte *rsp, uint8_t rsp_len)
{
    makita.reset();
    delayMicroseconds(400);
    makita.write(0x33);

    byte initial_read[8];
    for (int i = 0; i < 8; i++)
    {
        initial_read[i] = makita.read();
        delayMicroseconds(90);
    }

    if (cmd != nullptr)
    {
        for (int i = 0; i < cmd_len; i++)
        {
            makita.write(cmd[i]);
            delayMicroseconds(90);
        }
    }
    if (rsp != nullptr)
    {
        for (int i = 0; i < rsp_len; i++)
        {
            rsp[i] = makita.read();
            delayMicroseconds(90);
        }
    }
}

bool MakitaBMS::isPresent()
{
    digitalWrite(_enable_pin, LOW); // ON
    delay(400);
    bool present = makita.reset();
    digitalWrite(_enable_pin, HIGH); // OFF
    return present;
}

// --- 靜態數據讀取 ---
String MakitaBMS::readStaticData(BatteryData &data, SupportedFeatures &features)
{
    logger("--- NEW Starting Static Data Sync ---", LOG_LEVEL_INFO);
    _is_identified = false;
    digitalWrite(_enable_pin, LOW);
    delay(400);

    const byte read_cmd[] = {0xAA, 0x00};
    byte full_resp[40];

    if (makita.reset())
    {
        makita.write(0x33);
        for (int i = 0; i < 8; i++)
        {
            full_resp[i] = makita.read();
            delayMicroseconds(90);
        }
        for (int i = 0; i < 2; i++)
        {
            makita.write(read_cmd[i]);
            delayMicroseconds(90);
        }
        for (int i = 8; i < 40; i++)
        {
            full_resp[i] = makita.read();
            delayMicroseconds(90);
        }
    }
    else
    {
        digitalWrite(_enable_pin, HIGH);
        return "Reset failed";
    }

    // ... 前段讀取 full_resp[40] 保持不變 ...

    log_hex("RAW_33_FULL: ", full_resp, 40);

    char buf[16]; // 稍微加大緩衝區確保安全

    // 1. 製造日期: 前 3 Byte [0]=年, [1]=月, [2]=日
    sprintf(buf, "%02d/%02d/20%02d", full_resp[2], full_resp[1], full_resp[0]);
    data.prod_date = String(buf);

    // 2. 基本資訊：容量與電壓類型
    data.capacity = String(nibble_swap(full_resp[24]) / 10.0f, 1) + "Ah";
    data.battery_type = String(nibble_swap(full_resp[19])) + "V"; // 顯示如 18V

    // 3. 狀態碼與鎖定狀態 (對標 Status Code & State)
    data.status_code_raw = full_resp[27]; // 存儲原始數值
    char s_code[8];
    sprintf(s_code, "0x%02X", full_resp[27]);
    data.status_code_hex = String(s_code); // 修正成員名稱為 status_code_hex
    
    // 修正：更精確地解析鎖定狀態，對應 BatteryData 結構中的定義
    // 0:正常, 1:永久鎖定(熔斷), 2:過熱暫時鎖定, 3:過放電鎖定
    uint8_t lock_code = full_resp[28] & 0x0F;
    switch(lock_code) {
        case 0x00:
            data.lock_status = 0; // 正常
            break;
        case 0x01:
            data.lock_status = 3; // 過放電鎖定
            break;
        case 0x02:
            data.lock_status = 2; // 過熱暫時鎖定
            break;
        case 0x03:
            data.lock_status = 1; // 永久鎖定(熔斷)
            break;
        default: // 其他未知值也視為鎖定
            data.lock_status = 1;
            break;
    }

    // 4. 循環次數 (對標 Charge count*)
    // 使用 c_low / c_high 組合，並移除後段重複定義的代碼
    uint16_t c_low = nibble_swap(full_resp[35]);
    uint16_t c_high = nibble_swap(full_resp[36]);
    data.charge_cycles = ((c_high << 8) | c_low) & 0x0FFF;

    // 5. 異常紀錄 (對標診斷儀：過放、過載)
    data.over_discharge = full_resp[37];
    data.over_load = full_resp[38];

    // 6. 身份識別 (ROM ID 與 序號替代方案)
    String rom_str = "";
    for (int i = 0; i < 8; i++)
    {
        char r_buf[3];
        sprintf(r_buf, "%02X", full_resp[i]);
        rom_str += r_buf;
    }
    data.rom_id = rom_str;
    data.serial = "ID-" + rom_str.substring(rom_str.length() - 6);
    // --- 識別控制器型號 ---
    _controller_type = "UNKNOWN";
    String model_str = getModel();
    if (model_str != "")
    {
        _controller_type = "STANDARD";
        data.model = model_str;
    }
    else
    {
        model_str = getF0513Model();
        if (model_str != "")
        {
            _controller_type = "F0513";
            data.model = model_str;
        }
        else
        {
            // 💡 修正處：如果都找不到，給它一個預設型號，不要直接跳出
            _controller_type = "STANDARD";
            data.model = "GENERIC_MAKITA";
            logger("Unknown model string, forcing STANDARD mode", LOG_LEVEL_WARN);
        }
    }
    _is_identified = true;        // 強制標記為已識別
    features.read_dynamic = true; // 開啟動態更新功能
    if (_controller_type == "STANDARD")
    {
        features.led_test = true;
        features.clear_errors = true;
    }
    // 修正：移除此處的呼叫。此呼叫會與外部的電源管理衝突，導致通訊失敗。
    // readAdvancedDiagnostics(data); 
    digitalWrite(_enable_pin, HIGH);
    return "OK_NEW_LOGIC";
}

// --- 動態數據讀取 ---
String MakitaBMS::readDynamicData(BatteryData &data)
{
    if (!_is_identified)
        return "Identify battery first.";

    // 建立分流機制，避免時序錯亂
    if (_controller_type == "STANDARD")
    {
        return readDynamicDataStandard(data);
    }
    else if (_controller_type == "F0513")
    {
        return readDynamicDataF0513(data);
    }
    return "Unknown Controller Type";
}

// STANDARD 專用動態讀取
String MakitaBMS::readDynamicDataStandard(BatteryData &data)
{
    digitalWrite(_enable_pin, LOW);
    delay(400);
    byte resp[29];
    const byte dyn_cmd[] = {0xD7, 0x00, 0x00, 0xFF};
    cmd_and_read_cc(dyn_cmd, 4, resp, sizeof(resp));

    // 新增：將讀取到的原始動態數據輸出到日誌
    log_hex("RAW_DYN_STD: ", resp, sizeof(resp));

    data.pack_voltage = ((resp[1] << 8) | resp[0]) / 1000.0f;
    float min_v = 5.0, max_v = 0.0;
    for (int i = 0; i < 5; i++)
    {
        float v = ((resp[i * 2 + 3] << 8) | resp[i * 2 + 2]) / 1000.0f;
        data.cell_voltages[i] = v;
        if (v > 0.5 && v < min_v) min_v = v;
        if (v > max_v) max_v = v;
    }
    data.cell_diff = (max_v > min_v) ? (max_v - min_v) : 0.0;
    data.temp1 = ((resp[15] << 8) | resp[14]) / 100.0f;
    data.temp2 = ((resp[17] << 8) | resp[16]) / 100.0f;
    digitalWrite(_enable_pin, HIGH);
    return "";
}

// F0513 專用動態讀取 (獨立機制)
String MakitaBMS::readDynamicDataF0513(BatteryData &data)
{
    digitalWrite(_enable_pin, LOW);
    delay(400); // F0513 可能需要不同的喚醒延遲，這裡暫時保持一致，但已隔離
    byte resp[29];
    const byte dyn_cmd[] = {0xD7, 0x00, 0x00, 0xFF};
    cmd_and_read_cc(dyn_cmd, 4, resp, sizeof(resp));

    // 新增：將讀取到的原始動態數據輸出到日誌
    log_hex("RAW_DYN_F0513: ", resp, sizeof(resp));

    data.pack_voltage = ((resp[1] << 8) | resp[0]) / 1000.0f;
    // F0513 的數據解析邏輯與 Standard 相同
    float min_v = 5.0, max_v = 0.0;
    for (int i = 0; i < 5; i++) {
        float v = ((resp[i * 2 + 3] << 8) | resp[i * 2 + 2]) / 1000.0f;
        data.cell_voltages[i] = v;
        if (v > 0.5 && v < min_v) min_v = v;
        if (v > max_v) max_v = v;
    }
    data.cell_diff = (max_v > min_v) ? (max_v - min_v) : 0.0;
    data.temp1 = ((resp[15] << 8) | resp[14]) / 100.0f;
    // 修正：補上 temp2 讀取
    data.temp2 = ((resp[17] << 8) | resp[16]) / 100.0f;
    digitalWrite(_enable_pin, HIGH);
    return "";
}

void MakitaBMS::readAdvancedDiagnostics(BatteryData &data)
{
    // 分流處理：確保不同控制器的進階診斷邏輯互不干擾
    if (_controller_type == "STANDARD")
    {
        readAdvancedDiagnosticsStandard(data);
    }
    else if (_controller_type == "F0513")
    {
        readAdvancedDiagnosticsF0513(data);
    }
}

void MakitaBMS::readAdvancedDiagnosticsStandard(BatteryData &data) {
    // 修正：在執行通訊前，確保電池電源已開啟
    digitalWrite(_enable_pin, LOW);
    delay(400);

    // 1. 進入第二指令樹 (存取隱藏暫存器)
    const byte enter_tree2[] = {0x99};
    cmd_and_read_cc(enter_tree2, 1, nullptr, 0);
    delay(150); // 修正：增加延遲，提高對不同電池的相容性

    // 2. 讀取原始數據
    uint8_t real_over_discharge = readOneWireByte(0x08);
    uint8_t real_over_load = readOneWireByte(0x09);
    uint8_t val04 = readOneWireByte(0x04);
    uint8_t val05 = readOneWireByte(0x05);
    uint8_t val06 = readOneWireByte(0x06);
    uint8_t val07 = readOneWireByte(0x07);
    uint8_t f_val = readOneWireByte(0x0C);
    uint8_t val_fw = readOneWireByte(0x32); // Martin 提到的固件版本位址
    uint8_t temp3_raw = readOneWireByte(0x0A); // 修正：讀取第三溫度地址 0x0A
 
    // 3. 過濾 255 亂碼並賦值給結構體
    // 在 1-Wire 中，255 (0xFF) 通常代表讀取失敗或位址不匹配
    data.over_discharge = (real_over_discharge == 255) ? 0 : real_over_discharge;
    data.over_load = (real_over_load == 255) ? 0 : real_over_load;
    data.err_cnt_04 = (val04 == 255) ? 0 : val04;
    data.err_cnt_05 = (val05 == 255) ? 0 : val05;
    data.err_cnt_06 = (val06 == 255) ? 0 : val06;
    data.err_cnt_07 = (val07 == 255) ? 0 : val07;
    data.fw_ver = (val_fw == 255) ? 0 : val_fw;
    // 新增：處理第三溫度
    // 0xFF 代表讀取失敗。原始值減 100 是常見的轉換公式。
    if (temp3_raw != 255)
    {
        data.temp3 = (float)temp3_raw - 100.0f;
    }
 
    // 4. 判定軟體保險絲狀態 (核心邏輯)
    long s_num = data.status_code_raw;
    // 判定條件：有值 (f_val > 0) 且 不是通訊失敗 (f_val != 255) 且 狀態碼異常 (s_num != 0x60)
    if (f_val > 0 && f_val != 255 && s_num != 0x60)
    {
        data.fuse_blown = 1; // 真正的鎖定狀態
    }
    else
    {
        data.fuse_blown = 0; // 健康電池或新版協議誤讀，視為正常
    }

    // 5. 序列號輸出偵錯資訊 (方便觀察新電池版本)
    if (val_fw != 255)
    {
        Serial.printf("[BMS] Advanced Diagnostic - FW Ver: %02X, FuseRaw: %02X, Status: %02X\n",
                      val_fw, f_val, (uint8_t)s_num);
    }

    // 6. 退出第二指令樹，回到主面板
    const byte exit_cmd[] = {0xF0, 0x00};
    cmd_and_read_cc(exit_cmd, 2, nullptr, 0);

    // 修正：通訊結束後，關閉電池電源
    digitalWrite(_enable_pin, HIGH);
}

void MakitaBMS::readAdvancedDiagnosticsF0513(BatteryData &data) {
    // F0513 專用進階診斷邏輯 (目前結構與 Standard 相同，但獨立封裝以便未來調整時序)
    
    // 修正：在執行通訊前，確保電池電源已開啟
    digitalWrite(_enable_pin, LOW);
    delay(400);

    // 1. 進入第二指令樹
    const byte enter_tree2[] = {0x99};
    cmd_and_read_cc(enter_tree2, 1, nullptr, 0);
    delay(150); // 修正：增加延遲，同步 Standard 版本的修改

    // 2. 讀取原始數據
    uint8_t real_over_discharge = readOneWireByte(0x08);
    uint8_t real_over_load = readOneWireByte(0x09);
    uint8_t val04 = readOneWireByte(0x04);
    uint8_t val05 = readOneWireByte(0x05);
    uint8_t val06 = readOneWireByte(0x06);
    uint8_t val07 = readOneWireByte(0x07);
    uint8_t f_val = readOneWireByte(0x0C);
    uint8_t val_fw = readOneWireByte(0x32);
    uint8_t temp3_raw = readOneWireByte(0x0A); // 新增：讀取第三溫度

    // 3. 過濾與賦值
    data.over_discharge = (real_over_discharge == 255) ? 0 : real_over_discharge;
    data.over_load = (real_over_load == 255) ? 0 : real_over_load;
    data.err_cnt_04 = (val04 == 255) ? 0 : val04;
    data.err_cnt_05 = (val05 == 255) ? 0 : val05;
    data.err_cnt_06 = (val06 == 255) ? 0 : val06;
    data.err_cnt_07 = (val07 == 255) ? 0 : val07;
    data.fw_ver = (val_fw == 255) ? 0 : val_fw;
    // 新增：處理第三溫度
    if (temp3_raw != 255)
    {
        data.temp3 = (float)temp3_raw - 100.0f;
    }

    // 4. 判定軟體保險絲狀態
    long s_num = data.status_code_raw;
    if (f_val > 0 && f_val != 255 && s_num != 0x60)
    {
        data.fuse_blown = 1;
    }
    else
    {
        data.fuse_blown = 0;
    }

    // 5. 輸出偵錯資訊
    if (val_fw != 255)
    {
        // 使用 F0513 專屬標籤方便區分
        Serial.printf("[F0513] Adv Diag - FW: %02X, FuseRaw: %02X, Status: %02X\n",
                      val_fw, f_val, (uint8_t)s_num);
    }

    // 6. 退出第二指令樹
    const byte exit_cmd[] = {0xF0, 0x00};
    cmd_and_read_cc(exit_cmd, 2, nullptr, 0);

    // 修正：通訊結束後，關閉電池電源
    digitalWrite(_enable_pin, HIGH);
}

// 輔助函數：讀取特定指令回傳的位元組
uint8_t MakitaBMS::readOneWireByte(byte cmd)
{
    makita.reset();
    makita.write(0xCC);
    makita.write(cmd);
    return makita.read();
}

String MakitaBMS::getModel()
{
    byte resp[16];
    const byte model_cmd[] = {0xDC, 0x0C};
    cmd_and_read_cc(model_cmd, 2, resp, sizeof(resp));
    if (resp[0] == 0xFF || resp[0] == 0x00)
        return "";
    char m[8];
    memcpy(m, resp, 7);
    m[7] = '\0';
    return String(m);
}

String MakitaBMS::getF0513Model()
{
    const byte f_cmd[] = {0x99};
    cmd_and_read_cc(f_cmd, 1, nullptr, 0);
    delay(100);
    makita.reset();
    makita.write(0xCC); // 修正：遵循 1-Wire 協議，發送 Skip ROM
    makita.write(0x31); // 然後才發送功能指令
    byte r[2];
    r[0] = makita.read();
    r[1] = makita.read();
    const byte clr[] = {0xF0, 0x00};
    cmd_and_read_cc(clr, 2, nullptr, 0);
    if (r[0] == 0xFF)
        return "";
    char buf[8];
    sprintf(buf, "BL%02X%02X", r[1], r[0]);
    return String(buf);
}

String MakitaBMS::ledTest(bool on)
{
    if (!_is_identified) return "N/A";
    
    if (_controller_type == "STANDARD") return ledTestStandard(on);
    if (_controller_type == "F0513") return ledTestF0513(on);
    
    return "N/A";
}

String MakitaBMS::ledTestStandard(bool on)
{
    digitalWrite(_enable_pin, LOW);
    delay(400);
    byte dummy[9];
    const byte unlock_cmd[] = {0xD9, 0x96, 0xA5};
    cmd_and_read_33(unlock_cmd, 3, dummy, 9);
    const byte action_cmd[] = {0xDA, (byte)(on ? 0x31 : 0x34)};
    cmd_and_read_33(action_cmd, 2, dummy, 9);
    digitalWrite(_enable_pin, HIGH);
    return "";
}

String MakitaBMS::ledTestF0513(bool on)
{
    // F0513 獨立 LED 控制邏輯
    digitalWrite(_enable_pin, LOW);
    delay(400);
    byte dummy[9];
    const byte unlock_cmd[] = {0xD9, 0x96, 0xA5};
    cmd_and_read_33(unlock_cmd, 3, dummy, 9);
    const byte action_cmd[] = {0xDA, (byte)(on ? 0x31 : 0x34)};
    cmd_and_read_33(action_cmd, 2, dummy, 9);
    digitalWrite(_enable_pin, HIGH);
    return "";
}

String MakitaBMS::clearErrors()
{
    if (!_is_identified) return "N/A";

    if (_controller_type == "STANDARD") return clearErrorsStandard();
    if (_controller_type == "F0513") return clearErrorsF0513();

    return "N/A";
}

String MakitaBMS::clearErrorsStandard()
{
    digitalWrite(_enable_pin, LOW);
    delay(400);
    byte dummy[9];
    const byte unlock_cmd[] = {0xD9, 0x96, 0xA5};
    cmd_and_read_33(unlock_cmd, 3, dummy, 9);
    const byte reset_cmd[] = {0xDA, 0x04};
    cmd_and_read_33(reset_cmd, 2, dummy, 9);
    digitalWrite(_enable_pin, HIGH);
    return "";
}

String MakitaBMS::clearErrorsF0513()
{
    // F0513 獨立錯誤清除邏輯
    digitalWrite(_enable_pin, LOW);
    delay(400);
    byte dummy[9];
    const byte unlock_cmd[] = {0xD9, 0x96, 0xA5};
    cmd_and_read_33(unlock_cmd, 3, dummy, 9);
    const byte reset_cmd[] = {0xDA, 0x04};
    cmd_and_read_33(reset_cmd, 2, dummy, 9);
    digitalWrite(_enable_pin, HIGH);
    return "";
}