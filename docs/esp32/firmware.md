# ESP32 韌體、命令與校準

> 此文件為保留的 ESP32 方案；目前進度是 [Mega 2560 單顆調機](../../mega2560/README.md)。請勿混用接線與命令。

狀態：可建置／可執行主機端測試的工程原型。未燒錄實板、未做物理運動驗證。這裡的指令會造成實際馬達運動；完成 [wiring.md](wiring.md) 與 [assembly.md](assembly.md) 門檻後才使用。

## 1. 工具與入口

- `firmware/esp32/platformio.ini`：固定 `platformio/espressif32@6.12.0`、Arduino、`esp32dev`、115200 baud
- `include/board_config.h`：GPIO21 SDA、22 SCL、23 /RESET，100kHz I²C
- `include/mcp_bus.h`：硬體傳輸抽象，OLAT 快取／每片兩埠批次交易／配置讀回
- `include/motion.h`、`src/motion.cpp`：無 Arduino 依賴的排程與歸零狀態機
- `src/main.cpp`：Wire、Preferences/NVS、有限長度串列解析與故障鎖定
- `include/characters.h`：原版 45 字元的索引映射

安裝官方 [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html)，然後：

```sh
cd firmware/esp32
pio run
```

`esp32dev` 是通用 WROOM-32 起始設定，不能由「30pin／CH340／Type-C」推定 Flash 容量或供電原理。組裝前核對模組型號、Flash、板子 GPIO 標示、USB 驅動與資料線。若不同，調整 board/flash 設定再燒錄。沒有 GitHub Actions 工作流程，驗證走本地，不額外使用付費 CI。

```sh
# 遵守外接 5V 與 USB 的供電隔離要求，馬達12V先關閉
pio run --target upload
pio device monitor --baud 115200
```

UART0 經 CH340 使用 USB；不把 GPIO1/3 分配給馬達。開機只保持 /RESET 為低，沒有自動控制或歸零。

## 2. 最小調機流程

所有 unit 索引 **0–9**；第一個字元是 0。MCP 四片都須接妥才可初始化；只接單片時可先 `SCAN POWER_OFF` 驗證位址，不能假裝剩下三片存在。

```text
HELP
SCAN POWER_OFF
INIT POWER_OFF
STATUS
ENABLE 1
LIMIT 1
```

`INIT POWER_OFF` 的前提是真的已切斷馬達 12V，控制 5V 正常。命令不會量測電壓；單靠文字確認並非電氣保護。初始化經 GPIO23 脈衝 /RESET、清零 OLAT、設定方向與上拉、讀回配置。收到成功後，依實測安全條件開啟馬達 12V，手邊保留實體切斷方式：

```text
ARM
HOME 0
# 等動作停止後
STATUS
MOVE 0 1
```

出現卡住、異音、方向不對、溫升過高或電源掉壓，立即切斷實體 12V。`STOP` 只在軟體及連線正常時有效。

## 3. 命令表

| 命令 | 意義／限制 |
|---|---|
| `HELP` | 簡短命令列表；忙碌時僅 STOP/STATUS 接受 |
| `SCAN POWER_OFF` | 馬達12V關閉時重置MCP後掃描 0x20–0x27，結束保持RESET低、使位置失效；ACK 只能證明有裝置，不能證明料號／接線正確 |
| `INIT POWER_OFF` | 控制電路初始化／故障恢復；先切斷實體馬達 12V；清除位置與解除 ARM |
| `ENABLE n` | 啟用前 n 個 unit，0–10；不運動，解除 ARM 並使全部位置失效；開機預設 0 |
| `LIMIT n` | 最多同時運轉 n 顆，1–10；開機預設 1，包含100ms停止保持期；不能當作電氣限流 |
| `ARM` | 完成配置稽核後允許命令；仍不自動動作；不代表電路安全認證 |
| `HOME ALL` / `HOME u` | 對已啟用 unit 歸零，按併發上限排隊 |
| `MOVE u index` | 前往 0–44；必須已歸零；索引較小會先重新歸零，再前往目標 |
| `TEXT HELLO` | 字串依目前字元表解析，最多 enabled 數，尾部補空白；全部先驗證才排程；大寫，不暗自替換不支援字元 |
| `CAL u rev offset dir us` | 每圈半步數、有效 Hall 邊緣之後偏移半步、相序方向 ±1、半步間隔 µs；使該 unit 位置失效並解除 ARM |
| `POLARITY u LOW` / `HIGH` | 依量測選 Hall 有效位準，預設 LOW；停止且解除 ARM，需重新歸零 |
| `CHARSET UMLAUT` / `NOUM` | 原 ÄÖÜ 或替代 `$&#` 翻頁；不改物理槽位 |
| `SAVE` | 空閒時把十組校準、Hall 極性及字元表儲存 NVS；不儲存 ARM／enabled 數／併發上限 |
| `STATUS` | 空閒時列出配置、有效 Hall mask 與位置；忙碌只回短訊，避免大量串列輸出拖慢步進 |
| `STOP` | 停止全部、解除 ARM、清除位置；再運轉需 ARM/HOME；I²C 失效時改為鎖定與 /RESET 低 |

