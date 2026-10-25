# Split-Flap · Mega 2560 調機（1～10 位可選）

目前以 **Arduino Mega 2560 直接控制 ULN2003、讀取 KY-003**，透過USB操作1～10個字元。先接一個字元確認馬達與機構，再擴充指定啟用的位置；文字依序逐顆更新。Mega與Hall由USB供電，獨立12V只供馬達；兩者共地。本階段不需MCP23017、電平轉換板或降壓板。

1. **[從單顆測試開始：接線、硬體清單、燒錄與操作](mega2560/README.md)**
2. [單顆接線圖](mega2560/wiring-single.svg)／[十字元接線規劃](mega2560/wiring-ten.svg)／[Hall 洞洞板逐孔配置](mega2560/hall-board.svg)
3. [目前 BOM](hardware/project-bom.csv)／[暫不使用及未來選配](hardware/optional-parts.csv)
4. [KY-003 的 3.39V 量測與鎖存排查](mega2560/hall-diagnostics.md)
5. [軟體驗證與實機紀錄](mega2560/validation.md)
6. [VS Code F5 建置、燒錄、串列除錯與主機中斷點](mega2560/vscode.md)
7. [磁鐵尚未到貨：手動定位目前字元，再指定目標](mega2560/README.md#無磁鐵手動定位與指定字元)
8. **[1～10位安裝配置、整串文字控制與功能完成度](mega2560/multi-unit.md)**

![單顆測試接線](mega2560/wiring-single.svg)

`mega2560/unit_test/unit_test.ino` 開機只啟用U0、不轉動。`DIGITS 1..10`啟用前N位，`UNITS 0,2,7`可指定非連續位置；未啟用的馬達輸出保持LOW。每位獨立保存RAM設定與位置，UNIT切換會保留狀態。無磁鐵時可SET／SHOW單位字元，或SETTEXT宣告目前整串、ARM ALL後TEXT指定目標；跨圈按步數前進。文字與HOME ALL／CYCLE ALL依序逐顆執行。定距移動自動加減速，SPEED巡航間隔800–30000µs。重開機恢復預設；EEPROM、多顆同時步進、Cardputer／網路／UART協定／RTC仍待整合。現有接線無法自動偵測插拔，請只啟用實際接上的組別。

**U0手動定位已回報數輪無偏移：SPEED 800、ACCEL 800、REVSTEPS 4096。** [實機紀錄](mega2560/validation.md#使用者回報的實機進度)保留使用者STATUS與驗證範圍；霍爾歸零與完整系統驗收仍待完成。先只接 U0；馬達電源須有實體切斷開關。移動中送任何非換行字元會停止並釋放線圈，不能取代切斷 12V 或保證硬體故障時關閉。

## 既有 ESP32 與來源

原 ESP32 韌體、MCP 接線、Wokwi 模擬保留，入口為 **[ESP32 文件](docs/esp32/README.md)** 與[原 ESP32 BOM](hardware/esp32-project-bom.csv)。其 INIT／ENABLE／HOME ALL 命令、GPIO／MCP 表及模擬驗收不適用 Mega 測試程式。

機構延續 David Königsmann 設計，以 [JonnyBooker/split-flap 的 0c6ef692](https://github.com/JonnyBooker/split-flap/tree/0c6ef692ac6a5d21a9bc858fa79ecbe816530d8d) 為主要參考，保留每字元 45 片與原字元順序。沿革見[參考比對](docs/esp32/upstream-reference.md)，GitHub fork parent 未更動。

- [原組裝 PDF](Instructions/SplitFlapInstructions.pdf)／[歷史 README](Instructions/README.upstream.md)
- 原始 `unit/`、`ESPMaster/`、`EEPROM_Write_Offset/`、`PCB/` 留存，不適用新 Mega 配線。
- [3D 檔來源](https://www.printables.com/model/69464-split-flap-display)：repo 未包含 STL，未修改磁鐵孔或 Hall 固定孔。

授權維持 [GPL-3.0](LICENSE)，外部機構與 3D 檔依原發布頁授權。
