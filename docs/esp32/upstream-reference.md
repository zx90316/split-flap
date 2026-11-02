# JonnyBooker 參考基準與 ESP32 移植界線

> 此文件為保留的 ESP32 方案；目前進度是 [Mega 2560 單顆調機](../../mega2560/README.md)。請勿混用接線與命令。

核對日期：2026-10-20。主要參考：[JonnyBooker/split-flap](https://github.com/JonnyBooker/split-flap)，固定 commit [`0c6ef692ac6a5d21a9bc858fa79ecbe816530d8d`](https://github.com/JonnyBooker/split-flap/tree/0c6ef692ac6a5d21a9bc858fa79ecbe816530d8d)（2026-03-29）。後續更新應重新比較，不以浮動 `main` 取代已驗證的基準。

## 維護狀態與來源

該版本 README 說明作者會盡力協助問題、更新相依套件，但沒有時間持續投入；2026-03-29 移除 stale workflow，2025-10-17 加入選配 mDNS。這是維護能力有限的參考專案，不是 ESP32 支援或持續維護保證。

本專案既有 Git 歷史起點為 David Königsmann 的 `14a22d9f38900aa520feeb70656357b8fd719e45`。JonnyBooker 同樣承接 David 的設計。改用 JonnyBooker 作主要參考並不改寫既有 commits，也不更動 GitHub fork parent。本地 `unit/`、`ESPMaster/`、`EEPROM_Write_Offset/` 是歷史 David 版，不應誤認為已同步成 JonnyBooker 版。

本次採納參考基準、機構相容性與功能規劃，並新增字元回歸測試；沒有把舊 ESP8266/Nano 韌體換名後冒充 ESP32 移植。本專案授權維持 [GPL-3.0](../../LICENSE)，原設計與程式來源歸 David Königsmann、JonnyBooker 及各貢獻者。

## 已核對的相容性

| 項目 | JonnyBooker 固定版本 | 本版決策 |
| --- | --- | --- |
| 組裝 PDF | 與本地檔案逐位元一致 | 保留機構頁；電子接線依 ESP32 文件 |
| PCB 三個製造檔 | 與本地 `PCB/` 逐位元一致 | 僅歷史參考，本版不用客製 PCB |
| 馬達／Hall | 28BYJ-48、ULN2003、KY-003 | 延續類型；本版採標準 BOM 的 12V 馬達，實際相序、電流與尺寸仍量測 |
| 字元 | 45 片；空白、A–Z、ÄÖÜ、0–9、: . - ? ! | 完整 45 索引回歸測試；替代 $&# 依原 PDF |
| 步數 | `STEPS=2038`，Arduino Stepper 全步 | 本版半步預設 4096 只是起點；不能把 2038 或 EEPROM offset 直接套用 |
| 方向 | `ROTATIONDIRECTION=-1` 配其 pin order | 本版 PH1–PH4 與可設定方向須實板測試，不直接照抄符號 |
| 歸零 | Hall 低有效、offset、跨圈重校準 | 保留本版離開有效區再找邊緣、debounce、時間／步數限位及故障狀態 |
| 通訊 | ESP8266 傳字元／速度給各 Nano | 本版 ESP32 直接排程 4 MCP 的 10 馬達，不沿用 Nano 位址或兩 byte 命令 |
| 調機序列 | 空白、Z、A、U、N、?、0、1、2、9 | 納入主機端字元映射回歸測試 |

PDF SHA-256：`b7fcecc557a10707f4dc042b7e03641b25d78079346c9726e202b3239da3d05a`。

JonnyBooker 的 [Unit/Unit.ino](https://github.com/JonnyBooker/split-flap/blob/0c6ef692ac6a5d21a9bc858fa79ecbe816530d8d/Unit/Unit.ino) 相較本地歷史版本主要更名 debug/test 巨集；不構成機構齒比已改版的證據。原 `2038` 是程式設計值，不是每顆現購馬達的齒比測量。

## 3D 模型

JonnyBooker 增加 [3dModels/README.md](https://github.com/JonnyBooker/split-flap/blob/0c6ef692ac6a5d21a9bc858fa79ecbe816530d8d/3dModels/README.md) 與 Models.zip 備份，說明來自同一 [Printables 模型](https://www.printables.com/model/69464-split-flap-display)。本版提供鎖定來源連結，不重複發布尚未獨立核對授權的 STL 包，也不宣稱此備份與目前發布頁完全相同。列印前核對版本、授權及尺寸，先試裝單字元。MCP／洞洞板固定支架與新模組孔位仍依實物設計。

## 功能分層與移植順序

JonnyBooker 的改善集中在 ESP8266 應用層：Wi-Fi 設定 portal、網頁 UI、長訊息切段、訊息排程、倒數、NTP／時區、OTA 與選配 mDNS。上述目前均未移植；不能直接刷其 ESPMaster 到 ESP32。

1. **已實作的控制層**：`firmware/esp32` 非阻塞狀態機、MCP 批次 I/O、歸零／校準、NVS、錯誤鎖定與串列命令。先完成單字元及十字元實體驗收。
2. **待實作的顯示應用層**：參考 JonnyBooker 的訊息切段、對齊、排程與倒數行為，透過有上限的命令佇列提交顯示目標。只由控制迴圈接觸 MCP，網頁 callback 不直接驅動馬達；完成、故障、忙碌狀態必須可回報。
3. **待實作的網路／時間層**：選用並鎖定適用 ESP32 的 Wi-Fi、Web、NTP 與時區相依版本，將配網與連線重試設為非阻塞。上游 ESP8266 的依賴版本與 LittleFS 上傳流程不能照搬。未同步時間時不執行絕對日期排程；處理斷線、重啟、夏令時間及重複任務。
4. **待實作的 UI 與維運層**：移植前逐檔保留來源／授權，改接新狀態 API；OTA 僅在馬達實體斷電及控制安全停機時執行，網頁不可宣稱軟體 STOP 等於物理斷電。憑證不放進 repo，mDNS 為可選。

不能直接採用上游訊息顯示中的多秒 `delay()`：本版集中控制十個馬達，長時間阻塞會觸發停滯保護或破壞步距。也不移植 Nano 的開機自動歸零、睡眠/TWI 暫存器程式或未限制長度的接收回呼。

## 模擬與驗收影響

此參考切換不改變 GPIO21/22/23、4 MCP 位址、Hall 接腳、相位映射、45 字元或標準 BOM。Wokwi 應繼續依本版 [接線文件](wiring.md) 和 ESP32 韌體重現。模擬的數位模型不代表已驗證 12V 電源、馬達扭力／齒隙、溫升、磁鐵距離或真實 MCP 時序。

執行 `bash firmware/esp32/test/native/run.sh` 驗證現有控制測試及新增字元序列。實物仍依 [組裝](assembly.md) 與 [驗收](validation.md) 逐關完成。

本次核對結果：20 項 motion 測試、bus／characters／adapter 三套測試皆通過；ASan＋UBSan 在 `ASAN_OPTIONS=detect_leaks=0 SANITIZE=1` 下通過。此環境的 ptrace 限制使 LeakSanitizer 無法執行，因此未驗證記憶體洩漏。ESP32 交叉編譯通過（espressif32 6.12.0／Arduino 2.0.17，RAM 22,776 bytes，Flash 304,873 bytes）。未完成實板測試。