每行最多 159 bytes，LF 結尾；CR 忽略。過長行整行丟棄，最多每輪處理32 bytes，不阻塞等輸入。空行忽略。忙碌期間拒絕變更配置或新移動目標，避免半途改校準。`TEXT` 保留命令後空格，所以可顯示前導空白。`TEXT ` 可清成空白。

`STATUS` 的 `hall` 位元 0–9 對應「正在有效感測」的 unit，不是原始 5V 電壓；`slot` 只有 `homed=1` 才有意義。

## 4. 預設值與測量

- 45 flaps；每圈 **4096 半步只是起點**，不是實際馬達的實測值
- `offset=0` 只是先找 Hall 邊緣，不保證空白正確朝前
- 預設每半步 8000µs、相序方向 +1、Hall active-low
- 28BYJ-48 實際齒輪比可能不是整數64；上游 `STEPS=2038` 是全步，**不能直接當成本版半步數**
- `CAL` 範圍：rev 1000–20000、offset 0..rev-1、dir ±1、us 3000–30000

先讓單顆無負載／低風險機構驗證相位，若只震動，檢查 ULN 通道及線圈映射，不能靠反轉方向掩蓋接線錯誤。方向 +1/-1 只改電氣相序，邏輯位置永遠依翻頁順序往前，不倒翻卡片。

1. 確認磁極、非鎖存 Hall、電壓與有效位準。手動轉動（12V關閉）並觀察 STATUS 的 Hall 位元。
2. 用零偏移 HOME 取得參考邊緣。這是機械基準，不一定是空白。
3. 小幅修改 offset、重新 ARM/HOME，直到空白穩定對齊，例如 `CAL 0 4096 100 1 8000`；100僅為語法範例，並非建議實測偏移。
4. 用 `MOVE 0 1`、`MOVE 0 22`、`MOVE 0 44` 比較早／中／晚槽位。如果誤差隨索引增加，調整 rev；所有槽位固定平移則調整 offset。每次改 CAL 後必須重新 HOME。
5. 以可見軸標記、步數記錄及多圈重複結果確認實際每圈步數；本版沒有編碼器，也沒有自動測量齒輪比指令，不得把軟體目標位置稱作量測值。
6. 逐槽檢查 0–44，再執行 44→0 跨圈歸零及斷電重啟。記錄每個 unit 的參數後 SAVE。

整數目標採 `round(index × rev / 45)` 的絕對位置，不把每格的截斷誤差累加。跨圈重新尋找 Hall 限制齒比誤差累積，但不能偵測所有打滑／卡片卡住；一圈內的失步仍可能錯字，需人工驗收。

## 5. 歸零、故障與時間

狀態：Unhomed → QueuedHome → ClearMarker → SeekMarker → Offset → Settling → Ready。

- 起始已在磁區：先移出，確認穩定離開，才接受下一次進入邊緣
- 起始不在磁區：先確認穩定非有效，再開始尋找
- 邊緣候選暫停步進以做5ms debounce，不在抖動時持續跨過參考點
- 歸零最多2×rev半步（含偏移），同時有 `clamp(2×rev×us+5秒,60秒,120秒)` 的時間限制；慢速／大 rev 設定可能先碰到120秒上限
- 正常定位為有限步數，停止保持100ms後線圈全關；釋放後若外力移動機構，必須重新 HOME
- 每個 tick 最多一個相位變化；排程延遲會變慢，不會一次補多步
- Hall每2ms目標輪詢；I²C交易、100ms迴圈停滯偵測、每250ms配置稽核都不是硬即時／功能安全裝置
- ACK 失敗、短讀、配置或輸出暫存變動：停排程、位置失效、ARM解除、GPIO23保持RESET低；必須切斷12V並檢查後 INIT

100kHz 一筆16-bit輸出約36個SCL時脈，單次Hall讀取約45個；三片同時變更時名義資料交易約1.53ms，未含軟體、拉升及稽核。每250ms完整配置讀回會額外延遲。3000µs只是輸入合法下限，並非已驗證十顆同步速度；先用8000µs與LIMIT1，依量測增加。NVS寫入與大量STATUS僅空閒允許。

## 6. 後續通訊工作

本版僅 USB 串列。原 `ESPMaster` 的 ESP8266 Web/NTP 程式未接新核心，不能當成 ESP32 可用 UI。單顆、電源、十顆驗收完成後，再以有界輸入佇列、驗證與授權，把同一份命令語意接到 ESP32 Wi-Fi。網路處理不可阻塞步進，不開放未驗證的互聯網控制，也不把 Wi-Fi 密碼存進 repo。

## JonnyBooker 相容性

45 字元順序與 `Unit/Unit.ino` 相同，測試另涵蓋其調機序列（空白、Z、A、U、N、?、0、1、2、9）及全部 45 個索引。保留本版非阻塞控制與 NVS 校準；上游 Nano 的全步 offset、2-byte I²C 命令與 EEPROM 不能直接套用。Web／訊息切段／排程／倒數等移植界線見 [參考比對與計畫](upstream-reference.md)。
