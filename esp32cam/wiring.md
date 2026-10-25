# ESP32-CAM、CP2102、Mega 接線表

圖為功能接線，**依板上印字與量測接線，不依方塊內的上下排列猜排針順序**。目標板為兩側各八針的 AI-Thinker ESP32-CAM；相機與 SD 卡未使用。

本版沿用現有 **P011：8 路分立 MOS 電平轉換板**（照片標示 VCCA／A0～A7、VCCB／B0～B7）、LM2596、CP2102 及跳線，不新增模組。Mega 改從已調整的穩壓 5V 腳供電；接電腦 USB 前須先拔外部 5V，不能並接。

## 正常運作

![正常接線](wiring-runtime.svg)

| 從 | 接到 | 說明 |
| --- | --- | --- |
| 12V 正端 | 主保險絲／主開關後分兩支路 | 電源額定、保險絲及線規依實測選定 |
| 控制支路 12V | LM2596 IN+ | 控制支路在馬達分支開關之前 |
| 12V 負端 | LM2596 IN−／OUT−及共地分配點 | 確認模組為非隔離降壓；正負不能接反 |
| LM2596 OUT+（先調 5.0V） | ESP32-CAM **5V** | 不接 3V3 或未確認用途的 VCC |
| LM2596 OUT+（穩壓 5.0V） | Mega **5V** | 此接法直接進入 5V 電源軌；Mega USB 必須拔除，不接 VIN／DC 圓孔 |
| LM2596 OUT− | ESP32 GND、Mega GND | 共地；Mega 外部 5V 線須能在燒錄前拔除 |
| 馬達分支 12V | 獨立分支開關 → 各 ULN 正端／COM | 不接 Mega、Hall、CP2102、ESP32 GPIO |
| 各 ULN GND | 共地分配點 | 馬達回流不穿過控制板、USB 或 Hall 細線 |
| ESP32 GPIO13 TX2 | P011 **A0** | 第一條訊號的 3.3V 端 |
| P011 **B0** | Mega D19 RX1 | A0↔B0，同一路轉為 5V |
| Mega D18 TX1 | P011 **B1** | 第二條訊號的 5V 端 |
| P011 **A1** | ESP32 GPIO14 RX2 | B1↔A1，同一路轉為 3.3V |
| ESP32 **3V3** | P011 **VCCA** | A 側參考電源；不能接 5V |
| Mega **5V** 電源節點 | P011 **VCCB** | B 側參考電源；與 Mega 使用同一電源域 |
| P011 **GND** | 共地 | ESP32 GND、Mega GND 均接此共地 |
| P011 A2～A7、B2～B7 | **不接** | 剩餘六路保留，不串接、不接地 |

**A0／B0 是一組，A1／B1 是另一組。**A／B 表示電壓側，不表示固定發送／接收方向。板上沒有 OE，不需要找 OE 或另外接使能線；每側重複的 VCCA／VCCB／GND 選一組接線。使用板上既有元件，不新增電容採購。

