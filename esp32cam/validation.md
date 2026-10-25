# 單主控 2.0.0 驗證紀錄

日期：2026-10-26（Asia/Taipei）。分支：`codex/esp32cam-single-controller`。本次只建置與測試軟體，**沒有開啟 COM 埠、燒錄、發送實體馬達命令或驗收 MCP 接線**。雙主控 1.1.0 的使用者實機回報保留於 [穩定基準](../docs/dual-controller-baseline.md)，不視為單主控實測。

## 正式建置

| 目標 | 固定工具／設定 | 結果 |
| --- | --- | --- |
| AI-Thinker ESP32-CAM | Arduino-ESP32 3.3.12；`PartitionScheme=min_spiffs` | 通過；Flash 1,119,417 / 1,966,080 bytes，靜態 RAM 55,112 / 327,680 bytes |
| 保留的 Mega 2560 | Arduino AVR 1.8.8；ATmega2560 | 通過；Flash 32,138 / 253,952 bytes，RAM 2,360 / 8,192 bytes |
| 單主控原生偵錯程式 | Zig 0.16.0，C++11、warnings as errors、CodeView | 通過；產生 test_local_debug.exe 及 PDB |

ESP app 映像 **1,119,568 bytes**；網頁包 **1,119,632 bytes**，含 64-byte header。app SHA-256：

```text
45de296a0e0e07795e30d660aa13cb548dccbebec2aba62b640ef26a271c0876
```

產物：`esp32cam/dist/splitflap-esp32cam.bin`、`splitflap-esp32cam.sffw`、`firmware.json`，均不提交 Git。上述大小與摘要對應本次建置；修改原始碼後以重新建置輸出為準。新版 `.sffw` 使用 SFCAM02 與單主控識別，首次移轉需 CP2102 USB。

## 自動測試結果

執行命令：

```powershell
python esp32cam/tools/dev.py test
python esp32cam/tools/dev.py test-web
python esp32cam/tools/dev.py native-debug
python mega2560/tools/dev.py test
python esp32cam/tools/dev.py build
python mega2560/tools/dev.py build
```

- **14 項 Python 測試通過**：建置失敗不燒錄、上傳失敗不監看、COM 驗證、OTA 封裝／長度／摘要／錯專案、嵌入資源與來源一致、四張 SVG、VS Code 預設任務及相依任務、HTTP 登入／CSRF、離線網路設定、設定整批驗證、舊雙主控備份移轉及 Hall 型別檢查。
- **單主控原生控制測試通過**：直接使用正式 LocalEngine、McpBank、MotionStore 與共用運動核心，配假 I²C／NVS／理想轉子。覆蓋 1～10 各位數、十位並行、正確相位次數、未啟用低輸出、相同字元略過、稀疏映射、真正寫入時刻的相位間隔不低於 800µs、保留校準與共同位置基準。
- **故障／保存測試通過**：STOP 優先取消、NACK 拉低 RST、必要／選用 MCP 缺席、Hall 板需求、暫存器異常、初始化重試、離線 OTA 維護鎖、flash 保存鎖、重複命令、重啟不 ARM、微秒計時溢位、100ms 控制停滯；132-byte 紀錄的 **133 個截斷位置**全部驗證舊／新有效設定回復。
- **運動功能測試通過**：45 字元整圈、CYCLE／FLAP／GOTO／STEP／REV、SPEED／ACCEL 差異套用、NVS 保存失敗保留 RAM 設定、SAVE／LOAD、模擬 Hall HOME 及三圈 MEASURE（各 4096 半步）。
- **既有顯示與雙板回歸通過**：45 字元／UTF-8／別名、對齊、日期／閏日／時區與全位數、正式 JSON 命令剖析、串列剖析、字元歷史及舊 UART 狀態／重送／失聯測試。這些 UART 測試只保護舊程式，不代表新韌體有雙板通訊。
- **Mega 回歸通過**：9 項 Python、15 組 Bench、7 組 Panel、實際 .ino 的模擬 GPIO／Serial1／EEPROM、4000 包 COBS／CRC、每個 EEPROM 寫入截斷、舊預設速度遷移與遠端控制。證明共用運動核心抽取後舊硬體程式仍可建置及模擬執行。

