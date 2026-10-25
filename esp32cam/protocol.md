# ESP32↔Mega 協定 v1

兩端共用 `libraries/SplitFlapLink/src/SplitFlapLink.h`，115200 baud、8N1。Mega Serial1=D19 RX／D18 TX；ESP32 UART2=GPIO14 RX／GPIO13 TX。USB 人工指令仍是原本的 Serial 115200。

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

STOP 優先於其他等待工作。UART 已送出一部分的 frame 先完成，再送 STOP，以免破壞 framing。沒有一般動作排隊；網頁及自動模式同時最多一項工作。STOP 取消待執行文字各位、解除全部 ARM、使位置失效。

## 狀態、心跳與 USB

Ping／Pong 每 500ms；Query 與狀態分頁不影響運動。Mega 的收送每輪都有 byte 預算，使用 `availableForWrite`，不等整行、不 flush、不在轉動中列印長篇 USB 狀態。狀態包含 panel、十筆 unit、完整設定，ESP32 收齊相同 snapshot ID 才發布，資料年齡超過兩秒時不接受新動作。

單位回報 enabled、模式、定位來源、索引、目標、ARM、原始／去抖 Hall、步數／剩餘、三筆量測、校準及錯誤。記錄值是韌體估計；手動模式無法證實硬體存在或漏步。

只有遠端 ARM／運動／WATCH 所取得的控制，才受 Mega 的兩秒心跳租期保護。租期到期停止全組、解除 ARM 與定位；同 session 遲到的命令也會拒絕，須 STOP／Claim 或重新握手。即使只有 ESP32→Mega 這一路斷線、ESP32 仍收到狀態，也會顯示不確定並停止自動模式。ESP32 的 UART 工作獨立 FreeRTOS task，不受網頁／OTA HTTP 等待影響。手機關閉頁面不會停止心跳。

USB 轉動期間任何非換行字元仍依原行為停止並丟棄整行；USB 人工控制使網頁進入 UsbOverride，自動顯示暫停。USB STATUS／HELP 在閒置時不接管。恢復時按「接管網頁控制」（Claim），解除 ARM 與定位，再由使用者重新設定。ESP32 未連接時，USB 調機可獨立使用。

## Web API

所有控制、設定與更新使用 Digest 驗證；POST 額外要求 `X-CSRF-Token`，有 Origin 時必須符合目前 Host。憑證／CSRF 在 upload START 即檢查，不能只在上傳完成 handler 才驗證。

- `GET /api/state`：連線、軟體記錄、校準、工作、網路、顯示與 OTA 就緒狀態，含本次 ESP boot、nextRequest、csrf 與 networkEditable。不包含密碼。網路設定依 networkEditable 啟用，不要求 Mega 連線；其他馬達控制仍要求 Mega 就緒。
- `POST /api/command`：`{boot, request, op, unit, value?, align?}`。op 使用既有命令名稱；例 `TEXT`／`SETTEXT` 的 value 是 Unicode 文字，送 Mega 前轉 0～44 索引。SHOW／SET 亦可用 glyph 或 index。
- `GET /api/config`：schema1、device、mega、display 的備份。
- `POST /api/config`：加上 boot／request，整份驗證後送 Mega ApplyConfig 並持久化；若含 display，等待 Mega 完成後保存 ESP32 NVS，兩者成功才回 job=done。NVS 失敗回 fault／storage，保留已保存的 Mega 設定，須重新匯入。
- `POST /api/network`：ssid，可選 password、apPassword、adminPassword；省略密碼欄位表示保留。
- `POST /api/preferences`：mode、run、clockFormat／dateFormat、timezone（分鐘）、align。只傳 run=false 可暫停而不取消已接受工作。
- `POST /api/time`：`{unix: secondsUTC}`，支援 2024～2099 年。
- `POST /api/ota/prepare`：boot／request，取得 STOP job 與一次性 ticket；等 otaReady 後使用。
- `POST /api/update`：multipart 上傳 `.sffw`，須有 CSRF 與 `X-OTA-Ticket`。

外部 request 必須等於 nextRequest；相同 request／相同內容回既有 job，不再次動作。快取外的已退役 request 拒絕。多頁同時操作若撞號，顯示錯誤，需刷新後由使用者重新下指令；前端不自動重送動作。

## OTA 封裝

64-byte header：magic `SFCAM01\0`（8）、映像長度 uint32、協定 uint32、SHA-256（32）、專案 tag `splitflap-cam-v1`（16）。後接 app `.bin`，大小最多 1,966,080 bytes；晶片必須 ESP32。上傳開始、資料寫入、結束與最終 HTTP handler 均需有效的 Mega 停止狀態。長度／SHA-256、完整 HTTP 請求與 SDK 映像驗證通過，才選取新分割區；多檔、截斷、失聯或 USB 重新啟用都中止更新。開機後自動模式保持暫停。
