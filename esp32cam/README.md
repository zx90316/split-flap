# NodeMCU ESP-32S＋Mega2560 Pro 手機翻頁控制台

目前為 **1.2.0 新硬體版**：NodeMCU ESP-32S 提供手機網頁、Wi-Fi、時鐘／日期、設定保存與 ESP32 OTA；Mega2560 Pro 負責 1～10 位並行步進、手動／Hall 定位與 EEPROM 校準。沿用現有 P011 電平轉換及 LM2596，不使用 MCP23017。

源碼目錄沿用 `esp32cam/`，但本頁與工具現在針對 **classic ESP32／ESP-WROOM-32 的 NodeMCU ESP-32S**；不適用已損壞的 ESP32-CAM、C3 或 S3。舊硬體實測與 1.1.0 基準保留於[歷史紀錄](../docs/dual-controller-baseline.md)。單主控與雙板統一更新文件仍屬獨立研究，未混入本次實作。

**[新購硬體與設定搬移](../hardware/purchased-2026-10-26.md) · [逐線表](wiring.md) · [正常接線圖](wiring-runtime.svg) · [板上 USB 燒錄圖](wiring-flashing.svg) · [驗證紀錄](validation.md)**

## 這次改了什麼

- 改用 NodeMCU ESP-32S 板上 USB 燒錄；不再接外部 CP2102 或 GPIO0 跳線。移除 CAM 專用 GPIO32／GPIO4 操作，PSRAM 關閉。
- 使用 4MB、DIO／40MHz、`min_spiffs` 的 OTA 配置；預設建置會產生 `dist/splitflap-esp32s.sffw`。新板識別拒絕舊 CAM 更新檔，USB／OTA 都不能選舊 `splitflap-esp32cam` 產物。
- Mega Pro 沿用 ATmega2560 邏輯腳位及正式韌體；板子排針位置、公母頭及 USB 接頭依實物重新接，不照標準 Mega 的插座位置搬接。
- 保留 800／800 預設、各位同時起步／各自停止、任意 1～10 位／非連續映射、字元歷史、登入、離線資源及原有通訊保護。速度／加速度變更不清除可信位置與 ARM。
- 四張設定頁內建 SVG、VS Code F5、BOM 與供電切換步驟同步更新。已購保險絲座不預設兩路 5A；0.5mm 銅芯直徑線不作馬達共用主幹。
- DIP 暫列備用低電流設定零件；不接 GPIO、不切馬達主電流。使用者確認繼電器維持選配，本版仍需合適的手動馬達開關。

## 先完成接線與搬移

![正常運作](wiring-runtime.svg)

12V 在 F1／總開關後分兩路：馬達支路經獨立手動開關供 ULN；控制支路經 LM2596 調至 **5.0V**，再經 F2 供兩板及 5V 周邊。全部共地，+12V／+5V 正極分開。

