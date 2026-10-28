# 雙主控 1.1.0 基準封存

**歷史基準是提交 `e65f30e`。** 2026-10-26 後續依使用者新購硬體，`esp32-purchased-bom` 已開始遷移至 1.2.0 的 Mega2560 Pro＋NodeMCU ESP-32S。下文描述封存當時的舊硬體；需要舊 CAM 版本時以此提交為準，不以分支名稱推定內容。[現行操作指南](../esp32cam/README.md)與[新購硬體紀錄](../hardware/purchased-2026-10-26.md)是新版入口。

日期：2026-10-26（Asia/Taipei）。穩定分支：`esp32-purchased-bom`。此分支保留 Mega 2560＋ESP32-CAM 雙主控、P011 UART 電平轉換及現有 USB／網頁單板更新流程。

已完成：1～10 位與非連續映射、並行翻頁、SPEED 800／ACCEL 800 預設、加減速、手動定位、45 字元、字元歷史、差異套用設定、手機網頁／時鐘／日期、登入、Wi-Fi、設定備份及 ESP32 OTA。操作見 [ESP32-CAM 指南](../esp32cam/README.md)，建置與測試結果見 [驗證紀錄](../esp32cam/validation.md)。

使用者於 2026-10-26 回報現版「實際測試通過，目前完美運行」。既有 U0 多輪無偏移參數為 SPEED 800、ACCEL 800、REVSTEPS 4096、OFFSET 0、DIR 1、ORDER 1234、POLARITY LOW；沒有將此回報擴大為十位滿載、Hall、長時間或逐項斷電驗收。

雙板統一更新僅完成 [可行性研究](../esp32cam/dual-update-research.md)，未修改 Bootloader、ISP 或現有接線。此穩定分支不接入單主控變更。

下一版在獨立分支 `codex/esp32cam-single-controller` 實作 ESP32-CAM＋四片現有 MCP23017；目前六片庫存中兩片備用。新接線驗收前，可回此分支依現有流程重建／還原兩板。移轉前先從網頁匯出安裝配置與校準；Wi-Fi／登入密碼不包含於備份，ESP32 NVS 應保留。
