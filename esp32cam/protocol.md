# ESP32↔Mega 協定 v1

兩端共用 `libraries/SplitFlapLink/src/SplitFlapLink.h`，115200 baud、8N1。Mega Serial1=D19 RX／D18 TX；ESP32 UART2=GPIO14 RX／GPIO13 TX。USB 人工指令仍是原本的 Serial 115200。

實體電平轉換使用現有 P011 8 路 MOS 板的兩路：ESP GPIO13→A0／B0→Mega D19、Mega D18→B1／A1→ESP GPIO14；VCCA=ESP3V3、VCCB=Mega5V，全部共地。兩板皆供電才接訊號，單板斷電／切換供電前先拔四條訊號線；這片板不假設具斷電隔離。詳見 [接線與供電切換](wiring.md)。協定速率未改，電腦測試不能證明實板的上升時間或串列穩定性。

## 封包

先組 raw bytes，再 COBS 編碼，以 `0x00` 結尾。多 byte 整數均 little-endian。

| 欄位 | 大小 |
| --- | --- |
| version=1、kind | 各 1 byte |
| session、request id | 各 uint32 |
| payload length | uint8，0～160 |
| payload | length bytes |
| CRC16 | uint16，涵蓋 header 與 payload |

CRC 為 CCITT-FALSE，poly 0x1021、init 0xffff；`123456789`→0x29b1。最大 raw 173 bytes、含 delimiter 的 wire 175 bytes。損壞、過長、長度不符、CRC 錯誤均不執行；未完成封包停頓超過 100ms 後丟棄到下一個 delimiter。

kind 依序為 Boot=1、Hello=2、Welcome=3、Ping=4、Pong=5、Command=6、Result=7、Query=8、Panel=9、Unit=10、Config=11。

ESP32 使用每次開機／重連不同的非零 session，Hello 的 payload 空白；Welcome 回 `{protocol, megaFirmwareMajor}`。只有正確 session／版本才接受命令。Mega Boot 與兩秒無有效回覆使 ESP32 取消等待中的舊工作，重新握手，不重送動作。

## 命令與結果

Command payload 開頭為 `{op, physicalUnit}`，unit=0～9 或 ALL=255。動作、範圍、參數及 enabled 狀態由 Mega 最終驗證。opcode 定義於共用 header；數字命令參數為 uint16，索引 uint8，文字為 `{count, glyphIndices[count]}`，安裝清單為 `{count, units[count]}`。完整設定為固定 121-byte 序列化，包含 count、10-byte map、十筆 11-byte 校準。

Result payload 為 `{stage, error, op}`，stage=Accepted1／Done2／Rejected3／Fault4；HTTP 202 只表示 ESP32 已接受傳送，不代表馬達完成。STEP、TEXT、HOME、MEASURE、CYCLE 與 EEPROM 保存會在完成後另外回 Done／Fault。

Mega 保留八筆命令結果，重複 ID／相同 payload 回既有結果，不重複執行；相同 ID／不同內容拒絕。已退役的舊 ID 即使離開快取也不會重新執行。每 session 的命令 ID 單調遞增。

ESP32 **只送一次動作封包**；需要補查結果時送 Query，id 指向該命令。Mega 回既有 Accepted／Done／Fault；不知道的 ID 回 Unknown，絕不從查詢開始動作。Unknown 會使 ESP32 禁止其他動作，直到使用者 STOP／Claim 確認恢復。

STOP 優先於其他等待工作。UART 已送出一部分的 frame 先完成，再送 STOP，以免破壞 framing。沒有一般動作排隊；網頁及自動模式同時最多一項工作。每項 TEXT／HOME ALL／CYCLE ALL 內的啟用位置並行起步，各自完成；與目標相同的位置不動。STOP 終止整批所有位、解除全部 ARM、使可信定位失效；ESP32 字元歷史保留供核對。

## 狀態、心跳與 USB

Ping／Pong 每 500ms；Query 與狀態分頁不影響運動。Mega 的收送每輪都有 byte 預算，使用 `availableForWrite`，不等整行、不 flush、不在轉動中列印長篇 USB 狀態。狀態包含 panel、十筆 unit、完整設定，ESP32 收齊相同 snapshot ID 才發布，資料年齡超過兩秒時不接受新動作。

單位回報 enabled、模式、定位來源、索引、目標、ARM、原始／去抖 Hall、步數／剩餘、三筆量測、校準及錯誤。記錄值是韌體估計；手動模式無法證實硬體存在或漏步。

只有遠端 ARM／運動／WATCH 所取得的控制，才受 Mega 的兩秒心跳租期保護。租期到期停止全組、解除 ARM 與定位；同 session 遲到的命令也會拒絕，須 STOP／Claim 或重新握手。即使只有 ESP32→Mega 這一路斷線、ESP32 仍收到狀態，也會顯示不確定並停止自動模式。ESP32 的 UART 工作使用獨立 FreeRTOS task，可在一般 HTTP 等待期間工作；Flash 擦寫仍可能暫停任務／中斷，不能視為 UART 絕不中斷的保證。手機關閉頁面不會停止心跳。

USB 轉動期間任何非換行字元仍依原行為停止並丟棄整行；USB 人工控制使網頁進入 UsbOverride，自動顯示暫停。USB STATUS／HELP 在閒置時不接管。恢復時按「接管網頁控制」（Claim），解除 ARM 與定位，再由使用者重新設定。ESP32 未連接時，USB 調機可獨立使用。

## Web API（ESP32 1.3.0）

