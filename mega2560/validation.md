# Mega 2560 驗證紀錄

日期：2026-10-25（Asia/Taipei）。軟體已驗證，尚未對實板燒錄、供電或轉動馬達。以下不是實物通過紀錄。

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
| 程式／RAM | Flash **9,896 / 253,952 bytes**；全域 RAM **605 / 8,192 bytes**；不含執行時堆疊使用量 |
| 診斷 | 專案程式無編譯警告；首次完整建置有 Arduino 核心 new.cpp 未使用參數警告，未改第三方核心 |
| 純運動核心 | 8組行為測試通過：boot/ARM/STOP、從已有磁區離開再歸零、正向跨圈、卡HIGH／LOW、5ms去抖、三圈量測、理想雙磁極鎖存模型、45片循環、相序／方向、步數／時間上限、延遲不追趕、迴圈停滯、micros溢位、腳位唯一性 |
| Arduino 介面 | 實際 .ino 配合主機stub測試通過：開機40相全低、CRLF不誤停、任何非換行輸入停止、整行丟棄、過長／非法數字拒絕、設定失效、只驅動選定U、WATCH與極性 |
| 主機工具 | Zig 0.16.0；專案測試以 C++11、-Wall -Wextra -Werror 編譯；Zig 首次建立標準函式庫有第三方 nullability 警告 |
| 圖面 | 三張SVG由pins.h生成；已轉成PNG人工檢視文字、線路、電源域及孔號；非Wokwi電氣模擬 |
| 文件／BOM | 與程式腳位、U9 D8–D11、單顆／十顆數量分開核對；原ESP32 BOM保留，歷史文件改指向該版本 |

主機模型用理想磁區與理想轉子，不模擬扭力、慣性、線圈電流、接觸電阻、漏步、回灌、波形、機構卡滯、真實磁極或USB供電。鎖存模型通過不代表使用者手上模組已確認鎖存，亦不代表雙磁鐵機構已完成。既有ESP32 Wokwi驗收不能當作Mega驗收。

## 實機檢查（目前均未執行）

- [ ] 盤點Mega、ULN、12V馬達、Hall、線材的實際型號、數量、針序、公母頭與額定值。
- [ ] 驗證12V極性、保險絲、可及的馬達實體切斷、端子及主幹線規、共地回流。
- [ ] 斷電量ULN的COM、共同端、IN／插座映射；查模組LED及電阻可承受12V。
- [ ] USB控制電源電流／壓降；單顆通過後才評估十片Hall負載及Mega GPIO各腳／各port／全晶片總電流限制。
- [ ] Mega開機、RESET、watchdog重設、USB拔除／控制域斷電時相位確實關閉；必要時加下拉。
- [ ] 每片Hall供電、輸出高低、上拉、有效磁極、移遠釋放及接入INPUT_PULLUP後的行為。
- [ ] 實際2×1mm磁鐵與機構距離、每圈邊緣；不根據3.39V或印字直接判定鎖存。
- [ ] 單顆LED、短步、平順前進、三圈步數、OFFSET、45片與跨圈HOME。
- [ ] 馬達負載與溫升、ULN溫升、接點壓降、噪音／假觸發、實體停止可用。
- [ ] 分別抄錄十顆校準值；尚未做十顆同時驅動或長時間運轉驗收。

空白核取方塊代表尚未驗證。設定只在RAM；EEPROM、十馬達排程、網路／Cardputer／RTC均未實作。