### 手機網頁：8 組通過

隔離的 Chrome／Playwright，390×844 手機尺寸及 1100×900 桌面尺寸，連接 Python 模擬控制器，不連實板。

1. 首次未接 MCP 仍能保存 Wi-Fi／安裝配置；維護期間禁止修改網路。
2. MCP 缺席、重試、初始化不 ARM、Hall 開關保存及關閉時禁止 Hall 命令。
3. 登入、手動目前字元、ARM、文字與寬度錯誤、未校時拒絕。
4. 調速／加速度保留字元與 ARM、重新載入頁面保留歷史、四張離線接線圖載入。
5. 非連續 U7/U0/U2、UTF-8、補空白、字元拒絕、1～3 位不開時鐘。
6. 五位時鐘、手機校時、關閉／重開頁面繼續、暫停與日期格式限制。
7. 網路設定、匯出／匯入、無效配置整批拒絕、離線恢復、重啟後未 ARM。
8. OTA 停止確認、截斷／錯摘要拒絕、成功重啟、頁面無溢出與無 JavaScript 錯誤。

四張 SVG 另以目前原始內容渲染，檢查標示、地址、電源、共地、RST、相位及 Hall 表；手機截圖更新於 [ui-preview.jpg](ui-preview.jpg)。瀏覽器結果在 `test/.build/browser-results.json`，截圖在同一個被忽略目錄。

## 尚未完成的實板驗收

以下全部仍待使用者實測，不以理想轉子／假匯流排結果代替：

1. **先備份舊設定並關電改線**：完全移除 Mega 的 UART／相位／Hall 線，核對 P011 三路、MCP 地址與 RST；SD 插槽無卡。先只接 0x20 和 U0。
2. **馬達分支關閉**：量 LM2596 輸出與 Wi-Fi 負載電壓，確認 ESP 接印字 5V、MCP VCC5V、全部共地。CP2102 供電腳不接。USB 燒入 2.0.0 後移除 GPIO0 跳線並重設。
3. **控制與初始化**：STATUS／設定頁應顯示需要 0x20 且已就緒，Hall 關閉；初始化、重啟及 ARM 都不應自動轉動，未啟用位置無相位輸出。必要板未接時拒絕運動；恢復接線並初始化後才能操作。
4. **U0 手動定位**：核對實物並 SET、ARM，再開馬達支路測 SHOW／FLAP／CYCLE，反覆跨越 44→0，檢查多輪無偏移、到位後線圈關閉、STOP 取消整批。800／800 是預設，是否穩定需本接線再驗證。
5. **逐步擴充**：加入 0x21／0x22，依實際接上組別調位數與稀疏順序，測四位、跨板、十位同時運動。量控制電壓與馬達支路壓降，觀察 maxTickGap／maxFlush／lateTicks；確認沒有反覆故障或失步。
6. **Hall**：接 0x23，避開 GPA7／GPB7；WATCH 檢查觸發及釋放，再試 HOME、偏移、三圈 MEASURE 與 CYCLE。不得把舊 U0 手動定位通過紀錄當作 Hall 驗收。
7. **保存與重啟**：校準、稀疏順序、Hall 開關、Wi-Fi、字元紀錄正常保留；重開不恢復有效位置／ARM／自動時鐘。受控斷電的 NVS 實測尚未執行。
8. **手機離線及自動顯示**：關閉頁面時工作／時鐘繼續；再連線狀態正確，忙碌期間採最新時間目標，1～3 位不提供時鐘。
9. **實板 OTA**：先關馬達支路，再準備停止、驗證 RST／輸出關閉，測錯檔、取消、重試與正常更新；核對設定保留及重啟不 ARM。真實 flash 寫入中斷、供電掉落與開機分割恢復仍待驗收。

現有轉換板的 400kHz 上升時間、四板負載、GPIO15 啟動與 RST 實際行為、ULN 關閉狀態、十顆電源裕度及長時間機構誤差均未量測。軟體檢查能在程式運行時故障停止；實體馬達電源開關仍是必要操作手段。
