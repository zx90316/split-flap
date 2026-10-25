# 單主控逐線接法

適用 `codex/esp32cam-single-controller`／韌體 2.0.0。使用現有 ESP32-CAM、P011 8 路轉換板與四片 MCP23017；其餘兩片 MCP 備用。原 Mega 的 UART、馬達與 Hall 線全部移除，不能並聯兩個輸出來源。

[正常總圖](wiring-runtime.svg) · [CP2102](wiring-flashing.svg) · [十組馬達](wiring-motors.svg) · [十路 Hall](wiring-hall.svg)

## 電源

改線前關閉控制電源、馬達分支並拔除 USB。以下是電氣端點對應，排針順序依實板印字。

| 來源 | 目的 |
| --- | --- |
| 12V 正端經保險絲與主開關 | LM2596 IN+；另一路經獨立馬達開關到各 ULN 正端／COM |
| 12V 負端 | 共地分配點、LM2596 IN− |
| LM2596 OUT+，已量測 5.0V | ESP32-CAM **5V**、四片 MCP **VCC**、Hall VCC、P011 **VCCB** |
| LM2596 OUT− | 共地分配點 |
| ESP32-CAM **3V3 輸出** | P011 **VCCA** |
| 全部板子 GND、ULN GND、CP2102 GND | 共地分配點 |

先空載調 LM2596，再接板量 Wi-Fi 負載下的 5V。馬達不能由 MCP／ESP 供電；MCP／ESP 不能接 12V。ESP 使用明確印「5V」的腳，3V3 和用途未確認的 VCC 不接外部 5V。CP2102 的供電腳一律不接。

開關只切馬達 12V 分支時，ESP 與 MCP 控制電源仍在。正常運作不需要 Mega、其 USB 或外部 5V。沿用現有電源與配線；本方案未加入 TXU0202 或其他新模組。

## ESP、P011 與四片 MCP 的共用線

| ESP32-CAM | P011 低壓側 | P011 高壓側 | 四片 MCP 並接端點 |
| --- | --- | --- | --- |
| GPIO13 | A0 | B0 | SDA |
| GPIO14 | A1 | B1 | SCL |
| GPIO15，開漏輸出 | A2 | B2 | **RST**，低有效 |
| 3V3 | VCCA | — | 不接 MCP VCC |
| — | — | VCCB＝控制 5V | VCC＝控制 5V |
| GND | GND | GND | GND |

P011 A3～A7、B3～B7 不接，沒有 OE 腳。MCP 的 ITA／ITB 中斷輸出不接，本版以輪詢取樣。四片 RST 並在一起，由 GPIO15 經轉換板控制；不能把這條 RST 固定短接 GND。韌體開機先拉低 RST、設好低輸出後才開放操作；I²C 故障也會拉低 RST。RST 只重設 MCP，不能切斷 12V 或涵蓋所有硬體故障。

MCP 在 5V 時的保證輸入門檻不適合直接接 ESP 3.3V 高電位；SDA／SCL／RST 均經現有轉換板。不要讓高壓側上拉直接接到 ESP。關電改線，訊號不對單側未供電的板熱插拔；全部控制板供電後才測試。

匯流排使用短線、共地，速率 **400 kHz**。現有 P011 及四片板子的上拉阻值、總電容與上升時間尚未實測；先驗收一片再擴充，先前 UART 成功不能當作 I²C 已驗收。線交叉但沒有接點圓點時不相接。

ESP32-CAM 的 GPIO13／14／15 與 SD 插槽共用，**不插 SD 卡**。相機、閃光燈不啟用；U0R／U0T 留給 CP2102，不能拿來接 MCP。

## MCP 地址與板上名稱

地址採 **7-bit 0x20～0x23**。先斷電依板上 A0／A1／A2 跳線設定，不浮接；表中的 0＝GND、1＝MCP VCC 5V，實際焊橋位置依板上標示。

| 地址 | A2 | A1 | A0 | 用途 |
| --- | --- | --- | --- | --- |
| 0x20 | 0 | 0 | 0 | U0～U3 馬達 |
| 0x21 | 0 | 0 | 1 | U4～U7 馬達 |
| 0x22 | 0 | 1 | 0 | U8～U9 馬達 |
| 0x23 | 0 | 1 | 1 | U0～U9 Hall |

MCP 地址 A0／A1／A2、MCP 的 GPA/GPB 輸出、P011 的 A0/B0 是不同名稱，勿互相混用。以「GPA0」或板上同義的「A0 輸出」辨認相位腳，不把地址跳線接到 ULN。

