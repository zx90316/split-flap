# ESP32-CAM＋MCP23017 單主控 2.0.0

適用分支：`codex/esp32cam-single-controller`。ESP32-CAM 同時提供手機網頁與馬達控制；三片 MCP23017 輸出十組馬達相位，第四片讀取 Hall。六片現有庫存使用四片，兩片備用；沿用現有 P011 電平轉換板，不新增模組。相機、閃光燈與 SD 功能停用，SD 卡槽保持空白。

本版完成軟體實作與自動測試，**MCP 實板尚未驗收**。雙主控 1.1.0 的穩定基準、使用者實測與回退方式見 [基準紀錄](../docs/dual-controller-baseline.md)。完整結果與實機檢查順序見 [驗證文件](validation.md)。

## 從雙主控移轉

1. 在仍運作的舊網頁匯出設定 JSON，保存安裝順序、十位校準與顯示偏好。備份不含 Wi-Fi／登入密碼、ARM 或實際位置。
2. 關閉馬達及控制電源，拔 USB。依 [新逐線表](wiring.md)移除 Mega 的 UART、馬達相位與 Hall 線，改接 MCP；不要讓 Mega 與 MCP 同時驅動同一片 ULN。
3. 第一次透過 CP2102 USB 燒入本版。**舊雙主控 1.x 的網頁更新器不接受單主控 2.0 更新包**；不修改或更新 Mega。
4. 保持 NVS、不執行全片擦除，原 ESP32 的 Wi-Fi、登入資料與最後字元紀錄可沿用。進入「裝置設定」匯入舊 JSON；本版接受原 `schema:1 / mega` 備份。
5. 先設定一位 U0，Hall 保持關閉；核對實物字元，確認定位並允許轉動，再開馬達支路測試。之後逐步擴充。

單主控的馬達配置另存於 ESP32 NVS，不會自動讀取 Mega EEPROM。回退時切回 `esp32-purchased-bom`、依其接線及燒錄流程恢復；不要混用兩版更新檔。

## 接線入口

[正常運作圖](wiring-runtime.svg) · [CP2102 燒錄圖](wiring-flashing.svg) · [十組相位表](wiring-motors.svg) · [Hall 圖](wiring-hall.svg) · [完整逐線說明](wiring.md)

- ESP GPIO13＝SDA、GPIO14＝SCL、GPIO15＝低有效共用 RST，經現有 P011 的 A0/B0、A1/B1、A2/B2 分別接四片 MCP。P011 VCCA＝ESP 3V3、VCCB＝控制 5V；全部共地。
- MCP VCC 使用 **5V**。0x20／0x21／0x22 控制馬達，0x23 讀 Hall；地址各不相同，不能直接把 5V 匯流排接到 ESP。
- LM2596 先量測並調到 **5.0V**，供 ESP 板上明確標示的 **5V** 腳及 MCP／Hall。不要接 ESP 的 3V3 或用途未確認的 VCC 腳。
- 馬達只接有獨立開關的 12V 分支；關閉此分支時控制電源仍保留。RST、STOP 與軟體故障處理不能取代切斷 12V。
- CP2102 只接 TXD→U0R/GPIO3、RXD←U0T/GPIO1、GND；供電腳與 DTR／RTS 均不接。

![正常運作接線](wiring-runtime.svg)

## VS Code 建置、燒錄與偵錯