- 兩塊新板先各自只接 USB 驗證。外部 5V 要接已核對用途的 5V 電源腳；NodeMCU 若只印 VIN，先確認其為可用的 5V 輸入，不送入 12V。兩板 3V3 都不接 5V。
- **任一板接電腦 USB 前，關電並拔該板外部 5V 與板間四條 UART 訊號線。**現有 P011 不假設單側斷電隔離。接線／復原順序見[逐線表](wiring.md#usb-燒錄與供電切換)。
- UART 沿用：GPIO13→A0／B0→Mega D19；Mega D18→B1／A1→GPIO14。VCCA=ESP 3V3、VCCB=Mega 5V，115200、8N1。
- **新板不會自動取得舊板設定。**新 Mega Pro 須匯入原校準，或依[搬移紀錄](../hardware/purchased-2026-10-26.md#到貨與設定搬移)手動還原。原 U0 的 OFFSET=273 已有現場確認；其他位不可照抄。新 ESP32 從 USB ACCESS 取得新登入資料，重新設定 Wi-Fi；字元仍需核對實物後建立定位。
- 四個 KF301 是需焊接的 PCB 端子，不是完成的十路配電板；DIP 不代替直流電源開關。配件數量與限制見[已購清單](../hardware/purchased-2026-10-26.md)。

## VS Code：準備、建置與板上 USB 燒錄

在 repo 根目錄開啟 VS Code，安裝 Python、C/C++ 擴充套件；[Mega VS Code 指南](../mega2560/vscode.md)提供 Arduino CLI／AVR／主機測試工具準備方式。

1. F5 選 **ESP32-S: 準備環境**，固定 Arduino-ESP32 **3.3.12**；現有 Mega 的任務保留。
2. 依供電切換表拆外部 5V／UART，關馬達電源，Mega Pro 用板上 USB 接電腦。先查埠，再選 **MEGA: 建置＋燒錄＋串列除錯**；板型仍是 `arduino:avr:mega:cpu=atmega2560`。若商品沒有 bootloader，需另行處理，不能僅靠 CH340G 上傳。
3. NodeMCU ESP-32S 也拆外部 5V／UART，以板上 USB 資料線接電腦。F5 選 **ESP32-S: 查詢連接埠**，使用其新 COM 埠，不沿用舊 CP2102 的 COM4。
4. F5 選 **ESP32-S: 建置＋燒錄＋串列監看**。通常自動重設燒錄；停在 Connecting 時按住 BOOT、按一下 EN／RESET，開始寫入再放開 BOOT。完成後若未啟動，按 EN／RESET；不接 GPIO0 跳線。
5. 串列 115200，`ACCESS` 查看新 Wi-Fi／登入資料；`STATUS` 看 Mega 連線。尚未恢復 UART 時顯示 offline 是正常的。
6. 結束監看、拔 USB、確認控制電源關閉，再接回外部穩壓 5V／UART；兩板同時供電。取得 `Mega=connected` 後先還原設定、定位與 ARM，再測 U0。

![兩板 USB 燒錄](wiring-flashing.svg)

等效指令：

```powershell
python esp32cam/tools/dev.py setup
python esp32cam/tools/dev.py build
python esp32cam/tools/dev.py ports
python esp32cam/tools/dev.py upload-monitor --port COM4
```

COM4 僅為範例，必須換成這塊 NodeMCU 的埠。建置不開啟 COM；燒錄前重建，失敗即停，不上傳舊檔。監看只在上傳成功後開始。這次兩板仍各自 USB 更新，網頁 OTA 僅更新 ESP32。

ESP32 建置 FQBN：

```text
esp32:esp32:esp32:PartitionScheme=min_spiffs,FlashSize=4M,FlashMode=dio,FlashFreq=40,PSRAM=disabled
```

使用可指定 OTA 分割的 ESP32 Dev Module 相容板型，GPIO 以數字配置。新 NodeMCU 預期為 4MB ESP-WROOM-32，到貨後核對；沒有宣稱此相容設定已完成實板驗收。

### Windows 找不到 COM

COM 來自兩塊板子各自內建的 USB-UART。先只接一塊板、使用資料線，查裝置管理員；依實際 VID/PID 與 USB-UART 晶片安裝官方簽章驅動。Mega Pro 選項標示 CH340G；NodeMCU 晶片型號到貨核對，不因舊 CP2102 驅動已安裝就假設新板一定相同。

`Serial Port (USB) Unknown` 表示工具未辨識 Arduino 型號，不等於驅動失敗。查不到埠先檢查線材／USB／驅動；BOOT 只影響 ESP32 下載模式，不影響 USB-UART 本身的列舉。

## 手機第一次連線

1. 手機加入串列 `ACCESS` 顯示的 **SplitFlap-xxxxxx** Wi-Fi，輸入該設備的隨機直連密碼。手機若提醒「沒有網際網路」，選擇保持連線。
2. 用一般瀏覽器開 `http://192.168.4.1/`。瀏覽器登入視窗使用帳號 **admin**，密碼為 `ACCESS` 顯示的 **Admin password**；兩個密碼分開產生。
3. 到「裝置設定 → 網路與登入」填家用 Wi-Fi，保存後手機改連同一個家用網路。**不需要先連接 Mega，也不需要定位／ARM。** 可開 `http://splitflap.local/`，或用串列 `STATUS`／網頁顯示的 STA IP。
4. 設備直連網路會保留，家用 Wi-Fi 斷線時仍可直連。密碼不寫進 repo、狀態 JSON 或匯出檔。

若忘記密碼，在板上 USB 串列輸入 `ACCESS`。`NETWORK RESET` 會先請求停止，重設 Wi-Fi／登入密碼；不清除 Mega 校準。更換網路設定會暫停自動顯示，需要手動重新啟動。

歷史修正：ESP32 韌體 **1.0.1** 修正「Mega 尚未連線時，保存網路設定按鈕被停用」的問題，該單一修正不需更新 Mega；1.1.0 的功能需配合 Mega 封包韌體；1.2.0 換新板時，網路與登入須重新設定。連線中的 Mega 正在運動、保存或監看，以及 ESP32 正在處理工作／OTA 時，仍需等待或停止後再保存網路設定。

## 沒有磁鐵：先測一位，再擴充

1. 「裝置設定 → 安裝配置」選 **1** 位。位數可任選 1～10；自訂模式例如三位填 `0,2,7`，即依序使用 U0、U2、U7。只啟用實際接上的組別，現有線路無法偵測插拔。
2. 在「顯示 → 手動定位」選出每位**實際可見**字元，按「記錄目前字元」。這一步不轉動，並解除該定位操作涉及位置的 ARM。
3. 確認機構可動，開啟馬達支路，按「允許已選位置轉動」，輸入目標文字並送出。各位同時起步，各自到位停止；與目標相同的位置略過。
4. 文字太長或含未支援字元會拒絕。短文字按對齊方式補空白；清空輸入會把所有位翻到空白。45 片順序為空白、A–Z、Ä Ö Ü、0–9、`: . - ? !`。`_` 當空白，`$ & #` 亦可代表 Ä Ö Ü。
5. 調速／加速度保留定位與轉動許可；相序等幾何校準、STEP／REV、STOP 或重開機後，依顯示的定位狀態核對實際字元。網頁保留最後字元，按「確認字元並允許轉動」可一次完成手動定位與 ARM。不能從通訊成功判斷馬達是否真的轉到位。

U0 已由使用者回報多輪通過的參數是 **SPEED=800、ACCEL=800、REVSTEPS=4096、OFFSET=0、DIR=1、ORDER=1234、POLARITY=LOW**。此組現已成為預設速度／加速度；其餘機構仍需依實際負載驗證，不能把單顆實測當成十顆保證。修改後按「保存目前 Mega 參數」才會寫入 EEPROM；「套用並保存」的安裝／校準表單則會直接保存整份設定。

Hall／HOME／MEASURE／CYCLE、LED 相序檢查及單顆 STEP／SHOW 都在「調機與狀態」。LED 測試須拔所選馬達；HOME／MEASURE 須先裝磁鐵並確認 Hall 訊號。

## 時鐘／日期

先完成全部定位與 ARM ALL，再選可用格式、台北 UTC+08:00 及對齊方式。能連上網際網路時自動 NTP 校時；直連離線可按「使用手機時間校時」。斷電後沒有外接 RTC，須重新取得網路時間或手機校時。

| 格式 | 最少位數 |
| --- | --- |
| HHMM／MMDD | 4 |
| HH:MM／MM-DD | 5 |
| YYYYMMDD | 8 |
| YYYY-MM-DD | 10 |

時鐘按分鐘更新，日期按內容變動更新；只轉動變更的位置。忙碌時不累積舊時間工作，完成後重新計算最新目標。關閉網頁、手機離開 Wi-Fi 不會中斷；「暫停自動顯示」讓已接受的工作完成，「全部停止」會停止並使位置失效。USB 調機、兩板失聯或定位失效會暫停自動模式，恢復後需手動啟動。

## 保存、備份與網頁更新

- **Mega EEPROM**：有版本／CRC 的雙槽，保存顯示順序和十組校準。逐 byte 寫入，最後寫提交標記；上一份有效設定保留。無有效資料時只啟用 U0。開機不 ARM、不 HOME；只有首次舊版設定遷移會轉存新版記錄。
- **ESP32 NVS**：保存 Wi-Fi／登入、顯示偏好及最後完成字元。每次字元有變化且全組停止後保存 13-byte 版本／CRC 記錄；相同字元不重寫，失敗限速重試。未完成動作不覆寫歷史；不保存「正在運作」、可信定位或 ARM。重開後由使用者確認歷史值才建立新的手動定位。
- **匯出／匯入**：JSON 備份安裝、校準與顯示偏好；不含密碼、位置或 ARM。整份資料驗證成功才接受，保存完成後重新定位。先保存 Mega，再保存 ESP32 偏好；兩板都成功才回報完成。若第二步儲存失敗會回報故障，Mega 已保存的設定保留，修復後重新匯入。
- **OTA**：先按「停止並準備更新」，等 Mega 回覆且狀態確認全關，再上傳 `esp32cam/dist/splitflap-esp32s.sffw`。包含專案識別、協定、映像長度、ESP32 chip ID 與 SHA-256 驗證；失敗／中斷不選取未驗證映像。更新成功後重開機，重新定位與 ARM。

`.sffw` 是完整性封裝，不是數位簽章；只使用本專案建置的檔案。管理介面以裝置密碼與 Digest／CSRF 保護，適用現場直連或可信任區網，不提供公網遠端服務。OTA 更新 ESP32，Mega 仍由 USB 上傳。

## 開發與實機驗收

```powershell
python mega2560/tools/dev.py test
python esp32cam/tools/dev.py test
python esp32cam/tools/dev.py mock-web
# 開 http://127.0.0.1:8765/，模擬器登入 admin / MockPassword123；不接硬體。
```

另有 F5 **ESP32-S: 手機網頁流程測試**，在隔離的 Chrome 視窗驗證手機操作、設定備份、斷線／重啟與 OTA 錯檔／重試，不連接硬體、不讀取個人瀏覽器資料。需要 Node.js、已安裝的 Chrome 與 Playwright；一般建置／燒錄不需要這些工具。首次使用可執行：

```powershell
npm --prefix esp32cam/test install
python esp32cam/tools/dev.py test-web
```

測試前先建置產生 `.sffw`。可用 `SF_NODE`、`SF_PLAYWRIGHT_MODULE`、`SF_WEB_CHROME` 指定已安裝工具路徑；結果與失敗截圖放在忽略版本控制的 `esp32cam/test/.build/`。此模擬器登入及 OTA 都是測試替身，不能代替實板 Digest、Wi-Fi、UART 或 Flash 更新驗收。

[協定與介面](protocol.md) · [接線與供電](wiring.md) · [軟體結果／實機驗收表](validation.md)

手機頁面預覽（電腦模擬資料）：

<img src="ui-preview.jpg" alt="手機翻頁控制台顯示 HELLO，模擬資料" width="390">

依序驗收：量供電 → 關馬達支路驗 UART／狀態 → U0 手動定位與文字 → 網頁關閉後繼續 → 拔 UART 兩秒停止 → 兩板重開機載入設定且不動 → 逐顆確認其餘位置 → 多位文字／Hall → OTA。軟體停止不能切斷 12V，實體分支開關仍需保留。
