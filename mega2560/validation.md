# Mega 2560 驗證紀錄

日期：2026-10-25（Asia/Taipei）。軟體驗證與使用者回報的實機進度分列，尚未完成完整機構驗收。

## 使用者回報的實機進度

2026-10-25：已完成 COM3 上傳，串列輸出本版選單；ARM／STEP 回覆 BUSY、DONE，無軟體故障。使用者確認馬達外殼標示12V、ULN正負端量到12V。起初ULN閃燈、馬達嗡叫但不轉，發現IN1插在D22旁的錯誤接點；修正後回報已能轉動。

此紀錄確認初次上傳與單顆試轉進度，不代表已通過 HOME、每圈步數、OFFSET、45片、熱穩定或十顆同時運作驗收。

後續使用者回報：加減速版設定SPEED 1500「速度都還行」；磁鐵尚未到貨，並確認沿用原版45片字元順序。這是使用者對試轉的回報，未包含量測或完整負載／定位驗收。

2026-10-25手動定位版後續：使用者回報「測試通過，測試數輪皆無偏移」，並提供SHOW Z完成後與再次查詢的相同STATUS：

```text
SHOW Z
BUSY (any non-newline byte stops)
DONE coils off
U0 armed=1 homed=0 reference=MANUAL value=Z index=26 REVSTEPS=4096 OFFSET=0 SPEED=800 ACCEL=800 DIR=1 ORDER=1234 POLARITY=LOW error=none
```

記錄為**U0手動定位模式在SPEED 800µs、ACCEL 800半步/s²、REVSTEPS 4096設定下，多輪測試無可見偏移（使用者回報）**。其餘設定為OFFSET 0、DIR 1、ORDER 1234、POLARITY LOW；目前位置Z、第26格，無軟體錯誤。4096是測試使用的設定值，尚未以霍爾量測實際圈步數。

具體回合數、逐項目標／CYCLE紀錄與溫升未提供；本次回報不擴大到ACCEL 6000、霍爾歸零或十顆同時驅動的驗收。

## 可重複的軟體檢查

```sh
# 主機行為測試：需 g++／clang++（C++11）
python mega2560/test/run.py
# 也可使用 Zig 的 C++ 編譯器
python mega2560/test/run.py --zig /path/to/zig

# AVR 編譯，不上傳
arduino-cli core install arduino:avr@1.8.8
arduino-cli compile --fqbn arduino:avr:mega:cpu=atmega2560 --warnings all mega2560/unit_test

# 從同一份韌體腳位表重建三張 SVG
python mega2560/generate_wiring.py
```

| 項目 | 本次結果 |
| --- | --- |
| AVR 交叉編譯 | Arduino CLI 1.5.1，arduino:avr 1.8.8，AVR GCC 7.3.0，ATmega2560；成功 |
| 程式／RAM | 手動定位版 Flash **11,886 / 253,952 bytes**；全域 RAM **699 / 8,192 bytes**；不含執行時堆疊使用量 |
| 診斷 | 專案程式無編譯警告；首次完整建置有 Arduino 核心 new.cpp 未使用參數警告，未改第三方核心 |
| 純運動核心 | 15組行為測試通過：原有boot/ARM/STOP、已有磁區歸零、正向跨圈、Hall故障／5ms去抖、三圈量測、鎖存模型、45片循環、相序／方向、步數／時間上限、延遲不追趕、迴圈停滯、micros溢位、腳位唯一性、加減速與立即停止／重啟；新增高速巡航、手動目標、手動整圈／位置失效、字元映射 |
| 加減速輸出時序 | 觀察主機模型的步進排程時間：1／2／3／8／91／500／1500／20000半步，三角形與梯形曲線、前後對稱、起末10000µs、巡航不快於設定、最大20000步／ACCEL 6000的乘積；800／1000／1200µs長距離巡航；約一片91步在ACCEL 400與6000的差異；不代表實板波形量測 |
| 手動定位／跨圈 | 每圈4076與4096下各45×45、共4050組起點／目標：同頁不動、只前進、跨圈、數值更新；正／反電氣方向；連續45次翻頁的步數總和等於一圈，1000／20000圈步數邊界與時間溢位；全程只允許Moving／Settle／Pause，無Hall歸零 |
| 手動CYCLE／失效 | 從第0／17／44格各翻整圈4076步回原格；噪音Hall不影響；SET不輸出、不自動ARM，運動中拒絕直接SET；STOP／設定／RAW／LED取消位置；顯式HOME可恢復霍爾模式 |
| 歸零與量測隔離 | SPEED 5000與1500在理想模型中的HOME／OFFSET步數、停點與時間一致；高速設定下MEASURE各步至少5000µs、三圈各1000半步；未測真實磁鐵或高速漏步 |
| Arduino 介面 | 實際 .ino 配合主機stub測試通過：開機40相全低、CRLF、忙碌時整行停止／丟棄、過長／非法數字、SPEED／ACCEL邊界；SET／SHOW字母、數字0與BLANK、AE／UE、SETINDEX界限、連續目標、FLAP／GOTO／CYCLE、reference／value狀態、忙碌SET不可重新定位、設定／UNIT失效；原WATCH與極性、只驅動選定U |
| 主機工具 | Zig 0.16.0；專案測試以 C++11、-Wall -Wextra -Werror 編譯；Zig 首次建立標準函式庫有第三方 nullability 警告 |
| 圖面 | 三張SVG由pins.h生成；已轉成PNG人工檢視文字、線路、電源域及孔號；非Wokwi電氣模擬 |
| 文件／BOM | 與程式腳位、U9 D8–D11、單顆／十顆數量分開核對；原ESP32 BOM保留，歷史文件改指向該版本 |
| VS Code 工作流程 | 9項測試通過：先建置再驗證上傳、建置失敗不燒舊檔、上傳失敗不開監控、成功後才開監控、缺少／非法連接埠拒絕、建置不開埠、輸出路徑限制、Windows停止啟動器會清除子程序 |
| VS Code 原生偵錯產物 | 兩組 C++ Debug EXE／PDB 成功建置並執行通過；確認 PDB 格式與來源紀錄、launch參數符合已安裝擴充套件schema、preLaunchTask參照與唯一預設建置正確 |

