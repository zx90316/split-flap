# 在 VS Code 建置、燒錄與偵錯 Mega

適用目前的 Windows 開發環境。以「檔案 → 開啟資料夾」開啟 **repo 根目錄**，讓 VS Code 載入根目錄的 `.vscode/launch.json` 與 `tasks.json`。不要只開單一 `.ino` 或 `mega2560/` 子資料夾。

## 平常最常用的操作

1. **Ctrl+Shift+B**：建置韌體。這個操作不開 COM 埠、不燒錄。
2. **Ctrl+Shift+D**：開「執行與偵錯」，在上方下拉選單選 **MEGA: 建置＋燒錄＋串列除錯**，再按 **F5**。
3. 輸入已確認的連接埠，目前預設 **COM3**。燒錄時先關馬達12V分支，保持 Mega USB 連接、共地接好。
4. 自動完成建置、上傳、讀回驗證，成功後以 **115200** 開啟串列監控。任何一步失敗都停止，不會拿舊韌體繼續燒錄。
5. 串列終端每行按 Enter 送出。可先輸入 `STATUS`；要轉動時依[單顆指南](README.md)確認供電，再分別送 `ARM`、`STEP 500`。不會自動送 ARM 或移動命令。

若原本已開著串列監控，先 Ctrl+C 或停止原工作階段，釋放 COM3 再燒錄。其他程式也可能占用 COM 埠。F5 的 COM 提示只會選埠，不會替你確認裝置身分；可先選「查詢連接埠」。

