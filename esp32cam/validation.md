# ESP32-CAM＋Mega 驗證與實機驗收

日期：2026-10-25，Asia/Taipei。以下區分正式韌體建置、電腦測試與實機結果。本次沒有開啟 COM 埠、燒錄實板或驅動馬達。

## 正式建置

| 項目 | 結果 |
| --- | --- |
| Arduino CLI | 1.5.1 |
| Mega | `arduino:avr@1.8.8`；`arduino:avr:mega:cpu=atmega2560`；建置成功 |
| Mega Flash／全域 RAM | 31,852／253,952 bytes；2,362／8,192 bytes |
| ESP32-CAM | `esp32:esp32@3.3.12`；`esp32:esp32:esp32cam:PartitionScheme=min_spiffs`；建置成功 |
| ESP32 Flash／全域 RAM | 1,072,373／1,966,080 bytes；53,984／327,680 bytes |
| OTA 產物 | `esp32cam/dist/splitflap-esp32cam.sffw`；app 映像 1,072,528 bytes，封裝額外 64 bytes |
| 網頁 | 所有 HTML／CSS／JavaScript 隨韌體 gzip 提供，無外部 CDN |
| 診斷 | 本次建置無專案編譯警告；容量是編譯器統計，不含執行時的堆疊／動態記憶體峰值 |

`dist/firmware.json` 保存當次映像 SHA-256、板型、核心及協定版本。建置產物與下載的工具不提交 Git；重建可重新產生。

## 自動測試結果

| 範圍 | 通過內容 |
| --- | --- |
| Mega 工作流程 | 9 項 Python 測試：先建置、失敗停止、驗證上傳後才監看、埠號驗證、建置不開埠、工作目錄與子程序管理 |
| Mega 運動／多位核心 | 15 組 bench、6 組 panel；加減速、手動字元與跨圈、Hall 理想模型、故障、所有 DIGITS 1～10、非連續順序、批次預檢與逐顆輸出 |
| 實際 Mega `.ino` 介面 | 搭配 Arduino stub 執行；原 USB 指令相容、USB 轉動中輸入仍停止整批、Serial1 狀態不打斷運動、遠端 STOP 結束 USB WATCH、SAVE／LOAD 不恢復 ARM |
| 共用協定 | 4,000 組 COBS／CRC 封包；逐段輸入、雜訊、位元損壞、溢位、100ms 半包逾時、設定序列化與範圍驗證 |
| Mega EEPROM | 每次寫入前後模擬斷電；有效舊槽保留、提交後選新槽、壞 CRC／錯版本／非法設定拒絕、雙槽恢復與重新開機不動 |
| 遠端控制 | 接受／完成／拒絕／故障、重複與退役命令 ID、忙碌拒絕、STOP 優先、重新握手不重播、USB 接管、失聯解除 ARM／定位 |
| ESP32 工具／HTTP fixture | 12 項 Python 測試：建置／燒錄失敗流程、封裝長度／晶片／雜湊／版本、嵌入網頁一致、VS Code 任務與 SVG、模擬登入／CSRF、整份配置驗證、OTA 停止票證與失敗後失效 |
| 正式 C++ 網頁資料處理 | 全 45 個 UTF-8 字元、ä／ö／ü、NFC 組合、空白／別名、左中右對齊、長度、閏日／時區與各種位數；實際 cJSON 解析器測試錯型別、範圍、尾隨內容、NUL、深度及整份設定 |
| 兩板正式通訊核心 | `BridgeEngine` 直接連 `Remote`；模擬每毫秒 11 bytes 與 Mega 64-byte RX，所有 1～10 位每輪最多一顆通電；遺失 ACK／DONE／整個命令、快取退役、STOP、USB、OTA 維護鎖、斷線／重連、協定版本不符；Mega 保存後 ESP32 保存失敗能維持 job=fault／storage |
| 單向失聯 | 只切斷 ESP32→Mega；Mega 兩秒後停止，ESP32 即使仍收到狀態也進入不確定狀態；需 STOP／Claim，遲到命令不能重新啟動 |
| 慢速十位批次 | 十位各從空白到 !、SPEED 5000；模擬超過 186 秒仍維持正確待完成狀態，之後十位全部完成；不套用單顆三分鐘的錯誤總逾時 |
| VS Code 主機偵錯 | `test_bridge_debug.exe`／PDB 成功建置並執行相同核心測試；未宣稱已互動操作中斷點，也不是 ESP32／Mega 實板硬體偵錯 |

電腦上的 UART、EEPROM 與 Hall 都是理想替身。它們不能量測電壓、真實串列誤碼、GPIO 時序、馬達扭力、漏步、溫升、磁極或實際斷電的電氣行為。

## 手機網頁流程

隔離 Chrome／Playwright 1.62.1，390×844 手機尺寸及 1100×900 桌面尺寸，正式 HTML 連本機模擬 Mega。五組流程通過，未捕捉到 JavaScript 例外：