VS Code 配置驗證未包含互動式中斷點／單步操作；本次未再次開啟實板COM埠或燒錄。實板串列除錯與電腦上的模擬中斷點入口見 [VS Code 指南](vscode.md)。

主機模型用理想磁區與理想轉子，不模擬扭力、慣性、線圈電流、接觸電阻、漏步、回灌、波形、機構卡滯、真實磁極或USB供電。鎖存模型通過不代表使用者手上模組已確認鎖存，亦不代表雙磁鐵機構已完成。既有ESP32 Wokwi驗收不能當作Mega驗收。

## 實機驗收進度

單顆上傳與試轉的使用者回報見上方；已勾選項目僅代表所述範圍，未勾選代表該整項仍未完整驗收。

- [x] U0手動定位：使用者回報SPEED 800／ACCEL 800／REVSTEPS 4096下測試數輪皆無偏移；SHOW Z與STATUS確認MANUAL、Z／26、error=none。
- [ ] 盤點Mega、ULN、12V馬達、Hall、線材的實際型號、數量、針序、公母頭與額定值。
- [ ] 驗證12V極性、保險絲、可及的馬達實體切斷、端子及主幹線規、共地回流。
- [ ] 斷電量ULN的COM、共同端、IN／插座映射；查模組LED及電阻可承受12V。
- [ ] USB控制電源電流／壓降；單顆通過後才評估十片Hall負載及Mega GPIO各腳／各port／全晶片總電流限制。
- [ ] Mega開機、RESET、watchdog重設、USB拔除／控制域斷電時相位確實關閉；必要時加下拉。
- [ ] 每片Hall供電、輸出高低、上拉、有效磁極、移遠釋放及接入INPUT_PULLUP後的行為。
- [ ] 實際2×1mm磁鐵與機構距離、每圈邊緣；不根據3.39V或印字直接判定鎖存。
- [ ] 單顆LED、短步、平順前進、三圈步數、OFFSET、45片與跨圈HOME。
- [ ] 補齊手動定位各項測試紀錄：SET實際字元、SHOW正向／跨圈／連續目標、數字0與BLANK、45片CYCLE回同頁；目前已有多輪無偏移回報，尚無逐項紀錄與具體圈數。
- [ ] 系統驗證SPEED／ACCEL調整、短／長距離起停、負載下實際頁面與重複停點；已回報通過組合為800／800，其他組合與長時間運轉仍待紀錄；磁鐵到貨後重新確認OFFSET。
- [ ] 馬達負載與溫升、ULN溫升、接點壓降、噪音／假觸發、實體停止可用。
- [ ] 分別抄錄十顆校準值；尚未做十顆同時驅動或長時間運轉驗收。

空白核取方塊代表尚未驗證。設定只在RAM；EEPROM、十馬達排程、網路／Cardputer／RTC均未實作。
