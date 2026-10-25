# Mega＋ESP32-CAM 統一更新可行性

> 歷史雙主控研究，基準保留於 `esp32-purchased-bom`。本分支已採用 [ESP32-CAM＋MCP23017 單主控](README.md)，只需一份韌體；未實作下列雙板更新器。

研究日期：2026-10-26（Asia/Taipei）。基準為 `cac9658`、ESP32 韌體 1.1.0、Arduino AVR 1.8.8、Arduino-ESP32 3.3.12。本文件是研究與實作建議；雙板更新器、USB 轉送及新版 Bootloader 尚未實作，也沒有改動或燒錄目前正常運行的韌體。

使用者於 2026-10-26 回報現版「實際測試通過，目前完美運行」。設計沿用現有 Mega、AI-Thinker ESP32-CAM、CP2102、P011、LM2596，不以新增模組為前提。

同日後續亦研究 [ESP32-CAM＋四片 MCP23017 單主控](single-controller-research.md)：使用者確認現有六片，該方向可免除雙板更新，但需重新驗證高速 I²C 與運動時序。本文件保留為維持雙板架構時的選項，不表示已決定實作雙板更新器。

## 可行性結論

| 需求 | 判定 | 必要改動 |
| --- | --- | --- |
| 單一 USB，固定插 CP2102，更新兩板 | 可行，建議作為長期有線入口 | ESP32 新增串列系統包接收器與 Mega 燒錄器；Mega 須有 ESP32 能到達的底層燒錄通道 |
| 手機網頁只上傳一包，更新兩板 | 可行，可與上述入口共用更新引擎 | 系統包、暫存／驗證、Mega 燒錄通道及分階段更新狀態機 |
| 單一 USB，固定插 Mega，更新兩板 | 可行；可保留標準 Mega Bootloader | Mega 轉送 ESP32 更新資料，PC 最後以同一 COM 埠更新 Mega；需另訂雙板供電切換流程 |
| 保留原廠 Mega Bootloader、目前全部接線不變，僅改包裝就達成網頁更新兩板 | 無法直接達成 | 現有 Serial1 不是原廠 Bootloader 的 UART0 燒錄通道 |

「單個有線 USB」解讀為電腦只需接一條 USB 資料線；不是由這條 USB 供應整台馬達。兩板共用同一個更新檔，但 Flash 仍須分階段更新，不能承諾兩板同時原子切換。

## 現況與限制

