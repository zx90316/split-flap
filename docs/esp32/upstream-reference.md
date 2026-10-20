# JonnyBooker Git 基底與 ESP32 移植界線

核對日期：2026-10-20。實際 Git 基底：[JonnyBooker/split-flap](https://github.com/JonnyBooker/split-flap)，固定 commit [`0c6ef692ac6a5d21a9bc858fa79ecbe816530d8d`](https://github.com/JonnyBooker/split-flap/tree/0c6ef692ac6a5d21a9bc858fa79ecbe816530d8d)（2026-03-29）。後續更新應重新比較，不以浮動 `master` 取代已驗證的基準。

## 維護狀態與來源

該版本 README 說明作者會盡力協助問題、更新相依套件，但沒有時間持續投入；2026-03-29 移除 stale workflow，2025-10-17 加入選配 mDNS。這是維護能力有限的參考專案，不是 ESP32 支援或持續維護保證。

新分支 `esp32-jonnybooker-base` 直接建立於上述 JonnyBooker commit，移入原 `esp32-purchased-bom` 的六筆自訂修改。保留的 `Unit/`、`ESPMaster/`、`EEPROM_Write_Offset/`、`3dModels/`、PCB、組裝 PDF 與上游 workflows 皆來自該基底，未以 David 舊版覆蓋。完整上游 README 保留在根目錄 README 的上游章節。

舊分支原基底為 David Königsmann 的 `14a22d9f38900aa520feeb70656357b8fd719e45`；舊分支、PR #1 與 `main` 不改寫。GitHub fork parent 仍是 David 的倉庫，不會隨分支基底自動更改。`Instructions/README.upstream.md` 保留的是遷移前的 David 歷史 README，而不是目前 Jonny 基底的說明。

移入的原始提交（依序）：
- `5366376e455b3442eb4ed995a9e66b2d3bb43e90`：ESP32 韌體及初始 BOM
- `313e88f18c3bfdeb43491cfa8d9ccea8a766ec11`：標準 BOM 與組裝文件
- `86c597a6bbeb60c883bf0ef5ddea0dba948a4a8a`：Jonny 相容性及移植界線
- `6208ddde2980ee88f70e10933cd0c3c7e778dd92`：十通道 Wokwi 接線診斷
- `9c70a70952c6b59f601c4abfe2bbfd6e3e0a5207`：正式韌體閉迴路模擬
- `69177a45efcdb3bbd461fbf6c4f74d62f58a3ac8`：瀏覽器驗收紀錄

本次是實際 Git 祖先遷移，不只是更換參考連結；不包含 ESP8266 網路功能的 ESP32 移植。本專案授權維持 [GPL-3.0](../../LICENSE)，原設計與程式來源歸 David Königsmann、JonnyBooker 及各貢獻者。

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

JonnyBooker 的 [Unit/Unit.ino](https://github.com/JonnyBooker/split-flap/blob/0c6ef692ac6a5d21a9bc858fa79ecbe816530d8d/Unit/Unit.ino) 相較遷移前 David 歷史版本主要更名 debug/test 巨集；不構成機構齒比已改版的證據。原 `2038` 是程式設計值，不是每顆現購馬達的齒比測量。

## 3D 模型

JonnyBooker 增加 [3dModels/README.md](https://github.com/JonnyBooker/split-flap/blob/0c6ef692ac6a5d21a9bc858fa79ecbe816530d8d/3dModels/README.md) 與 Models.zip 備份，說明來自同一 [Printables 模型](https://www.printables.com/model/69464-split-flap-display)。本分支原樣繼承該基底的 `3dModels/` 及 Models.zip，未另外下載或修改模型，也不宣稱此備份與目前發布頁完全相同。列印前核對版本、授權及尺寸，先試裝單字元。MCP／洞洞板固定支架與新模組孔位仍依實物設計。

## 功能分層與移植順序

JonnyBooker 的改善集中在 ESP8266 應用層：Wi-Fi 設定 portal、網頁 UI、長訊息切段、訊息排程、倒數、NTP／時區、OTA 與選配 mDNS。這些原始碼已隨 Git 基底保留，但上述功能目前均未移植到 `firmware/esp32`；不能直接刷其 ESPMaster 到 ESP32。

1. **已實作的控制層**：`firmware/esp32` 非阻塞狀態機、MCP 批次 I/O、歸零／校準、NVS、錯誤鎖定與串列命令。先完成單字元及十字元實體驗收。
2. **待實作的顯示應用層**：參考 JonnyBooker 的訊息切段、對齊、排程與倒數行為，透過有上限的命令佇列提交顯示目標。只由控制迴圈接觸 MCP，網頁 callback 不直接驅動馬達；完成、故障、忙碌狀態必須可回報。
3. **待實作的網路／時間層**：選用並鎖定適用 ESP32 的 Wi-Fi、Web、NTP 與時區相依版本，將配網與連線重試設為非阻塞。上游 ESP8266 的依賴版本與 LittleFS 上傳流程不能照搬。未同步時間時不執行絕對日期排程；處理斷線、重啟、夏令時間及重複任務。
4. **待實作的 UI 與維運層**：移植前逐檔保留來源／授權，改接新狀態 API；OTA 僅在馬達實體斷電及控制安全停機時執行，網頁不可宣稱軟體 STOP 等於物理斷電。憑證不放進 repo，mDNS 為可選。

不能直接採用上游訊息顯示中的多秒 `delay()`：本版集中控制十個馬達，長時間阻塞會觸發停滯保護或破壞步距。也不移植 Nano 的開機自動歸零、睡眠/TWI 暫存器程式或未限制長度的接收回呼。

## 模擬與驗收影響

此次 Git 基底遷移不改變 GPIO21/22/23、4 MCP 位址、Hall 接腳、相位映射、45 字元或標準 BOM。Wokwi 應繼續依本版 [接線文件](wiring.md) 和 ESP32 韌體重現。模擬的數位模型不代表已驗證 12V 電源、馬達扭力／齒隙、溫升、磁鐵距離或真實 MCP 時序。

執行 `bash firmware/esp32/test/native/run.sh` 驗證現有控制測試及新增字元序列。實物仍依 [組裝](assembly.md) 與 [驗收](validation.md) 逐關完成。

本次核對結果：20 項 motion 測試、bus／characters／adapter 三套測試皆通過；ASan＋UBSan 在 `ASAN_OPTIONS=detect_leaks=0 SANITIZE=1` 下通過。此環境的 ptrace 限制使 LeakSanitizer 無法執行，因此未驗證記憶體洩漏。ESP32 交叉編譯通過（espressif32 6.12.0／Arduino 2.0.17，RAM 22,776 bytes，Flash 304,873 bytes）。未完成實板測試。

## Git 基底遷移驗證（2026-10-20）

- 固定 Jonny 基底共 34 個已追蹤檔案；除根 README 增加 ESP32 說明外，其餘 33 個逐位元保留。原 README 全文亦逐位元保留於根 README 的上游章節。
- `firmware/esp32/` 與 `simulation/wokwi/` 共 32 個檔案與舊分支 `69177a45` 逐位元一致，包括正式韌體匯出 sketch、custom chips 與瀏覽器驗收紀錄。
- 本次重新通過五套 native 測試（含 20 項 motion 及正式 setup/loop/parser 閉迴路）、韌體匯出一致性、接線圖檢查與 Wokwi rotor C callback 測試。
- 五套 native 測試以 ASan＋UBSan 重新通過；仍使用 `detect_leaks=0`，未宣稱通過洩漏偵測。
- 本次遷移環境未安裝 PlatformIO，沒有重新交叉編譯；前述 ESP32 編譯與瀏覽器驗收為既有紀錄，不能冒稱本次重跑。因韌體及模擬檔案未變，保留原驗收證據，沒有重新修改線上 Wokwi 專案。實物驗收仍待完成。
