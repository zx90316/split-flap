# Split-Flap · ESP32 模組版（10 字元）

本分支把 [David Königsmann 的 split-flap](https://github.com/davidkingsman/split-flap) 改成 **一片 ESP32＋現成模組＋洞洞板**，依 2026-10-20 提供的下單清單規劃。保留原本 10 字元、每字元 45 片翻頁的機構與字元順序，不再要求 ESP-01、10 片 Nano 或訂製 PCB。

> **工程原型，尚未實物驗證。** 採購紀錄全部為待出貨；未登錄點收、接線、燒錄、馬達／電源／十字元測試。軟體測試不是硬體安全認證。先閱讀電源及組裝門檻，再接線或通電。

## 從這裡開始

1. [實作架構與採購分配](docs/esp32/architecture.md)
2. [逐腳接線、位址、供電與 RESET](docs/esp32/wiring.md)
3. [到貨點收、機構比對與分階段組裝](docs/esp32/assembly.md)
4. [編譯、串列命令、歸零與校準](docs/esp32/firmware.md)
5. [軟體驗證結果與硬體驗收表](docs/esp32/validation.md)
6. [21 項訂購＋6 項既有設備 BOM](hardware/ordered-bom.csv)

## 實際採用的架構

- ESP-WROOM-32 30pin Type-C／CH340 主控，PlatformIO 的 `esp32dev` 為待確認實板的起始設定
- 10 顆 **12V** 28BYJ-48、10 片 ULN2003、10 片 KY-003
- **4／6 片 MCP23017**：`0x20–0x22` 共 40 路馬達相位；`0x23` 集中 10 路 Hall。其餘兩片暫不分配
- **1／2 片 8 路 MOS 電位轉換板**：SDA、SCL、/RESET 用 3 路；ESP32 為 3.3V，MCP/Hall 為 5V
- **1／3 片 5V 降壓板**作初期控制電源候選；不得把三片輸出並聯。實際持續電流／溫升待測
- 12V 馬達匯流排＋5V 控制匯流排，共地；PD/QC 線的 12V 協商、極性與電流能力尚未驗證

此分配取代先前「3 MCP＋8 路 Hall＋2 路 ESP32 Hall」草案。Hall 全在 5V 側，避開現行 MCP23017 資料表只允許輸出的 GPA7/GPB7，並減少每輪 Hall 讀取到一筆 16-bit I²C 交易。

## 新韌體已實作

`firmware/esp32/` 是此架構的唯一新韌體入口：

- 非阻塞、最多十馬達排程；每輪每馬達最多一步，延遲時不暴衝追趕
- MCP OLAT 影子暫存及每片 16-bit 批次更新，100kHz I²C
- 開機不動作、不自動歸零；明確初始化、逐級啟用、ARM、HOME
- 離開既有磁區再尋找有效邊緣；5ms debounce、步數與時間雙重歸零限制
- 每字元半步／圈、零點偏移、相序方向、步距、Hall 極性校準與 NVS 儲存
- 45 字元原版順序、UTF-8 ÄÖÜ 與替代 `$&#` 翻頁；往前跨圈時重新歸零
- 停止／故障釋放線圈、位置失效；I²C 失敗、配置變動及迴圈停滯鎖定
- 主機端單元測試與 ESP32 交叉編譯；結果見[驗證紀錄](docs/esp32/validation.md)

**本版先用 USB 串列完成可靠調機。** 原 ESP8266 網頁、Wi-Fi、時間顯示尚未移植，原程式只作歷史參考。接下來應先完成單字元及十字元量測，再在同一片 ESP32 增加通訊介面；無須另用 ESP32-CAM／HC-05。

## 快速建置（尚不可直接接上十顆馬達）

```sh
cd firmware/esp32
pio run
# 確認 USB/外接電源隔離及實板後才燒錄：
pio run --target upload
pio device monitor --baud 115200
```

初次開機保持馬達 12V 關閉，先依文件完成檢查。`INIT POWER_OFF` 只是操作者確認，韌體沒有電壓感測器能驗證你真的斷電。

## 保留的上游資料

- [原版 README](Instructions/README.upstream.md)
- [原始完整組裝 PDF](Instructions/SplitFlapInstructions.pdf)
- 原始 `unit/`、`ESPMaster/`、`EEPROM_Write_Offset/`、`PCB/` 原封保留，**不適用本分支新接線**
- [原作者 3D 檔來源](https://www.printables.com/model/69464-split-flap-display)；本 repo 未包含 STL，也沒有未經量測就重畫孔位
- Fork 起點：`14a22d9f38900aa520feeb70656357b8fd719e45`

授權維持 [GPL-3.0](LICENSE)。原專案、照片、組裝設計歸原作者；本分支新增 ESP32 實作與文件保留其來源。外部 3D 檔的使用以原發布頁授權為準。
