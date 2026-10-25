# 驗證紀錄與驗收門檻

> 此文件為保留的 ESP32 方案；目前進度是 [Mega 2560 單顆調機](../../mega2560/README.md)。請勿混用接線與命令。

日期：2026-10-20。以下區分軟體測試與硬體驗證。尚未完成實板驗證；自動化測試不代表實板已運作。

## 可重複的本地測試

在 repo 根目錄執行：

```sh
firmware/esp32/test/native/run.sh
# 支援 ASan/UBSan 的 Linux 主機：
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 firmware/esp32/test/native/run.sh
cd firmware/esp32
pio run
```

Native 測試以 C++11、`-Wall -Wextra -Werror -pedantic` 建置，不需要 Arduino、網路或硬體。測試不依賴 GitHub Actions；沒有新增或觸發付費 CI 工作流程。

## 軟體驗證範圍

| 範圍 | 結果／限制 |
|---|---|
| Motion 核心 | 20 項原生測試通過：初始停用、配置合法性、歸零原子性、起始 Hall active/inactive、bounce、stuck-high/low、步數／時間上限、偏移預算、8相序與反向、禁止追趕暴衝、45槽絕對定位、跨圈再歸零、1/2/10併發、保持期、stop/fault/config失效、uint32微秒 rollover |
| MCP 傳輸抽象 | 通過：十組 nibble 映射、每片批次輸出、未變更不重寫、OLAT先於IODIR、Hall避開A7/B7、active-high/low、讀回稽核、讀寫失敗鎖定 |
| ESP32 adapter 模擬整合 | 通過：啟動／殘留輸出reset、INIT/ARM門檻、掃描、稽核耗時後新Hall、部分寫入／短讀／MCP重置故障、迴圈停滯、STOP、busy命令拒絕、TEXT原子性、32-byte接收預算、過長行丟棄、NVS整批校驗及不保存enable/arm |
| 字元解析 | 通過：原版順序、ÄÖÜ UTF-8、替代$&#、補空白、過長及不支援字元拒絕 |
| Sanitizers | ASan＋UBSan 已執行；執行環境的 ptrace 限制使 LeakSanitizer 不適用，測試時關閉 leak detection。核心無動態配置 |
| ESP32 交叉編譯 | 通過；本專案來源碼啟用 `-Werror`，最終建置無警告；編譯不代表 Flash/板型/接線已驗證 |
| BOM／文件 | 標準 BOM 與選配零件分開；ID 唯一、必要元件數量與設計一致、未知用量留白並標示依據、CSV UTF-8 BOM、相對連結及空白字元檢查 |

### 建置紀錄

建置目標：`esp32dev`，PlatformIO Core 6.1.19，Espressif32 platform 6.12.0，Arduino-ESP32 2.0.17。最終建置成功，RAM 22,776 / 327,680 bytes（7.0%），Flash 304,873 / 1,310,720 bytes（23.3%）。已產生 firmware.bin，但未進行 USB 燒錄。MCP/字元解析/adapter 與20項核心測試均通過一般與ASan＋UBSan兩種建置。

### 獨立程式檢查

針對初始有效 Hall、卡住感測器、微秒 rollover、部分 I²C 寫入失敗、NVS 載入與 reset 順序做過第二輪審查。已修正：

- 配置稽核耗時後重新取得時間，再讀新 Hall；motion tick 只消耗成功的新感測樣本
- 歸零故障先拉低硬體 /RESET，再輸出診斷，避免串列背壓延後釋放
- 啟動先預載 GPIO23 LOW 再設 open-drain
- 單片掃描用 `SCAN POWER_OFF` 暫時釋放 reset，結束重新保持 reset；不能在 reset 持續低時誤判沒有裝置

這些檢查沒有模擬實際馬達電流、電源瞬變、機械慣性、Hall 雜訊波形或電平轉換電氣特性。

## 尚未執行，必須實物驗證

詳見 [assembly.md](assembly.md) 的分階段門檻及紀錄表。

- [ ] 元件型號、數量與實際模組版本
- [ ] 12V電源輸出（選配PD/QC另驗協商）、極性、斷電記憶、連續及峰值電流
- [ ] 降壓5V、ESP32 3.3V、USB／外接5V隔離、回灌與brownout
- [ ] ULN IN1–IN7到馬達插座映射、12V適用性、COM、輸入在reset時的關閉狀態
- [ ] Hall料號、腳位、磁極、非鎖存行為、有效位準、上拉及雜訊
- [ ] MCP位址、GPIO輸出、/RESET到達四片、I²C上升時間與低電位
- [ ] 單馬達相序、正確前翻方向、各字元實際半步／圈和零點offset
- [ ] 45槽、跨圈、反覆歸零、熱穩定、機械卡滯、負載下失步
- [ ] 1→2→5→10字元逐級運作的供電、線材／接點溫度及I²C錯誤率
- [ ] 安全地模擬Hall缺失、停用、重啟及故障；先確定實體切斷可用，不帶電拔感性負載
- [ ] 無人值守所需的獨立預設關閉互鎖／硬體看門狗（本版未設計完成）

未勾選項目均未完成。本版只適合有人看守、逐關檢查的原型調機，不宣稱適合無人值守成品。