現有 ESP32 `controller/bridge.h` 使用 UART2、RX14／TX13，接 Mega Serial1 的 D19／D18；Mega USB 則走 Serial0。Arduino AVR 1.8.8 的 Mega 設定使用 `wiring`、115200，標準 STK500v2 Bootloader 對 ATmega2560 使用 UART0。應用程式能收到 Serial1 命令，不代表 Bootloader 能收到。[官方 Bootloader 原始碼](https://raw.githubusercontent.com/arduino/ArduinoCore-avr/1.8.8/bootloaders/stk500v2/stk500boot.c)、[板型設定](https://raw.githubusercontent.com/arduino/ArduinoCore-avr/1.8.8/boards.txt)

現有 `.sffw` 只有 64-byte 標頭與 ESP32 app，沒有 Mega 映像；`App::finishOta()` 只提交 ESP32 更新。現版不可接收雙板包。ATmega2560 的 SPM 自行寫 Flash 指令須從 Boot 區執行，不能只新增一條應用程式命令就覆寫自己。[Microchip AVR106](https://onlinedocs.microchip.com/oxy/GUID-3572900C-2427-41B3-A6FD-538FB595F65C-en-US-2/GUID-891AE076-5DBA-49C2-8709-44CFE3F6A353.html)

標準 Mega Bootloader 在 watchdog reset 時直接跳應用程式，不能假設 watchdog reset 就進入燒錄；需真正的 RESET 控制，或新版 Bootloader 明確定義進入條件。傳統 ESP32-CAM 也沒有 S2／S3 的原生 USB，仍須沿用 CP2102 或 Mega 的 USB 介面。[同一份 Mega 原始碼](https://raw.githubusercontent.com/arduino/ArduinoCore-avr/1.8.8/bootloaders/stk500v2/stk500boot.c)、[Espressif USB 支援範圍](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/usb.html)

## 建議共用架構

```text
電腦／VS Code ──單一 USB── CP2102 ──UART0── ESP32-CAM
                                                │
手機網頁 ──────────Wi-Fi／單包上傳─────────────────┤
                                                │ 現有 P011／Serial1
                                                ▼
                                       Mega 雙 UART Bootloader
```

有線日常更新由 ESP32 正常程式接收系統包，不必每次切 GPIO0／按 RESET；首次安裝接收器及 ESP32 故障救援仍保留原本的 ROM 燒錄方式。CP2102 供電腳不接，兩板維持現有控制 5V 供電，Mega USB 平時拔除。

Mega 底層通道有三種候選：

| 方案 | 優點 | 代價與判定 |
| --- | --- | --- |
| Bootloader 增加 UART1，並保留 UART0 | 保留已測通 Serial1 配線與 Mega USB 救援 | 建議的長期方案；需一次 ISP 安裝、Boot 區保護與防中斷設計。現成單一 UART1 版不等於雙 UART 版 |
| 保留標準 Bootloader，另接 UART0 TX／RX＋RESET 到 ESP32 | 不必自行修改 Bootloader | 要分配 ESP32 腳位及 P011 空路；須檢查 Mega USB 轉換晶片與 UART0 爭用。USB 沒插不代表其 TX 高阻，不能把 UART0／UART1 直接短接 |
| ESP32 每次經 SPI ISP 更新 Mega | 可避開串列 Bootloader | 不建議日常使用：Mega D50～D53 已接 U3／U7 馬達，ISP 會切換這些腳；另須處理 chip erase、EEPROM 與 Bootloader 保留 |

MegaCore 已提供 ATmega2560 選擇 UART0／1／2／3 燒錄的實例，證明 UART1 Bootloader 路徑可行；更改 Bootloader 須使用 programmer。這不是本專案雙 UART／防中斷功能的現成實作，也不代表改成 UART1 後原 USB 燒錄會自動保留。[MegaCore Bootloader 說明](https://github.com/MCUdude/MegaCore#bootloader-option)

建議新版 Bootloader 保留 UART0 STK500v2 救援，另支援 UART1。保護 Boot 區與 EEPROM 設定；寫入前記錄未完成狀態，整份映像讀回驗證成功後才允許啟動。中斷時保持輸出關閉、留在可重試狀態，不能僅檢查第一個字非 `0xFFFF` 就啟動半份應用程式。

ESP32 最好能經 P011 空路控制 Mega RESET，讓 Mega 應用程式損壞時仍能更新；GPIO 與 RESET 電氣接法須先驗證，目前不發布接線指令。不能把 ESP32 GPIO 直接接 5V RESET；GPIO12／2 等也涉及開機綁定。[Espressif 開機腳說明](https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/boot-mode-selection.html)

首次 ISP 安裝可研究讓現有 ESP32＋P011 暫充 programmer，避免新增採購；其時序、EEPROM 備份／還原、Bootloader／fuse／lock 驗證尚未實作。CP2102 本身不是 AVR ISP，不能因此宣稱首次安裝已解決。一次維護須關馬達支路，暫拆與 ISP 衝突的馬達訊號，完成後復原；不在正常機上直接試未驗證的底層寫入。

## 不改 Bootloader 的單一 Mega USB 路徑

另一條可獨立實作的路徑是 PC→Mega USB→現有 Serial1→ESP32。先在兩板安裝更新接收／轉送功能，PC 透過 Mega 串流更新 ESP32；確認 ESP32 重開成功後，工具釋放同一 COM 埠，再用現有 Arduino CLI／avrdude 更新 Mega，最後重連核對兩板版本。這是依現有 UART 架構推導的方案，尚未端到端驗證。

轉送採有限大小封包、序號、CRC、ACK／流量控制；不把 1MB 映像放進 Mega 的 8KB RAM。正常文字命令須暫停，不能讓現有 USB「動作中任意字元停止」規則解析更新資料。新 ESP32 須能暫時與舊 Mega 溝通，失敗時明確顯示哪一板已更新。

這條路徑需要 Mega USB 供電互斥：先關電、拆訊號、拔 Mega 外部 5V，再讓 Mega 由 USB、ESP32 由 LM2596 各自供電；確認兩板都有電後才能接回 UART 做轉送。現有個別燒錄指南把 UART 全程拆除，不能原封不動套用。恢復外部 5V 前須先拔 USB，供電切換不可經訊號線回灌。

115200、8N1 傳送 1.1.0 的 ESP32 app（1,088,528 bytes），單計資料至少約 94.5 秒，封包、驗證、重啟及 Mega 燒錄另計。單線更新可減少操作，但不能承諾幾秒完成。

## 單一系統包與暫存

建議新增版本化系統包，例如 `splitflap-system.sffw`，包含兩板映像、發布編號、板型／版本、協定相容範圍、最低更新器與 Mega Bootloader 版本、各映像長度／Flash 範圍與 SHA-256。舊單板包可繼續辨識；舊韌體對新 magic 必須拒絕，不能誤燒。日常包不修改 Bootloader、fuse、EEPROM 或 ESP32 分區表；初次部署另做維護。

單包網頁上傳不要求設備可上外網，家用 Wi-Fi／設備直連都可用；若要從網路檢查並下載新版本，屬於另一個版本來源功能。

目前 `min_spiffs` 有兩個各 1,966,080-byte OTA app 槽，但資料分區僅 131,072 bytes；最大 Mega app 為 253,952 bytes，不能假設資料分區足以暫存任意 Mega 韌體。[固定 3.3.12 分區表](https://raw.githubusercontent.com/espressif/arduino-esp32/3.3.12/tools/partitions/min_spiffs.csv)

候選是把 ESP32 app 寫入未啟用的 OTA 槽，在對齊後尾部暫存 Mega／清單。現版 `align4K(1,088,528) + 253,952 = 1,343,488` bytes，尚餘 622,592 bytes；這僅證明目前容量足夠，不是已驗證實作。每包須加入 metadata／擦除邊界做檢查，超限拒絕，不能跨到 NVS 或正在運行的 app。

ESP-IDF 可分開下載／驗證 app 與選取下次啟動分區，適合等 Mega 完成後才提交 ESP32；現有 Arduino `Update.end()` 的提交點須重構。擦除順序不能抹掉暫存 Mega；工作階段與檔案須可在斷電後重新驗證。[ESP-IDF OTA API](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32/api-reference/system/ota.html)

## 建議網頁工作流程

1. 完整收包、核對兩個映像及相容性；不邊收網頁資料邊覆寫 Mega。
2. 暫停時鐘、取得停止／解除 ARM 確認，建立持久化工作並進入專用維護狀態，拒絕一般操作。
3. ESP32 控制 Mega 進入 Bootloader，分頁寫入並讀回驗證。預期的心跳中斷由更新狀態機處理，不能被現有兩秒失聯判斷誤報，也不能永久停用正常保護。
4. Mega 新版以未 ARM 狀態開機，回報版本／發布編號；確認仍能與舊 ESP32 溝通。
5. 提交 ESP32 app 並重開，確認兩板版本與儲存正常後完成工作。頁面重連可查階段與故障；最後核對保留字元，不自動轉動。

每包須有過渡相容性，協定大改版須經過渡版本。Mega 沒有現成雙 app 槽，不能承諾兩板同時回退；目標是停止、辨識階段、保留重試能力。ESP32 也須核對實際 Bootloader／核心回退設定與確認時點，不能將兩個 OTA 槽等同已驗證功能回退。

## 後續實作順序與驗收

先固定包格式與驗證器，再以獨立原型驗證 Mega UART1／UART0 Bootloader、RESET 與首次 ISP 安裝；保留可還原的現版映像與校準。之後整合 ESP32 共用更新引擎、CP2102 單 USB／VS Code 入口及手機單包頁面。若先求有線便利，可獨立先做保留原 Bootloader 的 Mega USB 轉送方案。

驗收須涵蓋錯板型、損壞包、地址越界、重複封包、每階段斷線／斷電、Mega 半寫入、ESP32 開機失敗、過渡版本、EEPROM／NVS 保留、USB 救援與完成後未 ARM，之後才宣告可用。

本次只讀取現有程式、固定核心與建置產物，核對 Mega Intel HEX 校驗和及容量，查閱一手文件；沒有開啟 COM 埠、燒錄、重設兩板、變更接線或驅動馬達。
