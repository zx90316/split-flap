> 此文件保留 ESP-WROOM-32 歷史原型；本分支正式方案為 [ESP32-CAM＋MCP23017 單主控](../../esp32cam/README.md)。本頁的 GPIO、BOM、指令與時序不適用新版。

# Split-Flap · ESP32 模組版（10 字元）

本專案以 [JonnyBooker/split-flap](https://github.com/JonnyBooker/split-flap) 為主要參考，延續 David Königsmann 的機構設計，控制系統改成 **一片 ESP32＋現成模組＋洞洞板**，作為本專案的標準組建方式。保留原本 10 字元、每字元 45 片翻頁的機構與字元順序，不再要求 ESP-01、10 片 Nano 或訂製 PCB。

> **工程原型，尚未實物驗證。** 尚未完成實板接線、燒錄、馬達／電源／十字元測試。軟體測試不是硬體安全認證。先閱讀電源及組裝門檻，再接線或通電。

## 從這裡開始

1. [實作架構與元件分工](architecture.md)
2. [逐腳接線、位址、供電與 RESET](wiring.md)
3. [元件檢查、機構比對與分階段組裝](assembly.md)
4. [編譯、串列命令、歸零與校準](firmware.md)
5. [軟體驗證結果與硬體驗收表](validation.md)
6. [參考版本、相容性比對與功能移植計畫](upstream-reference.md)
7. [標準組建 BOM](../../hardware/esp32-project-bom.csv) 與[選配零件](../../hardware/optional-parts.csv)

## 實際採用的架構

- ESP-WROOM-32 30pin Type-C／CH340 主控，PlatformIO 的 `esp32dev` 為待確認實板的起始設定
- 10 顆 **12V** 28BYJ-48、10 片 ULN2003、10 片 KY-003
- **4 片 MCP23017**：`0x20–0x22` 共 40 路馬達相位；`0x23` 集中 10 路 Hall
- **1 片 8 路 MOS 電位轉換板**：SDA、SCL、/RESET 用 3 路；ESP32 為 3.3V，MCP/Hall 為 5V
- **1 片 5V 降壓板**供控制電源；不得並聯降壓板輸出。實際持續電流／溫升待測
- 12V 馬達匯流排＋5V 控制匯流排，共地；12V 電源的極性、連續與峰值電流能力須先驗證；PD/QC 觸發線為選配輸入方式

Hall 全在 5V 側，避開現行 MCP23017 資料表只允許輸出的 GPA7/GPB7，並減少每輪 Hall 讀取到一筆 16-bit I²C 交易。

## 新韌體已實作

`firmware/esp32/` 是此架構的唯一新韌體入口：

- 非阻塞、最多十馬達排程；每輪每馬達最多一步，延遲時不暴衝追趕
- MCP OLAT 影子暫存及每片 16-bit 批次更新，100kHz I²C
- 開機不動作、不自動歸零；明確初始化、逐級啟用、ARM、HOME
- 離開既有磁區再尋找有效邊緣；5ms debounce、步數與時間雙重歸零限制
- 每字元半步／圈、零點偏移、相序方向、步距、Hall 極性校準與 NVS 儲存
- 45 字元原版順序、UTF-8 ÄÖÜ 與替代 `$&#` 翻頁；往前跨圈時重新歸零
- 停止／故障釋放線圈、位置失效；I²C 失敗、配置變動及迴圈停滯鎖定
- 主機端單元測試與 ESP32 交叉編譯；結果見[驗證紀錄](validation.md)

**本版先用 USB 串列完成可靠調機。** 原 ESP8266 網頁、Wi-Fi、時間顯示尚未移植，原程式只作歷史參考。接下來應先完成單字元及十字元量測，再在同一片 ESP32 增加通訊介面。

## 快速建置（尚不可直接接上十顆馬達）

```sh
cd firmware/esp32
pio run
# 確認 USB/外接電源隔離及實板後才燒錄：
pio run --target upload
pio device monitor --baud 115200
```

初次開機保持馬達 12V 關閉，先依文件完成檢查。`INIT POWER_OFF` 只是操作者確認，韌體沒有電壓感測器能驗證你真的斷電。

## 主要參考與來源沿革

主要參考鎖定 JonnyBooker 的 [`0c6ef692`](https://github.com/JonnyBooker/split-flap/tree/0c6ef692ac6a5d21a9bc858fa79ecbe816530d8d)。該版本仍採 ESP8266＋Nano；本版 ESP32 韌體為獨立控制實作，未宣稱已移植其 Web、排程、倒數、Wi-Fi 設定或 OTA。其作者說明只能在時間允許下提供協助與更新，不能視為持續維護保證。詳細證據與規劃見[參考比對](upstream-reference.md)。

GitHub fork 關係及既有歷史仍來自 David Königsmann 的倉庫；「主要參考」不代表已改動 GitHub 的 fork parent。


- [歷史 David 版 README](../../Instructions/README.upstream.md)
- [原始完整組裝 PDF](../../Instructions/SplitFlapInstructions.pdf)
- 原始 `unit/`、`ESPMaster/`、`EEPROM_Write_Offset/`、`PCB/` 原封保留，**不適用本分支新接線**
- [原作者 3D 檔來源](https://www.printables.com/model/69464-split-flap-display)；本 repo 未包含 STL，也沒有未經量測就重畫孔位
- JonnyBooker 的 [3D 備份說明](https://github.com/JonnyBooker/split-flap/blob/0c6ef692ac6a5d21a9bc858fa79ecbe816530d8d/3dModels/README.md) 亦指向同一模型來源；列印前確認發布頁授權與版本。
- Git 歷史 fork 起點：`14a22d9f38900aa520feeb70656357b8fd719e45`

授權維持 [GPL-3.0](../../LICENSE)。原專案、照片、組裝設計歸原作者；本分支新增 ESP32 實作與文件保留其來源。外部 3D 檔的使用以原發布頁授權為準。

## 線上正式韌體模擬

[開啟 Wokwi 閉迴路模擬](https://wokwi.com/projects/478374804791943169)；[快速複現、操作與驗證邊界](../../simulation/wokwi/README.md)。執行本 repo 正式 ESP32 韌體，四個 MCP 位址、四十相輸出及十個轉子模型自動產生 Hall 回授，可操作 HOME／TEXT／MOVE／CAL／SAVE／STOP。上電不自動啟用，須依說明送入指令。模型不驗證電位轉換、12V 電氣安全、扭力或真實翻頁機構；尚未移植的 Wi-Fi／Web／NTP 亦不在此模擬範圍。
