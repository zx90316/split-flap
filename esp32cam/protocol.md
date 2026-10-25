# 單主控 2.0 介面與執行模型

本版正式入口為 `controller/controller.ino` → `App` → `LocalController`。MCP23017 由 ESP32 直接透過 I²C 控制；**正式韌體不啟用 ESP↔Mega UART、COBS 或心跳**。`bridge*.h`、`SplitFlapLink` 舊封包與舊 Mega 測試保留作回歸；`SplitFlapLink` 的設定資料型別和操作列舉仍由本版共用。

## 工作、狀態與停止

GET `/api/state` 回傳快照，POST `/api/command` 接受工作後回 202 與 `id`，由 jobs 查詢 `queued / accepted / done / rejected / fault`。202 只表示接受，不能視為馬達到位。一次只接受一項一般工作，STOP 可取代待執行與運動中的工作；中止整批、線圈關閉、解除 ARM 並使位置失效。狀態查詢不打斷運動。

修改與更新需 Digest 登入及 `X-CSRF-Token`；有 Origin 時須符合目前 Host。每次開機使用新的 boot，網頁動作攜帶快照的 boot 和 nextRequest；舊 boot 拒絕，不能重播重啟前的動作。同一開機的最近 16 個 request 以內容 CRC 去重，重送相同工作回原 id，不重複運動；內容不同或順序不符拒絕。串列與自動顯示使用同一個本機佇列。

範例：

```json
{"boot":12345,"request":1,"op":"TEXT","unit":"ALL","value":"HELLO","align":0}
```

operation 支援 STOP/OFF、INIT、UNIT、ARM、SET/SETINDEX、SETTEXT、SHOW/GOTO、TEXT、STEP、REV、FLAP、CYCLE、HOME、MEASURE、WATCH、LED、DIGITS、UNITS、SPEED、ACCEL、REVSTEPS、OFFSET、DIR、ORDER、POLARITY、SAVE、LOAD。數字 unit 為實體 U0～U9；ALL 為目前安裝清單。TEXT/SETTEXT 操作整串；SETTEXT 需恰好等於位數。SHOW 的字串是顯示字元，GOTO 的數字是 0～44 索引。

INIT 可在缺少 MCP 時重試，先停止並使全部位置失效；不自動 ARM。HOME/MEASURE/WATCH 需要明確啟用 Hall。SAVE／LOAD 與配置可在 MCP 尚未接齊時執行，但運動需要所有本次配置所需板就緒。

快照包含 firmware `2.0.0`、architecture `esp32cam-mcp`、protocol `2`、boot/nextRequest、controller 配置、十位狀態、近期 jobs、Wi-Fi、顯示模式及 hardware：

| hardware 欄位 | 意義 |
| --- | --- |
| hallEnabled | 是否要求 0x23 並啟用 Hall 命令 |
| initialized | 本機 MCP 初始化是否完成 |
| presentMask | 0x20／21／22／23 分別是 bit 0／1／2／3 |
| requiredMask | 依目前安裝清單與 Hall 開關計算的必要板 |
| outputsDisabled | RST 已保持低，或三片輸出快取皆為零；不是外部量測 |
| configStorageOk | 最近馬達設定保存／NVS 狀態 |
| clockHz | 400000 |
| maxTickGapUs / maxFlushUs / lateTicks | 運動時最大迴圈間隔、最大相位批次寫入時間及間隔超過 800µs 的次數 |

legacy 相容欄位 protocolMismatch、uncertain、usbLocked 保持 false；不再存在雙板接管操作。

## 排程與 I²C

FreeRTOS core 1、priority 5 的唯一控制工作讀寫 Wire，esp_timer 每 250µs 通知；中斷內不做 I²C。HTTP／上傳在另一個一般工作流程，經鎖定的快照和單項佇列互動。控制工作不依賴手機頁面、UART 心跳或網路請求持續發生。

每片馬達 MCP 以一次 16-bit OLAT 寫入合併四顆的變更，未改變的板略過。只輸出啟用位置；其他 nibble 永遠零。每位另記錄真正 MCP 寫完的時刻，至少過要求間隔後才允許下一相；不因排程落後而補發短間隔相位。此策略避免過快，代價是實際週期可能大於設定值。