多位版上傳後，HELP應列出 `DIGITS 1..10`、`UNITS`、`SETTEXT`／`TEXT`、`SPEED [ALL] 800..30000`。STATUS預設包含 `DIGITS=1 UNITS=0`、`reference=NONE value=UNKNOWN`、`SPEED=5000 ACCEL=400`。單顆依[手動定位](README.md#無磁鐵手動定位與指定字元)操作；多顆依[1～10位操作](multi-unit.md)，先DIGITS選位數、SETTEXT目前實際文字、ARM ALL、TEXT目標。校準設定使對應位置失效；改配置使全部位置失效。UNIT切換保留各位設定與位置。

## F5 選單

| 選項 | 功能 |
| --- | --- |
| MEGA: 建置韌體 | 編譯目前來源，顯示 Flash／RAM 與錯誤 |
| MEGA: 建置＋燒錄＋串列除錯 | 建置 → 上傳 → 驗證 → 串列監控 |
| MEGA: 串列除錯（115200） | 開監控，輸入 STATUS／WATCH／校準與試轉命令 |
| MEGA: 建置＋燒錄 | 建置、上傳、驗證後結束 |
| MEGA: 查詢連接埠 | 列出目前序列裝置；Unknown 不等於燒錄失敗 |
| MEGA: 執行全部主機測試 | 工作流程測試＋運動核心與 Arduino 介面模擬測試 |
| MEGA: 工具檢查 | 檢查 Python、Arduino CLI、AVR 核心、主機編譯器 |
| 主機中斷點: 歸零與運動核心 | 建置帶 PDB 的 C++ 測試，開啟 Windows 原生偵錯器 |
| 主機中斷點: Arduino介面模擬 | 對真正的 .ino 程式搭配模擬 Arduino I/O 做中斷點偵錯 |
| 主機中斷點: 多位顯示排程 | 檢查1～10位、非連續映射、逐顆TEXT與全組停止／故障取消 |

F5 的建置／燒錄／串列選項由 Python 啟動 Arduino CLI，偵錯器的變數是操作腳本。要檢查C++歸零或多位排程邏輯，請選對應的「主機中斷點」選項。

## 中斷點與單步執行

1. 在 [bench.cpp](unit_test/bench.cpp) 的 `Bench::tick`、`markerFound` 或 `homeDone` 設中斷點。
2. 選 **主機中斷點: 歸零與運動核心**，按 F5；會先編譯並在程式進入時暫停。
3. 按 F5 繼續到中斷點，F10 逐步執行，F11 進入函式。在變數／監看視窗檢查 `mode_`、`stableHall_`、`stageSteps_`、`remaining_` 與 `coils_`。
4. 要查串列命令剖析，可選 **主機中斷點: Arduino介面模擬**，在 [unit_test.ino](unit_test/unit_test.ino) 的 `command` 或 `loop` 設中斷點。
5. 要查多位狀態，選 **主機中斷點: 多位顯示排程**，在 [panel.cpp](unit_test/panel.cpp) 的 `Panel::startNext`、`tick`、`setUnits` 設中斷點，查看 `mapping_`、`enabledMask_`、`cursor_` 與 `units_`。

這些程式在電腦上執行，使用理想轉子與 Arduino stub，**不連接或驅動實板**。USB 對實板提供的是燒錄與串列通訊；[ATmega2560 的 JTAG 晶片偵錯](https://www.microchip.com/en-us/product/ATmega2560)另需相容的硬體、接線與工具，本配置未新增實板 JTAG 工作階段。

紅色停止鍵會結束啟動器與其子程序，串列模式也可 Ctrl+C 結束；停止監控不等於發送馬達 STOP。試轉停止用監控中的 `STOP`／任意非換行輸入或實體馬達分支開關。WATCH 中的 `x` 加 Enter 會退出並解除 ARM。

## 其他工作任務

Ctrl+Shift+P → **Tasks: Run Task／工作: 執行工作** 可找到：重新建置韌體、準備 AVR 核心、準備主機偵錯工具、重建接線圖，以及上述建置／上傳／測試功能。

這台電腦已備妥工具。新環境先安裝 Python 3.9 以上與 [Arduino CLI 1.5.1](https://docs.arduino.cc/arduino-cli/installation)，CLI 放在 PATH 或 `.tools/arduino/arduino-cli.exe`，然後執行：

1. **MEGA: 準備AVR核心**：安裝固定的 `arduino:avr@1.8.8` 至本專案 `.tools/`。
2. **MEGA: 準備主機偵錯工具**：需要時以 pip 安裝 `ziglang==0.16.0` 至 `.tools/host-compiler/`。
3. 安裝 VS Code 建議的 Microsoft Python、Python Debugger、C/C++ 擴充套件。

建置使用相對於 repo 的工具與快取目錄，不依賴上一輪本機 YAML 的絕對 D: 路徑，不修改全域 PATH 或 Git 設定。來源為 `mega2560/unit_test/`，AVR 產物位於 `mega2560/.build/`，主機偵錯 EXE／PDB 位於 `mega2560/test/.build/`，都不提交。

右下角 C/C++ 組態可切換 **Mega 2560 AVR**（實際 AVR 標頭與定義）及 **Host test Windows**（Arduino stub）。編輯 AVR 與主機測試時選對應組態，避免混用 `Arduino.h`。

## 常見狀況

| 狀況 | 處理 |
| --- | --- |
| 找不到 debugpy／cppvsdbg | 安裝上面的 Microsoft 擴充套件並重新載入視窗；原生偵錯配置適用 Windows VS Code |
| Python 找不到或版本過舊 | 執行 Python: Select Interpreter，選 Python 3.9 以上，再重試 |
| COM3 Access denied／被占用 | 關閉原串列監控；避免同時用兩個視窗開同一埠 |
| 串列有選單但馬達不轉 | 先 STATUS；U0 為 D22／24／26／28；核對12V馬達電源、共地與插頭後才 ARM／STEP |
| 命令正常完成、ULN閃但馬達嗡叫 | 關12V後先查四相是否插錯腳；若接線正確再評估 ORDER、方向及機構負載 |
| Settings disarm／位置失效 | 手動模式重新SET眼前字元、ARM；霍爾模式重新ARM、HOME |
| homed=0但reference=MANUAL | 正常，表示位置由SET宣告；可ARM後SHOW，不需霍爾 |
| SHOW 0與GOTO 0結果不同 | SHOW 0指定數字0；GOTO 0指定索引0空白頁，空白可用SHOW BLANK |
| UNIT disabled | 先用DIGITS 1～10或UNITS指定實際接上的位置；預設只有U0，無自動插拔偵測 |
| TEXT readiness／length錯誤 | 所有啟用位置須已定位並ARM；SETTEXT恰好DIGITS字元，TEXT最多DIGITS字元 |

VS Code 的[工作任務](https://code.visualstudio.com/docs/debugtest/tasks)與[偵錯配置](https://code.visualstudio.com/docs/debugtest/debugging-configuration)是本整合使用的標準入口。