1. 未登入 API 拒絕；登入後手動記錄 Z、ARM、顯示 Ä；拒絕超長文字，未校時禁止啟動時鐘。
2. 三位依序 U7→U0→U2、手動定位、äA 正規化及補空白；拒絕 ß／中文，1～3 位不提供時鐘格式。
3. 五位 HELLO、手機校時、HH:MM、關閉／重開頁面保留已啟動工作、暫停及可用日期格式。
4. Wi-Fi 表單、匯出不含密碼／CSRF、非法重複位置整份拒絕、有效匯入、離線重連與重啟後全部未 ARM。
5. OTA 先 STOP；截斷檔、SHA-256 錯誤拒絕且不重啟；失敗訊息可重新準備；有效檔模擬更新／重啟，手機與桌面沒有頁面橫向溢出。

[手機預覽](ui-preview.jpg) 使用模擬資料。詳細結果及失敗截圖放 `esp32cam/test/.build/`。模擬器使用測試登入頁、記憶體設定與模擬 OTA；正式韌體的 Digest 驗證、FreeRTOS／Wi-Fi／NVS、HTTP 串流 Flash 寫入與每分鐘排程仍須實板驗收。關閉頁面測試確認模擬設備工作狀態保留；不是實際馬達或跨分鐘運轉紀錄。

## 重跑方式

```powershell
python mega2560/tools/dev.py test
python esp32cam/tools/dev.py test
python mega2560/tools/dev.py build
python esp32cam/tools/dev.py build
python esp32cam/tools/dev.py native-debug
python esp32cam/tools/dev.py test-web
```

工具準備與可選瀏覽器依賴見 [安裝指南](README.md)。一般建置不開啟 COM 埠。兩張 SVG 已轉圖人工檢查文字、電源分支、UART 方向、共地、CP2102 供電腳不接與 GPIO0 操作；沒有做電路模擬。

## 已有實機紀錄

使用者先前回報 U0 USB 單顆手動測試數輪無偏移：SPEED=800、ACCEL=800、REVSTEPS=4096、OFFSET=0、DIR=1、ORDER=1234、POLARITY=LOW，SHOW Z 後為 MANUAL／index 26／error=none。原始紀錄見 [Mega 驗證](../mega2560/validation.md)。這不代表本次 ESP32、其餘九位或 Hall 已通過。

## 實機驗收順序

所有未勾選項目仍待現場執行，完成時記錄板號、參數、日期與結果。

- [ ] **供電**：關馬達分支，空載調 LM2596 5.0V；核對正負、共地與 TXU0202 腳位／兩個 100nF。兩板負載時量 5V、3V3 與壓降；Mega 電腦 USB 和降壓器供電以同一插頭互斥。
- [ ] **燒錄／安全開機**：先更新 Mega，再經 CP2102 更新 ESP32。GPIO0 跳線移除、RESET 後取得 ACCESS；兩板開機均不轉、不 ARM。
- [ ] **板間 UART**：Mega D18／D19 經轉換接 ESP GPIO14／13；網頁連線穩定，讀狀態不讓運動停止。先不開馬達分支確認。
- [ ] **首次手機登入**：直連 AP、設備隨機密碼及 admin Digest 登入，未登入／錯 CSRF 操作拒絕。家用 Wi-Fi、IP、splitflap.local、重連及 AP 備援可用。
- [ ] **U0 手動定位**：只啟用一位；記錄實際字元、ARM，先短距離再跨圈，核對目標、45 字元及重複停點；確認 STOP 釋放線圈並使位置失效。
- [ ] **手機離線**：文字移動中關頁面應完成；校時啟動時鐘後關閉頁面，跨多個分鐘確認仍運作。忙碌跨分鐘時完成後只追最新目標。
- [ ] **兩秒失聯**：遠端運動時斷開單向 UART、雙向 UART 或 ESP32 電源，分別確認 Mega 停止、解除 ARM／定位；重連不重播，須重新定位並啟用。獨立 USB 調機不受未連接 ESP32 影響。
- [ ] **USB 接管**：USB 控制後網頁提示接管；時計暫停；Claim／STOP 後不會自行恢復運動。
- [ ] **持久化**：保存校準、位數／順序、Wi-Fi／偏好後重開兩板，設定恢復但位置／ARM／自動顯示不恢復。匯入先拒絕非法檔，再確認有效檔完整套用。
- [ ] **其餘位置**：逐顆驗 U1～U9 的 IN1～IN4、電源與運動，再測實際使用位數、非連續順序與整串文字；未啟用輸出維持關閉。
- [ ] **磁鐵／Hall**：裝磁鐵後量有效極性、原始／去抖狀態、HOME、三圈 MEASURE、OFFSET 與 CYCLE；本次沒有真實磁鐵驗收。
- [ ] **實際 OTA**：停止確認後先驗錯檔拒絕，再測有效更新；測中斷上傳可重試、上傳時 UART 仍有心跳、Mega 失聯／USB 啟動會中止。更新後設定保留、仍需定位與 ARM；中斷供電測試在已停止且備有 USB 復原方式時做。
- [ ] **長時間負載**：實際多位文字／時鐘、5V 穩壓、接點壓降、馬達／ULN／降壓器溫升、抗干擾與實體馬達支路切斷。

軟體 STOP 不能切斷 12V，也不能在控制器／供電故障時保證關閉。接線依 [逐線表](wiring.md)與兩張圖；新位置先逐顆確認，再擴充。