Hall 每 2ms 獨立讀取；暫存器檢查每 20ms 輪流一項，工作完成前作完整檢查，不在每一相讀回所有暫存器。檢查 IOCON、IODIR、GPPU、OLAT；NACK、短讀、重設／暫存器異常會拉低共用 RST、取消整批並使位置失效，需 INIT 及重新定位。運動時控制迴圈超過 100ms 或積壓超過 16 個通知亦故障停止；這是軟體恢復處理，不是獨立硬體斷電保證。

800µs、10 顆並行與現有轉換板在實機上的裕度仍待量測。統計數值是裝置執行時觀察，原生測試的假 I²C 時序不能證明實板速度。

## 設定、匯入與 NVS

GET `/api/config` 匯出以下 schema 2；POST 同路徑額外攜帶 boot/request，先驗證整份，再保存與套用。

```json
{
  "schema":2,
  "device":"splitflap-esp32cam-mcp",
  "controller":{
    "units":[0],
    "calibration":["十個完整校準物件；此處僅示意，非可匯入值"]
  },
  "hardware":{"hallEnabled":false},
  "display":{"mode":"text","clockFormat":"HH:MM","dateFormat":"YYYY-MM-DD","timezone":480,"align":0}
}
```

calibration 固定十筆，每筆含 revolution、offset、speed、accel、direction、order、activeLow。位置 1～10 個、0～9、不重複；Hall 必須是布林。省略 hardware 時沿用目前 Hall 設定。接受舊 `schema:1`、device `splitflap-esp32cam` 與 `mega` 欄位，將校準／顯示偏好移轉到本機；舊備份不會替使用者開啟 Hall。

`flap-motion` namespace 採 cfg-a／cfg-b 兩筆 132-byte blob：SFM2 magic（4）、generation（4）、設定（121）、Hall 布林（1）、CRC16（2）。寫入非目前槽，讀回驗證成功才承認新版本；開機挑最新有效槽。無有效設定則 U0、800／800、Hall 關閉。沒有 ARM 或位置狀態。

`flap-cam` 沿用雙主控版 AppSettings、Wi-Fi／登入、顯示偏好及 DisplayMemory 格式，不作全片擦除即可保留。字元歷史僅記錄停止且定位有效的值，未知或運動中不覆寫。所有 NVS 寫入均先保留閒置控制狀態，避免 flash 暫停發生在步進中。速度／加速度保留位置及 ARM，幾何設定只讓受影響位失效；安裝清單變動解除 ARM，保留共同位置基準。

整份匯入先保存馬達 blob，再保存顯示偏好，兩個 namespace **不是同一筆交易**。若後者失敗，工作會標為 storage fault、停止自動顯示並提示重新匯入；已保存的校準不會回滾。匯出與狀態不提供密碼。

其他端點：POST `/api/network` 保存網路／登入，POST `/api/preferences` 控制時鐘與日期，POST `/api/time` 接收 Unix 秒校時。網路設定在 MCP 缺席時仍可用；忙碌、保存、WATCH、維護期間拒絕。

## 單包 OTA

POST `/api/ota/prepare` 先提交 STOP，回工作 id 與一次性 ticket；確認 STOP 完成、全組未 ARM、輸出關閉才可上傳。POST `/api/update` 需驗證登入、CSRF 及 `X-OTA-Ticket`。上傳前進入維護、RST 保持低；完成或失敗前不接受其他動作。失敗清除 ticket，保留原開機分割，可重新準備；MCP 缺席時仍可進行。

本版 `.sffw` 為 64-byte header＋原始 ESP32 app image：

| offset | 長度 | 內容 |
| --- | --- | --- |
| 0 | 8 | `SFCAM02` 後接 NUL |
| 8 | 4 | app 長度，little endian |
| 12 | 4 | 更新格式版本 2，little endian |
| 16 | 32 | app SHA-256 |
| 48 | 16 | `splitflap-mcp-v2` |
| 64 | app 長度 | ESP32 app，256～1966080 bytes |

驗證 ESP32 image magic／chip ID、長度及摘要，再由 Update.end 完成映像驗證並切換開機分割。`SFCAM01` 雙主控包與普通 `.bin` 均拒絕；舊雙主控頁也拒絕本包，所以第一次移轉用 USB。USB 燒錄同一份 app 與標準 OTA 分割，不需其他主控 Bootloader 更新。

SHA-256 是檔案完整性校驗，並非發行者簽章。更新中斷保留原應用；不宣稱新應用開機後有自動健康回退。完整硬體斷電驗收尚未執行。
