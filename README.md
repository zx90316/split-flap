# Split-Flap · Mega 2560 調機（10 字元機構）

目前以 **Arduino Mega 2560 直接控制 ULN2003、讀取 KY-003**，先接一個字元，透過 USB 確認接線、馬達、歸零與翻頁機構。Mega 與 Hall 由 USB 供電，獨立的 12V 只供馬達；兩者共地。本階段不需 MCP23017、電平轉換板或降壓板。

1. **[從單顆測試開始：接線、硬體清單、燒錄與操作](mega2560/README.md)**
2. [單顆接線圖](mega2560/wiring-single.svg)／[十字元接線規劃](mega2560/wiring-ten.svg)／[Hall 洞洞板逐孔配置](mega2560/hall-board.svg)
3. [目前 BOM](hardware/project-bom.csv)／[暫不使用及未來選配](hardware/optional-parts.csv)
4. [KY-003 的 3.39V 量測與鎖存排查](mega2560/hall-diagnostics.md)
5. [軟體驗證與實機紀錄](mega2560/validation.md)

![單顆測試接線](mega2560/wiring-single.svg)

`mega2560/unit_test/unit_test.ino` 一次只操作選定的一顆。提供 WATCH、LED、STEP、REV、HOME、MEASURE、FLAP、GOTO、CYCLE 與校準命令。開機不動作，先 ARM 才能輸出；設定只在 RAM，重開機恢復預設。十顆排程、TEXT、EEPROM、Cardputer 網路／本地輸入及 RTC 留待後續重構。

**尚未完成實物驗證。** 先只接 U0；馬達電源須有實體切斷開關。移動中送任何非換行字元會停止並釋放線圈，不能取代切斷 12V 或保證硬體故障時關閉。

## 既有 ESP32 與來源

原 ESP32 韌體、MCP 接線、Wokwi 模擬保留，入口為 **[ESP32 文件](docs/esp32/README.md)** 與[原 ESP32 BOM](hardware/esp32-project-bom.csv)。其 INIT／ENABLE／HOME ALL 命令、GPIO／MCP 表及模擬驗收不適用 Mega 測試程式。

機構延續 David Königsmann 設計，以 [JonnyBooker/split-flap 的 0c6ef692](https://github.com/JonnyBooker/split-flap/tree/0c6ef692ac6a5d21a9bc858fa79ecbe816530d8d) 為主要參考，保留每字元 45 片與原字元順序。沿革見[參考比對](docs/esp32/upstream-reference.md)，GitHub fork parent 未更動。

- [原組裝 PDF](Instructions/SplitFlapInstructions.pdf)／[歷史 README](Instructions/README.upstream.md)
- 原始 `unit/`、`ESPMaster/`、`EEPROM_Write_Offset/`、`PCB/` 留存，不適用新 Mega 配線。
- [3D 檔來源](https://www.printables.com/model/69464-split-flap-display)：repo 未包含 STL，未修改磁鐵孔或 Hall 固定孔。

授權維持 [GPL-3.0](LICENSE)，外部機構與 3D 檔依原發布頁授權。