在 VS Code 開啟 repo 根目錄 `D:\Project\split-flap`。目前這台電腦已備妥工具；新環境先安裝 Python 3.9 以上、[Arduino CLI](https://docs.arduino.cc/arduino-cli/installation)（驗證版本 1.5.1），CLI 放 PATH 或 `.tools/arduino/arduino-cli.exe`，再安裝建議的 Microsoft Python、Python Debugger、C/C++ 擴充套件。

1. F5 選 **ESP32-CAM: 準備環境**，安裝固定 Arduino-ESP32 **3.3.12** 與測試用 cJSON；不需先安裝 AVR 核心。
2. **Ctrl+Shift+B** 建置 ESP32-CAM。建置會產生離線網頁與 `.sffw` 更新包，不開啟 COM 埠、不燒錄。
3. F5 選 **ESP32-CAM: 查詢連接埠**，確認 CP2102 的 COM，不要照抄預設 COM4。COM 來自 CP2102 USB；ESP 只接外部電源不會出現 COM。
4. 關馬達 12V 分支，保留控制 5V。**GPIO0 接 GND，按 RESET**，選 **ESP32-CAM: 建置＋燒錄＋串列監看**，輸入正確 COM。
5. 上傳成功後 **移除 GPIO0→GND 跳線，再按 RESET**。115200 串列輸入 `STATUS` 查看 MCP；`ACCESS` 查看設備 Wi-Fi 及登入資料。勿公開貼出 ACCESS 密碼。

每次燒錄先建置，建置失敗不會上傳舊檔；上傳與讀回驗證成功才進入監看。CP2102 沒有自動重設接線，GPIO0／RESET 的手動步驟不可省略。`Unknown` 板名不表示 COM 故障；完全沒 COM 時，分離板子後核對 [CP210x 官方驅動](https://www.silabs.com/software-and-tools/usb-to-uart-bridge-vcp-drivers)。

```powershell
python esp32cam/tools/dev.py setup
python esp32cam/tools/dev.py build
python esp32cam/tools/dev.py ports
python esp32cam/tools/dev.py upload-monitor --port COM4
```

| VS Code 選項 | 用途 |
| --- | --- |
| ESP32-CAM: 重新建置韌體 | 清理後重編譯；輸出本版更新包 |
| ESP32-CAM: 串列監看 | 115200，STATUS／試轉命令；Ctrl+C 結束 |
| ESP32-CAM: 全部模擬測試 | Python 工作流程及原生控制核心測試，不連硬體 |
| ESP32-CAM: 手機網頁流程測試 | Chrome／Playwright 配模擬控制器，需 Node.js 和 Playwright |
| ESP32-CAM: 開啟網頁模擬器 | 本機測試用登入；不是連到實板 |
| ESP32-CAM: 準備主機偵錯工具 | 安裝固定 Zig 0.16.0 至 `.tools`，僅偵錯／測試需要 |
| 主機中斷點: MCP 單主控與運動 | 原生 C++ 假硬體；可在 local_engine.h、mcp_bank.h、共用 bench.cpp 設中斷點 |
| ESP32-CAM: 產生接線圖 | 重建四張 SVG 並嵌入離線網頁 |

CP2102 提供燒錄及串列紀錄；主機中斷點在電腦執行，不是 ESP32 的硬體中斷點。停止監看不會停止馬達，試轉停止要用 `STOP` 或實體馬達分支開關。舊 Mega 工具保留在「歷史雙主控工具」群組。

## 手機連線與第一位測試

1. 加入 `ACCESS` 顯示的 `SplitFlap-xxxxxx`，輸入該設備的直連密碼；提示無網際網路時保持連線。
2. 開啟 `http://192.168.4.1/`，帳號 **admin**，使用 ACCESS 的 Admin password 登入。所有資源內建，離線可用。
3. 「裝置設定 → 網路與登入」填家用 Wi-Fi，保存後可用 `http://splitflap.local/` 或 STATUS 顯示的 STA IP。未接齊 MCP 也能設定網路；執行工作／保存／更新時需先等待或停止。直連備援網路會保留。
4. 「安裝配置」先選 **1** 位 U0；「MCP 硬體」保持 Hall 關閉。只接 0x20 馬達板也能測 U0，其餘尚未用到的 MCP 可以缺席。
5. 看到「MCP 已就緒」後，在「手動定位」選出**實際可見**字元，記錄並允許轉動。開馬達支路，再送出目標文字。記錄與 ARM 本身不會轉動。

找不到 MCP 時先關馬達支路，核對 5V、共地、地址與三條匯流排線，再按「初始化／重試 MCP」。初始化不會轉動，但會解除定位及 ARM。設定頁列出掃描到與需要的地址；不會自動判斷哪組馬達已接上。

### 1～10 位、字元與並行翻頁

位數可選任意 1～10 整數，自訂位置例如 `7,0,2` 表示由左至右 U7、U0、U2。每個實體位置保留自己的校準。只有安裝清單中的位置能輸出；新增／移除／重排清單解除 ARM，保留仍使用位置的基準，新位置需要確認字元。

需要改變的位同時起步、各自到位停止，相同字元略過；TEXT、HOME ALL、CYCLE ALL 均並行。45 片順序為：空白、A–Z、Ä、Ö、Ü、0–9、`:`、`.`、`-`、`?`、`!`。輸入小寫轉大寫，`_` 代表空白，`$ & #` 分別可代替 Ä Ö Ü。目標可左右／置中補空白，超長或不支援的字元拒絕；手動目前文字必須剛好符合位數。

### 字元保存與 ARM

最後確認或成功完成的字元按 U0～U9 自動保存到 ESP NVS；改設定、關手機頁面與正常重啟不清空。虛線框表示歷史紀錄，不能直接當作實際位置。重啟、STOP、故障或初始化後，核對機構，按「確認字元並允許轉動」重新建立手動基準。

ARM 只允許清單內的位置接受後續動作，不會立刻轉，也不會一直通電。調速／加速度保留位置與 ARM；圈步數、偏移、方向、相序、極性改變時，受影響位置需要重新確認。匯入相同設定不會無故清空位置。

安裝與校準採 NVS 雙槽、版本及 CRC，寫後讀回驗證；斷電保留上一份有效設定。位置與 ARM 不作為開機有效狀態保存。網頁「套用並保存」直接持久化；單次串列或快捷調速後用 SAVE 保存。Wi-Fi／登入與顯示偏好沿用 `flap-cam`，馬達配置另存 `flap-motion`。設定備份不含密碼或字元歷史。

### 速度、Hall 與調機

預設 **SPEED=800µs、ACCEL=800 半步/秒²、REVSTEPS=4096、OFFSET=0、DIR=1、ORDER=1234、POLARITY=LOW**。SPEED 可調 800～30000，越小越快；定距動作慢速起步、加速巡航、減速停止。

800 是要求的間隔下限，並非已量測的實際週期。I²C 寫入與排程會延長間隔；程式依真正寫入時刻限制下一相，避免延誤後快速補步。單顆、十顆、Wi-Fi 負載及機構的實際速度／是否失步仍須實測；設定頁可看最大寫入與排程延遲。

沒有磁鐵可一直使用手動定位。接妥 **0x23 與感測器**後，再啟用「已接 0x23 與霍爾」；啟用後該板是必要硬體。WATCH／HOME／MEASURE 在 Hall 關閉時拒絕；啟用後先監看原始值及磁鐵通過時的變化，再歸零與量測。LED 測試前拔除所選馬達。

串列也可操作，命令逐行送出；STATUS 可在轉動時查詢，只有明確 STOP 才請求停止：

```text
STATUS
INIT
UNIT 0
SET Z
ARM
SHOW A
SPEED 800
ACCEL 800
SAVE
```

每行回 `ACCEPTED job=...` 表示排入工作，STATUS 檢查結果與定位；忙碌時其他動作拒絕。多位使用 `DIGITS 3` 或 `UNITS 7,0,2`，接著 `SETTEXT ABC`、`ARM ALL`、`TEXT ZYX`。`SET Z`／`SHOW 0` 是字元，`GOTO 0` 是空白的索引。完整命令由 HELP 列出。串列手動命令會暫停時鐘，自動顯示需重新啟動。

### 時鐘與日期

預設台北 UTC+8，支援 NTP 及手機校時，未校時拒絕啟動。4 位可用 HHMM，5 位以上可用 HH:MM；日期依位數提供 MMDD、MM-DD、YYYYMMDD、YYYY-MM-DD。1～3 位只使用文字。只更新改變的字元，忙碌時完成目前動作後採最新時間；關閉手機頁面不停止已接受工作或時鐘。設備重啟後需重新定位、ARM 並啟動。

## 一條 USB、一包網頁更新

只需燒錄 ESP32-CAM，MCP23017 無韌體。USB 使用上述 CP2102；網頁使用建置產出的 **`esp32cam/dist/splitflap-esp32cam.sffw`**。`.bin` 是原始映像，不能拿來代替網頁更新包。

1. 先關閉實體馬達 12V 分支，在「韌體更新」按「停止並準備更新」。
2. 等待本機控制核心確認停止、解除 ARM 且線圈輸出已關閉。
3. 選本分支建置的 `.sffw`，上傳。寫入前拉低四片 MCP 的 RST 並鎖定動作；長時間上傳不影響已關閉的輸出。
4. 更新重啟後保留設定與字元紀錄；核對實物後確認並允許轉動。

錯檔、長度、板型、SHA-256 不符會拒絕；中斷上傳可重新準備。缺少 MCP 時也能停止並更新，便於修復。採 OTA 雙應用分割；完成前不切換開機分割，不提供上電後自動健康回退保證。首次燒入 2.0.0 必須 USB；其後才可用單主控網頁更新。

建置固定 `esp32:esp32:esp32cam:PartitionScheme=min_spiffs`、[Arduino-ESP32 3.3.12](https://github.com/espressif/arduino-esp32/releases/tag/3.3.12)。更新檔與測試產物在被忽略的 `.build/`、`dist/`，不提交到 Git。API、儲存與封裝細節見 [介面文件](protocol.md)。
