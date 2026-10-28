# 2026-10-26 新購硬體與換板紀錄

依使用者提供的購買清單截圖登錄：下列均為**已下單、待出貨**，不是到貨檢查或實機驗收。主方案維持雙主控，改為 **Mega2560 Pro CH340G＋NodeMCU ESP-32S**，不使用 MCP23017。

| 項目 | 購買選項 | 數量 | 本次用途 |
| --- | --- | --- | --- |
| Mega2560 Pro | ATmega2560／CH340G；焊好排針 | 1 片 | 取代標準 Mega；邏輯腳位與 EEPROM 格式沿用 |
| NodeMCU ESP-32S | classic ESP32／ESP-WROOM-32 開發板 | 1 片 | 手機網頁、Wi-Fi、時鐘、UART、OTA；板上 USB 燒錄 |
| 高溫膠帶 | 10mm × 33m | 1 卷 | 輔助絕緣與標記；不替代固定柱、外殼或拉力固定 |
| 插片式保險絲座 | 中號、18號線、標示「5A」 | 2 個 | 12V 入口 F1、5V 支路 F2；是否附保險絲、額定與品質到貨核對 |
| 單芯鍍錫銅線 | 黑色；銅芯直徑 0.5mm；500cm | 1 份／5m | 短訊號線、洞洞板連接；單組電源支路仍須核對電流與壓降 |
| DIP 指撥開關 | 4 位 | 4 個 | 暫保留備用；本版不接 GPIO，DIGITS／UNITS 仍由網頁／USB 設定 |
| KF301 PCB 螺絲端子 | 2P、約 5mm 間距 | 4 個 | 焊接式電源入口／出口端子；不等於完成的分電母線 |

## 韌體與接線

- 手機控制源碼保留在 `esp32cam/`，避免原有路徑與腳本失效；**目前工具的目標已改為 NodeMCU ESP-32S，不能拿來燒 ESP32-CAM**。
- ESP32 以 `ESP32 Dev Module` 的相容設定建置：classic ESP32、4MB、DIO／40MHz、PSRAM disabled、`min_spiffs`。固定 Arduino-ESP32 3.3.12。NodeMCU-32S 的套件板型缺少分割選單，因此使用可明確指定 OTA 分割的通用板型；GPIO 使用數字，不依 D 標籤別名。
- Mega Pro 沿用 `arduino:avr:mega:cpu=atmega2560`。ATmega2560 需有相容 bootloader；CH340G 僅為 USB 串列轉換器。
- UART 不改腳：ESP GPIO13→P011 A0／B0→Mega D19；Mega D18→P011 B1／A1→ESP GPIO14。仍為 115200、8N1，沿用現有 3.3V／5V P011。
- Pro 焊好的是公排針時，接 ULN 公針需母對母 4P；Hall 分線板至 Pro 端也改用母頭。實體排針位置不同，不能照標準 Mega 的雙排插座位置搬接。
- [逐線表與 USB 切換](../esp32cam/wiring.md)／[正常接線圖](../esp32cam/wiring-runtime.svg)／[USB 燒錄圖](../esp32cam/wiring-flashing.svg)。四張圖仍可從手機設定頁離線查看。

## 12V／5V 配電

12V 正端先經 F1／總開關，分成「手動馬達開關→12V 馬達分配」及「LM2596→5.0V→F2→兩板／Hall 控制分配」。12V−、LM2596 IN−／OUT−、兩板、P011、ULN 與 Hall 共地。+12V、+5V、GND 各自獨立母線；馬達回流直接回電源分配點，不經控制板細線。

**0.5mm 是直徑，並非 0.5mm²**：截面積 π×0.25²≈0.196mm²，約 AWG24。這份黑色單芯線不作十組馬達共用主幹，也不因座上附 18AWG 線就把整條電路都當成 18AWG。主幹沿用符合實測負載的粗銅線；正極、電壓與兩端用途加標籤，避免黑色線混接。單芯線不宜反覆彎折，需固定。

KF301 四個 2P 僅提供八個夾線點，需依端子料表及洞洞板佈局焊接、分配與固定；不能直接推定足以提供十路獨立電源插座。示例可作 12V 入口、受開關控制的馬達出口、穩壓 5V 入口、5V 控制出口；跨接導線與焊盤須承受所經總電流。正負極不能靠外觀推定已分線。

購買選項「5A」照實記錄，但**不代表 F1／F2 都應裝 5A**。電源額定、十組同時運轉電流、線長、最弱導線／接點、浪湧及熔斷曲線尚待確認；F2 需同時考慮 LM2596 的限流行為。PPTC 本次未列入已購品。保險絲不保證防止反接或 12V 誤插 5V。

## DIP 與既有繼電器

DIP 開關的購買本身不會改變位數設定。四位可表達 16 種組合，但「0／11～15 如何處理、何時讀取、與非連續 UNITS／EEPROM 的優先順序」仍須另定；目前不自動接管既有安裝配置。DIP 不承擔馬達電流。

使用者已有 SRD-12VDC-SL-C 12V 繼電器模組；不在此次新購清單。可作馬達正極支路的電控開關候選：開關後 12V→COM，NO→馬達分配，NC 留空。**本版未加入繼電器 GPIO 或啟用邏輯，也不要接 IN 到主控**；控制端上拉／觸發電壓與掉電狀態未確認。保留手動馬達開關及保險絲。繼電器外殼 10A 標示不能直接視為此 PCB、端子與馬達負載的可用額定。

## 到貨與設定搬移

1. 損壞的 ESP32-CAM／USB 轉接器保持隔離；新板先各自只接電腦 USB，確認列舉與燒錄。沒有外接 5V 或 UART 時才做此步。
2. 核對 Mega Pro 的 5V／GND／邏輯腳與公母頭；核對 NodeMCU 實際 5V 輸入。若只印 VIN，先確認其電源路徑可接穩壓 5V，不把 12V 送入新板。兩板外部 5V 與電腦 USB 互斥。
3. 新 Mega Pro 的 EEPROM 是另一顆晶片，不會帶入舊板校準。若有舊系統匯出檔，從新網頁匯入；沒有備份時手動還原。原 U0 已確認 `REVSTEPS=4096 OFFSET=273 SPEED=800 DIR=1 ORDER=1234 POLARITY=LOW`；最後串列紀錄的 ACCEL=799，還原時以實際保存值／匯出檔為準；韌體預設仍為800。不要把 U0 的 OFFSET 套用到其他位置。
4. 新 ESP32 的 Wi-Fi、登入密碼及最後字元不會從燒毀的舊 ESP32 自動移入。USB `ACCESS` 取得新憑證；重新設定 Wi-Fi，核對實物完成定位、ARM 後才運轉。
5. 先驗 U0，再逐顆與並行負載測試；新板、電源接點、USB 回灌、溫升、OTA、長時間運轉尚未實機驗收。

官方參考：[Arduino Mega](https://docs.arduino.cc/hardware/mega-2560/)、[Arduino-ESP32 3.3.12 板型定義](https://github.com/espressif/arduino-esp32/blob/3.3.12/boards.txt)、[ESP32 USB 自動重設／BOOT](https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/boot-mode-selection.html)。相容板的電源及排針差異仍以賣方資料與實板核對。
