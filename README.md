# Split-Flap · ESP32-CAM 單主控＋MCP23017

本分支 `codex/esp32cam-single-controller` 提供 **2.0.0 單主控韌體**：手機網頁、Wi-Fi、文字／時鐘／日期、設定保存與馬達排程都在 AI-Thinker ESP32-CAM。使用現有六片 MCP23017 中的四片，另外兩片備用；沿用 P011 8 路電平轉換板、ULN2003、12V 馬達、LM2596 及 CP2102，無須新增模組。

支援 **1～10 任意位數、非連續位置與自訂順序、同時翻頁、慢速起步與加減速、原版 45 字元、手動定位與選用 Hall**。預設 SPEED 800／ACCEL 800；MCP23017 不需燒錄，之後只維護一份 ESP32 韌體。

**[開始操作與移轉](esp32cam/README.md) · [完整逐線表](esp32cam/wiring.md) · [建置／自動測試與實板驗收](esp32cam/validation.md) · [本分支 BOM](hardware/project-bom.csv)**

![單主控接線](esp32cam/wiring-runtime.svg)

- [正常電源與 MCP 匯流排](esp32cam/wiring-runtime.svg)
- [CP2102：一條 USB 燒錄](esp32cam/wiring-flashing.svg)
- [U0～U9 馬達相位](esp32cam/wiring-motors.svg)
- [十路 Hall](esp32cam/wiring-hall.svg)

四圖也內建在手機「裝置設定」頁，離線可查看。VS Code 的 Ctrl+Shift+B 現在建置 ESP32-CAM；F5 提供燒錄、監看、模擬測試與主機中斷點。

單主控目前完成軟體建置與模擬驗證，**尚未燒入或驗收 MCP 實機**。800µs 是要求的相位間隔下限，I²C 傳輸與排程會增加實際間隔；需從 U0 開始驗收。開機不轉動、不恢復 ARM；核對保留的字元並確認後才允許轉動。

## 雙主控穩定基準與回退

已實際運作的 Mega＋ESP32-CAM **1.1.0** 保留在 `esp32-purchased-bom`，封存提交 **e65f30e**。見 [雙主控基準紀錄](docs/dual-controller-baseline.md)與[該提交的操作指南](https://github.com/zx90316/split-flap/blob/e65f30e/esp32cam/README.md)。原版不會加入本分支的 MCP 接線。

移轉前先由舊網頁匯出設定；首次使用 CP2102 USB 燒入 2.0.0，再匯入舊備份。不能拿單主控更新檔直接更新舊雙主控版。改線時須移除 Mega 原有的 UART、馬達及 Hall 連線；兩種接線不並用。

本分支的 `mega2560/` 保留舊硬體回歸程式；運動核心已抽為共用 `libraries/SplitFlapMotion/`。雙主控實機通過紀錄不代表 MCP 版本已通過實機驗收。

## 原始設計與歷史方案

機構延續 David Königsmann 設計，以 [JonnyBooker/split-flap 的 0c6ef692](https://github.com/JonnyBooker/split-flap/tree/0c6ef692ac6a5d21a9bc858fa79ecbe816530d8d) 為主要參考，保留每字元 45 片與原字元順序。沿革見[參考比對](docs/esp32/upstream-reference.md)，GitHub fork parent 未更動。

- [原組裝 PDF](Instructions/SplitFlapInstructions.pdf)／[歷史 README](Instructions/README.upstream.md)。
- [舊 ESP-WROOM-32 方案](docs/esp32/README.md)及 `firmware/esp32/` 為歷史原型，其 GPIO、命令與更新方式不適用本分支的 ESP32-CAM。
- 原始 `unit/`、`ESPMaster/`、`EEPROM_Write_Offset/`、`PCB/` 留存。
- [3D 檔來源](https://www.printables.com/model/69464-split-flap-display)：repo 未包含 STL，本次未修改機構或孔位。

授權維持 [GPL-3.0](LICENSE)，外部機構與 3D 檔依原發布頁授權。
