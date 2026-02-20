#ifndef MAKITA_BMS_H
#define MAKITA_BMS_H

#include <Arduino.h>
#include <functional>
#include "OneWireMakita.h"

// 定義日誌等級
enum LogLevel
{
    LOG_LEVEL_NONE = 0,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_WARN,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG
};

using LogCallback = std::function<void(const String &, LogLevel)>;

struct BatteryData {
    // === 靜態資訊 (Static Data - 來自 11h/EEPROM) ===
    String model = "N/A";
    String serial = "N/A";
    String rom_id = "";
    String prod_date = "N/A";     // 解析自 11h 特定偏移
    String capacity = "N/A";
    String battery_type = "LXT";
    int fw_ver = 0;

    // === 動態數據 (Dynamic Data - 來自 33h/3Bh) ===
    float pack_voltage = 0.0;
    float cell_voltages[5] = {0.0};
    float cell_diff = 0.0;
    float temp1 = 0.0;
    float temp2 = 0.0;
    float temp3 = 0.0;               // 來自 0Ah 偏移，第三溫度感測器
    
    // === 壽命與健康 (Life & Health) ===
    int charge_cycles = 0;
    uint16_t remaining_capacity = 0; // mAh (rosvall 協議)
    uint16_t full_capacity = 0;      // mAh
    uint8_t health_pct = 0;          // 0-100%

    // === 錯誤與鎖定狀態 (Status & Errors - 核心診斷) ===
    uint16_t status_code_raw = 0;    // 原始狀態碼
    String status_code_hex = "00";   // 十六進制顯示
    
    // 鎖定狀態
    uint8_t lock_status = 0; //：0:正常, 1:永久鎖定(熔斷), 2:過熱暫時鎖定, 3:過放電鎖定
    
    uint8_t over_discharge = 0; // 02h 偏移: 過度放電次數
    uint8_t over_load = 0;      // 03h 偏移: 異常過載次數
    uint8_t err_cnt_04 = 0;     // 錯誤計數 04 (限 4 次)
    uint8_t err_cnt_05 = 0;     // 錯誤計數 05 (限 3 次)
    uint8_t err_cnt_06 = 0;     // 充電錯誤 ()
    uint8_t err_cnt_07 = 0;     // 錯誤計數 07 (限 2 次)
    uint8_t fuse_blown = 0;     // 軟體熔斷紀錄 0C (限 1 次)
};

struct SupportedFeatures
{
    bool read_dynamic = false;
    bool led_test = false;
    bool clear_errors = false;
};

class MakitaBMS
{
public:
    MakitaBMS(uint8_t onewire_pin, uint8_t enable_pin);
    // 初始化硬體引腳
    void begin()
    {
        pinMode(_enable_pin, OUTPUT);
        digitalWrite(_enable_pin, HIGH); // NPN: HIGH = OFF
    }
    void setLogCallback(LogCallback callback);
    void setLogLevel(LogLevel level);
    bool isPresent();
    String readStaticData(BatteryData &data, SupportedFeatures &features);
    String readDynamicData(BatteryData &data);
    String ledTest(bool on);
    String clearErrors();
    String resetMessage();
    void setRelay(bool on);
    void setVerifyReads(bool on);
    void readAdvancedDiagnostics(BatteryData &data);

private:
    OneWireMakita makita;
    uint8_t _enable_pin;
    String _controller_type = "UNKNOWN";
    bool _is_identified = false;
    LogCallback _log;
    LogLevel _logLevel = LOG_LEVEL_DEBUG;
  bool _verifyReads = false;

    // --- 工具函數 ---
    void cmd_and_read_33(const byte *cmd, uint8_t cmd_len, byte *rsp, uint8_t rsp_len);
    void cmd_and_read_cc(const byte *cmd, uint8_t cmd_len, byte *rsp, uint8_t rsp_len);
    byte nibble_swap(byte b);
    String getModel();
    String getF0513Model();
    uint8_t readOneWireByte(byte cmd);               // 傳回值必須是 uint8_t，參數必須是 byte
    String readDynamicDataStandard(BatteryData &data);
    String readDynamicDataF0513(BatteryData &data);
    void readAdvancedDiagnosticsStandard(BatteryData &data);
    void readAdvancedDiagnosticsF0513(BatteryData &data);
    String ledTestStandard(bool on);
    String ledTestF0513(bool on);
    String clearErrorsStandard();
    String clearErrorsF0513();



    // --- 日誌輔助 ---
    void logger(const String &message, LogLevel level);
    void log_hex(const String &prefix, const byte *data, int len);
    };
#endif