首頁公開載入；控制、設定、接線圖與 OTA 使用持久 `SFSession` Cookie。沒有 Digest 或 `WWW-Authenticate`。Cookie 為 HttpOnly／SameSite=Strict／Path=/，目前 HTTP 區網服務不使用 Secure 屬性。每個登入使用 32 字元隨機值，NVS 僅保存 SHA-256 摘要，8 個獨立槽，滿時替換最舊槽。開頁更新 Cookie 保存期限；登出撤銷本槽，改密碼撤銷全部槽。登入連續失敗 5 次限制一分鐘。

已登入 POST 要求 `X-CSRF-Token`，有 Origin 時須符合 Host。登入僅接受 JSON，同樣驗證 Origin。OTA 在 upload START 與最終 handler 都檢查登入／CSRF／ticket；登入失效回 JSON 401，不觸發瀏覽器彈窗。

- `GET /api/session`：`authenticated`；已登入另回 csrf 並延長 Cookie。
- `POST /api/login`：`{username:"admin",password}`，成功設定 Cookie 並回 csrf。
- `POST /api/logout`：撤銷目前 Cookie。
- `GET /api/state`：原連線、校準、工作、網路、顯示與 OTA 狀態；增加 workflow 的 active、calibrating、calibrated、recovering、step、steps、jogReadyMask、message，以及每位 calibrated。rememberedIndex 獨立於 reference，不以歷史直接建立可信位置。state 仍保留 mega.units 供診斷；新版控制只接受連續排列。
- `POST /api/command`：`{boot,request,op,unit,value?,align?}`。位數外的單元拒絕。網頁不接受 ARM／DIGITS／UNITS，安裝設定統一走 config。動作由 ESP32 組成有界流程，按需 ARM、HOME，再執行目標；USB／Mega 協定的 ARM 不變。
- `POST /api/calibration`：`{boot,request,action,unit?,value?}`。action 為 start、preview、jog、finish、cancel；preview 的 value 是絕對偏移，jog 僅接受 1／10／100。所有操作回 parent job。preview／jog 只改 Mega RAM；finish 全部 HOME 成功後 ApplyConfig 保存，再記錄 ESP32 校準；cancel 恢復原設定。
- `GET /api/config`：schema 2、device=`splitflap-esp32s`、mega=`{digits,calibration[10]}`、display。無位置、憑證或校準確認標記。
- `POST /api/config`：加入 boot／request，支援 schema 2 或連續排列的 schema 1；舊 device=`splitflap-esp32cam` 仍可匯入。非連續清單拒絕。先存 Mega，再存 ESP32；後者失敗回 fault/storage。
- `POST /api/network`：ssid，可選 password、apPassword、adminPassword。省略密碼欄位保留。
- `POST /api/preferences`：mode=`text|clock|date|weekday|carousel`；clockFormat、dateFormat、weekdayFormat、timezone（分鐘）、align、leadingZero、limited、windowStart／windowEnd（當地分鐘）、autoRecovery；items 為 1～8 筆 `{mode,format,text,seconds,align}`。只傳 run=false 可暫停；run=true 另需 boot／request，回準備流程 job。保存模式與開始分開。
- `POST /api/time`：`{unix:secondsUTC}`，2024～2099。
- `POST /api/ota/prepare`：boot／request，取得 STOP job 與一次性 ticket；等 otaReady。
- `POST /api/update`：multipart `.sffw`，CSRF 與 `X-OTA-Ticket`。

外部 request 必須等於 nextRequest；同 ID／同內容回既有結果，不重複動作；退役或不同內容拒絕。多頁撞號需由使用者重新操作，前端不自動重送。八筆外部結果與八筆內部結果分開，避免時鐘更新擠掉手機的流程結果。

流程每個子命令只送一次，等待 Done 與更新後的完整狀態快照，再送下一步。失聯、換 session、USB 接管、子命令失敗或 185 秒子命令逾時中止；STOP 可取消流程。無一般動作排隊。

復電僅在 ESP_RESET 原因為正常上電、已開復電功能、全部校準簽章符合且 NVS 正常時執行。最多等連線 30 秒；校準簽章涵蓋圈步數、偏移、方向、相序、極性，不涵蓋速度／加速度。顯示歷史只由完成的顯示工作更新，校準／HOME／測試不覆寫；動態畫面保存最多每分鐘一次。秒數採完成後計算最新目標，不積壓過期工作。

## OTA 封裝

NodeMCU ESP-32S 1.2.0～1.3.0 共用的 64-byte header：magic `SFESP01\0`（8）、映像長度 uint32、協定 uint32、SHA-256（32）、專案 tag `splitflap-32s-v1`（16）。後接 app `.bin`，大小最多 1,966,080 bytes；晶片必須 ESP32。工具輸出 `esp32cam/dist/splitflap-esp32s.sffw`。

舊 CAM 的 `SFCAM01\0`／`splitflap-cam-v1` 更新檔會拒絕，即使映像晶片同樣是 ESP32。設定備份的舊 device 欄位不影響此 OTA 硬體檢查。

上傳開始、資料寫入、結束與最終 HTTP handler 均需有效的 Mega 停止狀態。長度／SHA-256、完整 HTTP 請求與 SDK 映像驗證通過，才選取新分割區；多檔、截斷、失聯或 USB 重新啟用都中止更新。OTA 引起的軟體重啟後不自動轉動；下次正常上電才依復電設定恢復。

1.3.1 在 `Update.progress()` 增加後暫停下一次寫入，等待 bridge 組成新的完整快照（`snapshotSerial` 增加且年齡 ≤1500ms）。最多等待 2500ms，等待時 `delay(2)` 讓 UART 任務處理接收；過期快照不能放行寫入。連線／session／維護鎖改變、STOP 確認遺失、busy／saving／watching／任一 ARM、USB 或配置 revision 改變都立即中止，不以等待掩蓋實際狀態變更。完整快照逾時、通訊中斷與停止狀態改變分別回報原因。