此類 MOS＋上拉的轉換電路可用於 TTL UART，但上升時間受上拉與線長影響；同類電路的製造商說明見 [Adafruit BSS138 轉換板](https://www.adafruit.com/product/757)。這是電路類型的參考，**不是這片 8 路板的型號或速率認證**；照片不足以確認 MOS 型號、上拉阻值或斷電隔離。板間韌體維持 **115200、8N1**，USB 監看也是 115200，不因改接本板而重刷韌體。

首次接線先關馬達支路、拔 Mega USB：

1. 所有電源關閉，只接兩板供電、P011 VCCA／VCCB／GND；先不接 A0／A1／B0／B1 的四條訊號線。
2. 兩板同時供電，量 VCCA 與 A0／A1 待機電壓約 3.3V、VCCB 與 B0／B1 約 5V。電壓側不符時先排查，不能把 5V 側接進 ESP32 GPIO。
3. 關閉控制電源，再接上四條訊號線；從短跳線開始，遠離馬達配線。重新供電後以網頁狀態或 ESP32 `STATUS` 確認 Mega 連線持續穩定，再測 U0。

**兩板都供電時才保留板間訊號線。**現有板不假設有單側斷電高阻保護；切換任一板電源或單板測試前，先關電源並拔 A0／A1／B0／B1 四條訊號線，避免經轉換板或 GPIO 回灌。GND 保持共地。此限制不能由通訊協定或失聯停止取代。

現有 LM2596 與配線需能連續承受兩板、Wi-Fi 峰值及 Hall 負載。不能把 LM2596 IC 標稱 3A 視為任意小模組的實際連續能力；先量各板 5V 壓降、電流與模組溫升。控制線保持短，遠離馬達主幹。

Mega 原馬達／Hall 腳位與配置見 [Mega 接線表](../mega2560/README.md#十字元腳位與-4p-方向)。GPIO13／14 與 microSD 腳位共用，因此本韌體不初始化 SD 卡。UART0 U0R／U0T 留給燒錄和紀錄；沒有使用 GPIO16／17 這組可能與 PSRAM 衝突的預設 UART 腳位。

## Mega 改接電腦燒錄

1. 關閉馬達分支及 LM2596 控制電源；拔掉 P011 A0／A1／B0／B1 的四條訊號線。
2. **拔除 LM2596 OUT+ → Mega 5V 的外部供電線**，確認沒有其他外部 5V 接入 Mega，再接電腦 USB。Mega 改由電腦 USB 供電；控制電源此時保持關閉。
3. 執行 Mega 燒錄／串列調機。Mega 的外部 5V 與 UART 訊號維持拔除，避免透過 P011 把電送到未供電的 ESP32。不使用 USB Y 型電源線。
4. 完成後先拔電腦 USB，確認控制電源仍關閉，再復原 Mega 外部 5V 與四條 UART 訊號線。重新開啟控制電源，確認兩板連線；重新定位、ARM 後才開馬達分支測試。

正常運作使用既有 LM2596 與跳線，不需要 USB-A 電源小板。**Mega 5V 腳接的是穩壓後的 5.0V，絕不能接 12V；接電腦 USB 時外部 5V 必須實際拔除，不能只關馬達開關。**

從 Mega 5V 腳直供會略過板上穩壓與 USB 入口保護，電壓及極性由外部電源負責；[Arduino 官方電源說明](https://store.arduino.cc/products/arduino-mega-2560-rev3)也提醒這個限制。因此本接法以 LM2596 空載、負載均確認為穩壓 5.0V 及上述供電互斥為前提。

## CP2102 燒錄 ESP32-CAM

![燒錄接線](wiring-flashing.svg)

| CP2102／操作 | ESP32-CAM | 說明 |
| --- | --- | --- |
| TXD | U0R／GPIO3 | 3.3V UART 訊號，TX→RX |
| RXD | U0T／GPIO1 | RX←TX |
| GND | GND | 與 LM2596、Mega 共地 |
| 5V／3V3／VCC | **不接** | ESP32 由 LM2596 5V 供電 |
| DTR／RTS／RST | 不接 | 本流程以板上 RESET 鍵手動切換 |
| 燒錄跳線 | GPIO0→GND | 接好後按 RESET，才進入 ROM 燒錄模式 |
| 上傳完成 | 移除 GPIO0→GND，再按 RESET | 開始正式程式；不會自動轉動 |

CP2102 USB 插電腦。確認轉接板 TXD 邏輯為 3.3V；「5V 輸出腳」不代表 TXD 可設為 5V，也不能把 CP2102 的 3V3 腳當 ESP32-CAM 電源。[Silicon Labs CP2102/9 資料表](https://www.silabs.com/documents/public/data-sheets/CP2102-9.pdf)

GPIO0 只在重設時決定啟動模式；上傳後不拔跳線、直接再重設會再次進入燒錄模式。GPIO2 保持原板狀態，不另加跳線。[Espressif Boot Mode Selection](https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/boot-mode-selection.html)

CP2102 與 Mega 使用兩組獨立 UART。燒錄 ESP32 時，兩板都由 LM2596 供電、Mega USB 拔除，P011 板間接線可以保留；如果只供電 ESP32，先拔 P011 四條訊號線。燒錄／重啟前仍先關實體馬達支路。成功後移除跳線並 RESET，115200 串列輸入 `ACCESS` 取得 Wi-Fi 與 admin 密碼。
