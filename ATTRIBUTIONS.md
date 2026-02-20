
*   **Martin Jansson 的部落格文章 (Martin Jansson's Blog Posts):** 對牧田電池通訊協定進行了深入的分析與逆向工程。
    *   **具體採用部分：** 參考了其關於通訊協定（Protocol）的基礎結構分析，特別是校驗和（Checksum/CRC）算法的實作邏輯，以及物理層連接腳位的定義。
    *   [Makita Battery Post 1](https://martinjansson.netlify.app/posts/makita-battery-post-1)
    *   [Makita Battery Post 2](https://martinjansson.netlify.app/posts/makita-battery-post-2)

*   **GitHub - Belik1982/esp32-makita-bms-reader:** 一個早期且具影響力的 ESP32 實作，用於讀取牧田 BMS 數據，是本專案的關鍵參考。
    *   **具體採用部分：** 參考了其 ESP32 端的通訊實作流程，包括發送指令序列（如讀取電池型號、電壓數據）的時序控制，以及部分錯誤碼的解析邏輯。
    *   專案庫連結

*   **Codeberg - Makita LXT Protocol:** 提供了極具價值的協定資訊與數據結構定義。
    *   **具體採用部分：** 採用了其整理的詳細數據映射表（Memory Map），用於精確解析回應封包中的各項數值（如製造日期格式、循環次數、過放/過載計數器等）。
    *   專案庫連結

*   **Done.land - Makita LXT Digital Interface:** 關於數位介面與通訊邏輯的詳細文件。
    *   **具體採用部分：** 參考了其關於電池介面電氣特性的說明，確保硬體連接的正確性，以及對通訊握手訊號的理解。
    *   文章連結

*   **GitHub - Open Battery Information:** 一個旨在記錄包含牧田在內、各種電池協定的協作專案。
    *   **具體採用部分：** 作為協定指令代碼（Command Codes）的交叉驗證來源，確認了如 0xB6（清除熔絲標記）等特殊指令的定義。
    *   專案庫連結

我們鼓勵使用者造訪這些原始來源，以更深入地了解其背後卓越的工作。