使用者提供的 [IMD-008087 模組頁面](https://www.taiwansensor.com.tw/product/mcp23017-e-ss-arduino-i2c%E4%BB%8B%E9%9D%A216%E8%B7%AFio%E6%93%B4%E5%B1%95%E6%A8%A1%E7%B5%84-iic-%E8%BC%B8%E5%85%A5%E8%BC%B8%E5%87%BA%E6%93%B4%E5%B1%95%E6%9D%BF/)列出 VCC、GND、SDA、SCL、RST、ITA、ITB；實板仍需核對印字及地址設定。

## MCP 到十組 ULN

每列四腳**依序**接對應 ULN 的 **IN1、IN2、IN3、IN4**；馬達五線插頭維持接該 ULN 的原插座。這些是控制訊號，不直接驅動線圈。

| 顯示位置 | MCP 地址 | IN1 | IN2 | IN3 | IN4 |
| --- | --- | --- | --- | --- | --- |
| U0 | 0x20 | GPA0 | GPA1 | GPA2 | GPA3 |
| U1 | 0x20 | GPA4 | GPA5 | GPA6 | GPA7 |
| U2 | 0x20 | GPB0 | GPB1 | GPB2 | GPB3 |
| U3 | 0x20 | GPB4 | GPB5 | GPB6 | GPB7 |
| U4 | 0x21 | GPA0 | GPA1 | GPA2 | GPA3 |
| U5 | 0x21 | GPA4 | GPA5 | GPA6 | GPA7 |
| U6 | 0x21 | GPB0 | GPB1 | GPB2 | GPB3 |
| U7 | 0x21 | GPB4 | GPB5 | GPB6 | GPB7 |
| U8 | 0x22 | GPA0 | GPA1 | GPA2 | GPA3 |
| U9 | 0x22 | GPA4 | GPA5 | GPA6 | GPA7 |

0x22 的 GPB 全部保持低；其他未啟用 U 的四路也保持低。每片 ULN 的正端／COM＝12V_MOTOR，GND＝共地。COM 用於線圈回掃，不接控制 5V。機構方向／線序若不同，以 ORDER／DIR 校準；先關馬達電源再改線。

## Hall 到 0x23

Hall 預設關閉，手動定位不需要 0x23。完整接好、裝上磁鐵並確認訊號後，才在設定頁啟用 Hall。

| 位置 | KY-003 S／OUT 接 0x23 |
| --- | --- |
| U0 | GPA0 |
| U1 | GPA1 |
| U2 | GPA2 |
| U3 | GPA3 |
| U4 | GPA4 |
| U5 | GPA5 |
| U6 | GPA6 |
| U7 | GPB0 |
| U8 | GPB1 |
| U9 | GPB2 |

各 KY-003 的 +／VCC＝控制 5V，−／GND＝共地；每路訊號獨立。針序以實物 S／+／− 印字為準。**GPA7、GPB7 不接 Hall**；依 [Microchip DS20001952](https://ww1.microchip.com/downloads/aemDocuments/documents/APID/ProductDocuments/DataSheets/MCP23017-Data-Sheet-DS20001952.pdf) 的輸入限制，本版讓這兩腳保持低輸出，其餘未使用腳也保持低。

韌體為十個使用中的 Hall 輸入開啟上拉，2ms 取樣，預設 LOW 觸發。先用 WATCH 確認磁鐵通過時確實觸發、離開後釋放，再測 HOME／MEASURE；未裝磁鐵維持手動 SET／SHOW。啟用 Hall 後，0x23 缺席會拒絕運動，不會默默當作無感測器。

## CP2102 與 USB 燒錄

| CP2102 | ESP32-CAM |
| --- | --- |
| TXD，須 3.3V 邏輯 | U0R／GPIO3 |
| RXD | U0T／GPIO1 |
| GND | 共地 |
| 5V／3V3／VCC | **全部不接** |
| DTR／RTS | 不接 |

燒錄期間仍由 LM2596 的 5.0V 供電。GPIO0→GND 是**臨時跳線**，與接到 MCP 的 GPIO15 RST 不是同一條線。

1. 關馬達分支，控制 5V 保留，確認 CP2102 供電腳未接。
2. GPIO0 接 GND，按 ESP32-CAM 的 RESET。
3. VS Code 查詢 CP2102 COM，執行「ESP32-CAM: 建置＋燒錄＋串列監看」。
4. 成功後移除 GPIO0 跳線，再按 RESET，115200 查看 STATUS。
5. 先保持馬達分支關閉，確認 MCP 初始化及配置，再依 [操作指南](README.md)核對字元、ARM 與試轉。

首次由雙主控移轉須 USB；後續僅上傳本版一包 `.sffw`。燒錄不需要拔下 MCP 訊號，前提是共地與全部控制板供電正常；全程關馬達支路，因 ROM 燒錄階段不執行應用程式的 RST 控制。
