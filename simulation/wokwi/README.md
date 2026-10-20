# ESP32 正式韌體閉迴路模擬

此專案執行 `firmware/esp32` 的正式韌體，不再使用獨立相序診斷程式。實際路徑是 **ESP32 setup/loop → I²C → MCP23017 OLAT → 40 路馬達相位 → 十個理想轉子 → 自動 Hall → MCP23017 GPIO → 韌體歸零／定位狀態機**。

[開啟完整模擬專案](https://wokwi.com/projects/478374804791943169)。舊專案 `478367216064916481` 保留作先前診斷版本，不代表本版。

## 啟動與驗證

按 ▶ 後馬達不會自動轉動。序列監控器使用 115200 baud／換行，每次輸入一行並送出：

```text
INIT POWER_OFF
ENABLE 10
LIMIT 10
ARM
HOME ALL
```

- `INIT POWER_OFF` 仍是正式韌體的操作者確認。模擬器不能量測 12V，實機必須先關閉馬達電源；不要把模擬中的操作視為實機安全驗收。
- 十個 Hall 開關預設左側 AUTO：感測輸出由對應馬達的實際模型位置決定，不需手動配合歸零。
- 預設 4096 半步／圈、8000µs 步距。全通道同時歸零約需 34 個模擬秒，瀏覽器負載可能使實際等待時間較長。
- 歸零期間 `STATUS` 顯示 BUSY。完成後再次輸入 `STATUS`，應見十路 `READY`、`homed=1`、`slot=0`，而非只看模型指針猜測成功。

接著：

```text
TEXT HELLO
STATUS
```

等待動作完成後，`STATUS` 的前五路應為 8、5、12、12、15，其餘為 0。模型圖是位置指針及四相指示燈，不是翻頁字型顯示器；字元結果以韌體回報為準。

更多驗證：

```text
MOVE 0 44
MOVE 0 1
CAL 0 4096 45 -1 8000
SAVE
ARM
HOME 0
STATUS
STOP
```

每個 MOVE／HOME 都應等上一動作完成再送下一行。由較大字元移到較小字元會先重新歸零。CAL 會取消 ARM 並使該單元位置失效；SAVE 儲存校準但不儲存啟用／ARM 狀態。NVS 在模擬器中的跨執行保存依平台行為，不等於真實 ESP32 斷電持久性測試。

## 故障注入

1. 停止動作後，將任一 H_U 開關移到右側：該路 Hall 斷開，MCP 上拉會讀成持續未觸發。
2. 對該路執行 HOME；等候步數／時間門檻。預期出現 `FAULT MOTION_HOME`、全部停止、RESET 拉低且解除 ARM。
3. 把開關切回 AUTO，重新 `INIT POWER_OFF`、`ARM`、`HOME` 才能恢復。
4. `STOP` 可在移動中使用：全部輸出歸零、解除 ARM、位置失效，須再 HOME。

主機測試另覆蓋 Hall 持續觸發、I²C 失敗與完整指令解析；網站沒有以手動切換 Hall 充當成功歸零。

## 快速複現與來源一致性

```sh
python3 simulation/wokwi/export_firmware.py
python3 simulation/wokwi/generate_diagram.py
python3 simulation/wokwi/export_firmware.py --check
python3 simulation/wokwi/verify_diagram.py
bash firmware/esp32/test/native/run.sh
bash simulation/wokwi/test/run.sh
```

`export_firmware.py` 將正式來源的四個 header、motion.cpp 與 main.cpp 合成 sketch.ino，只展開本地 include、移除 pragma once，不注入自動啟用或替代控制程式。每份來源附 SHA256；`--check` 在韌體或輸出漂移時失敗。

新建 Wokwi ESP32 專案時，上傳三組 `*.chip.c`／`*.chip.json`（共六檔），把 sketch.ino 與 diagram.json 同名內容貼入編輯器即可。若以既有專案另存副本，Wokwi 不允許上傳同名檔案，請直接在編輯器全選並貼上新版內容，不要重複上傳。圖中全部四十相線、十路 Hall、四個 MCP 位址及 RESET 都已確定義，不必逐線拖拉。`generate_diagram.py` 可重建同樣的圖。

模型屬性 `revolution`、`position`、`hallWidth` 分別為理想半步／圈、起始位置、磁區寬度。預設 4096／各單元不同位置／32。若改模型圈數，必須相應 CAL 韌體；CAL 不會偷改物理模型。

## 模型邊界

- MCP23017 僅實作正式韌體使用的 BANK=0、方向、上拉、GPIO、OLAT、IPOL、IOCON SEQOP 與 RESET，不是完整晶片模型。
- ULN2003／28BYJ-48 合為理想四線圈單極馬達模型。合法相序推進位置，位置進入磁區時輸出低有效開漏 Hall。第一個相位只建立參考，掉電後轉子保持位置；不模擬齒隙、慣性、負載失步、真實磁場、KY-003 閾值或齒輪比偏差。
- 4096 是未量測的示範值，不能拿來取代實機校準。紅色模型狀態條表示看到跳相／不合法相位；重新歸零時相位重新鎖定也可能產生此診斷，不等於真實機械故障。
- 5V／12V 標記僅布林訊號；電位轉換電氣效果省略。**實機不可照此數位圖直連不同電源域**，必須遵守 `docs/esp32/wiring.md`。
- 不驗證電源電流、壓降、溫升、扭力、保險絲、USB 回灌、電力時序或實際十字元機構。不能宣稱硬體完整驗收。
- 本版涵蓋 repo 目前的 USB 串列控制韌體；repo 尚未實作的 Wi-Fi／Web／NTP／OTA 不會因本模擬而新增。

## 驗證紀錄

2026-10-20：主機端五組測試通過，包括原有 20 項 motion 測試、MCP／字元／adapter 測試，以及新增的 production setup/loop 閉迴路測試。新增測試由實際輸出暫存器驅動理想轉子，再產生 Hall，涵蓋十路 HOME、TEXT、45 個位置、跨圈重歸零、反向／offset 校準、NVS 載入、STOP、Hall 斷開／固定低與 I²C 故障。拓撲與生成程式來源一致性檢查通過。

主機測試不等於 Wokwi 瀏覽器執行，也不等於真實硬體。網站編譯及互動驗證另以實際結果記錄，不以舊診斷版的通過紀錄替代。

## Wokwi MCP

官方 `wokwi-cli mcp` 為實驗性 stdio 模式，需要 Wokwi CI token。此次網站操作不需要 token，未宣稱 MCP 已連線。不要把 token 放入 repo 或聊天。

參考：[官方 CLI](https://github.com/wokwi/wokwi-cli/blob/main/README.md)、[Custom Chips API](https://docs.wokwi.com/chips-api/getting-started)。

### 網站實際驗證（2026-10-20）

在新分享專案完成編譯與重新開啟檢查；四份變更檔案逐檔與 repo 內容比較完全一致。實際執行 INIT、ENABLE 10、LIMIT 10、ARM、HOME ALL，十路均回報 ready、homed=1、slot=0。TEXT HELLO 完成後前五路為 8/5/12/12/15，其餘為 0。再執行 MOVE、STOP、CAL、SAVE，確認停止解除 ARM、位置失效，校準更新與儲存回報成功。[原始序列輸出](evidence/browser-runtime.txt)。

Hall 斷線／固定低及 I²C 故障的自動化驗證在主機測試完成，未宣稱已在瀏覽器逐項執行。主機五組測試亦通過 ASan/UBSan（此執行環境不支援 LeakSanitizer，因此 detect_leaks=0；不宣稱洩漏檢查通過）。另已測試實際 Wokwi C 轉子回呼。瀏覽器觀察到模擬速度約 16–24%，因此 34 個模擬秒可能需要數分鐘實際時間。
