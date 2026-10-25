# ESP32-CAM＋MCP23017 單主控研究

日期：2026-10-26（Asia/Taipei）。基準為目前已由使用者回報正常運行的 Mega＋ESP32-CAM 1.1.0。本文件為可行性研究，尚未將單主控韌體建置、燒入或接到馬達；現有成品程式與接線不變。

## 結論與現有零件

ESP32-CAM 可以作為唯一主控，由 MCP23017 擴充馬達相位輸出與 Hall 輸入。網頁、Wi-Fi、時鐘、校準、字元記錄及馬達排程都放在同一份 ESP32 韌體；MCP23017 不需燒錄，也不需處理 Mega Bootloader 或雙板版本配對。這比雙板統一更新更符合減少維護複雜度的目標。

使用者已確認有 **6 片 MCP23017 模組**。完整十位設計使用 **3 片馬達輸出＋1 片 Hall＝4 片**，其餘 2 片備用；不新增主控、驅動或轉換模組。沿用現有 P011、LM2596、ULN2003、12V 28BYJ-48、KY-003 與 CP2102。

使用者提供的 [模組頁面](https://www.taiwansensor.com.tw/product/mcp23017-e-ss-arduino-i2c%E4%BB%8B%E9%9D%A216%E8%B7%AFio%E6%93%B4%E5%B1%95%E6%A8%A1%E7%B5%84-iic-%E8%BC%B8%E5%85%A5%E8%BC%B8%E5%87%BA%E6%93%B4%E5%B1%95%E6%9D%BF/)已在瀏覽器核對：貨號 IMD-008087，腳位表列 VCC、GND、SCL、SDA、**RST（低有效）**、ITA、ITB，另有 A0／A1／A2 地址設定。此資料支持共用 RESET 的設計，實板仍需核對焊接與上拉值。商家文字含不同版本規格，電氣限制以 Microchip 現行資料表為準。

## 四片分工

| I²C 7-bit 地址 | 分工 | 腳位使用 |
| --- | --- | --- |
| 0x20 | U0～U3 馬達 | GPA0～3、GPA4～7、GPB0～3、GPB4～7，各四路接一片 ULN2003 的 IN1～4 |
| 0x21 | U4～U7 馬達 | 同上 |
| 0x22 | U8～U9 馬達 | GPA0～3、GPA4～7；其餘輸出低 |
| 0x23 | U0～U9 Hall | GPA0～6 對 U0～U6，GPB0～2 對 U7～U9 |

每顆 MCP 有 16 路，十顆馬達需要 40 路、十個 Hall 需要 10 路；50 路超過三片的 48 路，四片足夠。MCP23017 的 GPA7／GPB7 依現行 DS20001952D 只用於輸出，Hall 避開這兩腳。MCP 控制的是 ULN 邏輯輸入，不承擔馬達線圈電流。[Microchip 資料表](https://ww1.microchip.com/downloads/aemDocuments/documents/APID/ProductDocuments/DataSheets/MCP23017-Data-Sheet-DS20001952.pdf)、[TI ULN2003A](https://www.ti.com/lit/ds/symlink/uln2003a.pdf)

六片不用全部裝。若改成五片各控兩顆馬達，單次 8-bit 輸出需五筆、共 135 個 SCL；三片 16-bit 只需三筆、共 108 個 SCL。多用板不一定更快。

DIGITS 仍為 1～10 任意整數，UNITS 仍可非連續／重排。只初始化實際需要的 MCP／Hall 功能，停用 U 的輸出始終為低；MCP 回 ACK 只能證明擴充晶片存在，不能偵測馬達插頭。

## ESP32-CAM 腳位與電源候選

相機、SD 卡與閃光燈繼續停用，SD 卡槽不插卡。可研究以 GPIO13 作 SDA、GPIO14 作 SCL，GPIO15 經 P011 第三路以 open-drain 控制四片共用 RST；UART0 的 U0R／U0T 留給 CP2102。這是候選配置，不能與現有 GPIO13／14→Mega 的 UART 線同時接。

舊一般 ESP32 原型用 GPIO21／22／23，不能直接套用到 ESP32-CAM；AI-Thinker 的這些腳屬相機訊號，也不是目前可直接使用的排針配置。GPIO12 有 Flash 電壓開機綁定，GPIO2 影響下載模式，GPIO16／17 涉及 PSRAM；不拿它們當預設擴充腳。[Espressif AI-Thinker 相機腳位](https://raw.githubusercontent.com/espressif/arduino-esp32/3.3.12/libraries/ESP32/examples/Camera/CameraWebServer/camera_pins.h)、[開機綁定](https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/boot-mode-selection.html)

建議 MCP、Hall 及 P011 VCCB 都留在 5V 側，P011 VCCA 接 ESP32 3.3V，三路分別轉換 SDA、SCL、RST；沿用 LM2596 穩壓 5.0V。MCP 5V 下的 SDA／SCL／RESET 高電位門檻為 0.8×VDD，3.3V 直連不能作規格保證。MCP 僅擴充訊號，ULN／馬達的 12V 支路與共地維持原設計。[Microchip DC 規格](https://ww1.microchip.com/downloads/aemDocuments/documents/APID/ProductDocuments/DataSheets/MCP23017-Data-Sheet-DS20001952.pdf)

P011 通過先前 115200 UART 測試不等於通過 400kHz I²C。須量測模組與轉換板上拉的並聯總值、帶四片與實際線長時的上升時間／低電位。I²C 幹線盡量短，四片集中在控制區，較長的線由 ULN／Hall 端分配；不把高速 I²C 沿整排馬達任意延伸。MCP 對 ULN 輸入與板載 LED 的實際高電位／負載能力也須驗證。

## 800／800 與同時翻頁

MCP 是保持輸出電位的 I/O 擴充器，不是自行產生步進脈衝的控制器。ESP32 仍須對每半步計時，計算十位相位，將同一片 MCP 的輸出合併成 OLATA／OLATB 一次寫入；不能逐 IN 腳呼叫讀改寫。

估算假設每筆 16-bit 寫入包含 address、register、兩個 data，各有 ACK，共 36 個 SCL；一次 16-bit Hall 讀取含指標與 repeated-start，約 45 個 SCL。未包含 START／STOP 額外時間、ESP32 排程、驅動開銷及匯流排拉升。

| 理想匯流排資料時間 | 100kHz | 400kHz |
| --- | ---: | ---: |
| 三片馬達各寫一次 | 1080µs | 270µs |
| 加一次 Hall 讀取 | 1530µs | 382.5µs |
| 十顆各自觸發一次獨立 16-bit 寫入 | 3600µs | 900µs |

ESP32 官方支援上限按 400kHz 設計，不能因 MCP 宣稱 1.7MHz 就假設整條總線可跑 1.7MHz。[ESP-IDF 5.5.4 I²C](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32/api-reference/peripherals/i2c.html)

**400kHz、整片寫入及合併排程，讓十位 800µs 巡航在頻寬上有可行空間；尚不能保證實板同速／不漏步。** 若每 800µs 合併成三筆寫入、Hall 每 2ms 一讀，純資料平均佔用約 39.4%；但十顆相位時點分散時，十筆獨立寫入已需 900µs，尚未讀 Hall。因此不能僅憑 382.5µs 就宣稱任意十組不同校準都能精確維持 800µs。

排程須明確處理時間量化／合併窗口、各位不同加減速及負載。維持所有變更位置一起起步、各自完成，容許的微秒級相位差要量測；過載不得一次補多個相位而爆衝。必要時回報延遲或降低可用速度上限，而不是遺失步數後仍宣稱位置正確。同一 I²C 總線更新多片不是電氣上的同時鎖存，正常批次寫入的差距為數百微秒等級，不是逐顆跑完整個目標。

## Repo 可重用內容及舊原型缺口

- 現有 `mega2560/unit_test/bench.*`、`panel.*` 是純 C++ 運動狀態機，可抽成平台共用核心，沿用已測試的加減速、手動定位、45 字元、1～10 位／映射及並行行為；不直接搬 Arduino Mega GPIO 輸出與 EEPROM 適配層。
- `firmware/esp32/include/mcp_bus.h` 已有 nibble 映射、16-bit OLAT 合併、shadow、NACK／短讀與稽核處理，可重用概念與測試。它的設定要求全部四片存在，須改為按安裝配置啟用必要晶片。
- 舊 `firmware/esp32` 是一般 ESP32、100kHz、USB 控制原型；沒有現有手機頁面的完整功能，也沒有目前 Mega 版本的加減速／手動字元流程，不能直接改板型就取代成品。
- 舊主迴圈只在每 2ms 成功讀 Hall 後呼叫運動 tick，不能達成 800µs。每 250ms 一次的完整四片稽核在 400kHz 純資料也約需 1710µs，須拆成小工作／閒置執行。
- 現有 `esp32cam` 網頁、登入、Wi-Fi、時鐘、OTA 與字元保存可沿用；以本機控制器取代 `Bridge`，移除遠端 Mega 的 heartbeat、USB Claim 與雙板版本依賴，狀態頁改為本機／MCP 故障。

運動任務必須獨立於 HTTP，單一任務持有 I²C。不能用每輪 `vTaskDelay(1)` 的毫秒級節拍當 800µs 排程，也不能在 timer ISR 裡呼叫阻塞的 Wire；可用定時通知喚醒高優先序任務，實際週期與合併策略由原型量測決定。NVS、匯入與 OTA Flash 寫入僅在所有馬達停止時執行，避免 Flash／cache 操作使步進延遲。

## 更新、保存與故障差異

移轉後日常只有一個 ESP32 `.sffw` 與一條 CP2102 USB，網頁 OTA 也只更新該主控。首次移轉仍需燒入單主控版本並重新接線；不需要安裝 Mega 新 Bootloader、ISP 或雙板更新器。CP2102 現有接法的 GPIO0／RESET 操作仍用於 ROM 燒錄及救援，不會因改成單主控而自動多出硬體自動下載電路。

Mega 安裝／校準改存 ESP32 NVS；移轉前先匯出現有設定，轉入時保留十組參數、順序、800／800 及既有網路／字元歷史。不能直接複製 AVR EEPROM 記憶體佈局；沿用明確序列化與版本驗證。ARM 與可信定位不自動恢復，重開後核對上次字元再允許轉動。

需特別處理 MCP 的保持輸出：ESP32 重開或 I²C 卡住時，MCP 可能仍保持最後相位。故障時先拉共用 RST、解除 ARM 與定位；STOP／OTA 須確認實際輸出已關，再寫 Flash。GPIO 控制的 RST 不能保證主控死機期間一定保持低，移除 Mega 後也失去獨立控制器的兩秒失聯停機。現有實體 12V 馬達支路開關仍保留，RST／重開機時 ULN 是否確實關閉須實測，不能以 I²C 清零成功作唯一依據。

## 建議方向與驗收

建議以 **ESP32-CAM＋四片現有 MCP23017** 為下一版候選，優先降低日後更新負擔；目前雙板版保留為已通過實機的基準。先用備用 MCP 做 1 位原型，驗證 400kHz／RST／800／800，再擴到 4 位、10 位並混合不同距離與校準。

正式替換前須量測 Wi-Fi 活動／慢 HTTP／時鐘切換下的相位間隔、抖動與零漏步，多輪 45 片／跨圈回同點；另測缺板、NACK、SDA 卡住、單主控 RESET、MCP RESET、供電順序、OTA／NVS 保存、1～10 位與非連續映射，確保停用輸出為低。原型通過後再整合完整手機功能、接線圖與 VS Code 流程，不把目前的頻寬計算當作實板驗收。

本次完成原始碼檢查、商家 RST 腳位表核對、六片庫存記錄與理論時序計算；沒有開啟串列埠、改接線、上傳韌體或控制馬達。
