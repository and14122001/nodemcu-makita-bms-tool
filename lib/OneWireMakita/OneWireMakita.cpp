// lib/OneWireMakita/OneWireMakita.cpp

#include "OneWireMakita.h"

// 互斥鎖，用於保護關鍵部分免受 FreeRTOS 中斷的影響，
// 以確保精確的計時。
static portMUX_TYPE oneWireMux = portMUX_INITIALIZER_UNLOCKED;

// Конструктор класса
OneWireMakita::OneWireMakita(uint8_t pin) {
    _pin = (gpio_num_t)pin;
// 將引腳設定為「開漏」模式一次。
// 這是實作單一匯流排協定最有效且正確的方法。
// digitalWrite(HIGH) 會「釋放」總線，使上拉電阻將其拉高。
// digitalWrite(LOW) 會主動將總線拉低。
    pinMode(_pin, OUTPUT_OPEN_DRAIN);
    digitalWrite(_pin, HIGH); // Начальное состояние - шина свободна
}

// 總線重設的實現
bool OneWireMakita::reset(void) {
    digitalWrite(_pin, HIGH);
    pinMode(_pin, INPUT); // 暫時切換到輸入端，檢查匯流排是否繁忙
    uint8_t retries = 125;
    do {
        if (--retries == 0) return false;
        delayMicroseconds(2);
    } while (digitalRead(_pin) == LOW);

    pinMode(_pin, OUTPUT_OPEN_DRAIN); // 返回工作模式
    
    portENTER_CRITICAL(&oneWireMux);
    digitalWrite(_pin, LOW); // 發送重設脈衝
    portEXIT_CRITICAL(&oneWireMux);

    delayMicroseconds(750); // 重置脈衝持續時間

    portENTER_CRITICAL(&oneWireMux);
    digitalWrite(_pin, HIGH); // 鬆開EN
    delayMicroseconds(70);    // 等待電池管理系統回應
    uint8_t r = !digitalRead(_pin); // 讀取存在脈衝訊號（線路應拉至低）
    portEXIT_CRITICAL(&oneWireMux);

    delayMicroseconds(410); // 剩餘時段
    return r;
}

// 位元組記錄的實作（位元）
void OneWireMakita::write(uint8_t v) {
    for (uint8_t bitMask = 0x01; bitMask; bitMask <<= 1) {
        portENTER_CRITICAL(&oneWireMux);
        if ((bitMask & v)) { // 記錄“1”
            digitalWrite(_pin, LOW); delayMicroseconds(12);
            digitalWrite(_pin, HIGH);
            portEXIT_CRITICAL(&oneWireMux);
            delayMicroseconds(120);
        } else { // 記錄“0”
            digitalWrite(_pin, LOW); delayMicroseconds(100);
            digitalWrite(_pin, HIGH);
            portEXIT_CRITICAL(&oneWireMux);
            delayMicroseconds(30);
        }
    }
}

// 實作位元讀取位元組
uint8_t OneWireMakita::read() {
    uint8_t r = 0;
    for (uint8_t bitMask = 0x01; bitMask; bitMask <<= 1) {
        portENTER_CRITICAL(&oneWireMux);
        digitalWrite(_pin, LOW); delayMicroseconds(10);
        digitalWrite(_pin, HIGH); delayMicroseconds(10);
        if (digitalRead(_pin)) {
            r |= bitMask;
        }
        portEXIT_CRITICAL(&oneWireMux);
        delayMicroseconds(53);
    }
    return r;
}