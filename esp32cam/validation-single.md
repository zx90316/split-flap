# 單主控 2.0.0 驗證紀錄（NodeMCU ESP-32S＋MCP23017）

分支 `esp32s-single-controller`。本次只做軟體建置與電腦端測試：**沒有開啟 COM 埠、燒錄、接 MCP 或控制實體馬達**。雙主控 1.3.x 的現場紀錄見 [validation.md](validation.md)，不視為單主控實測。

## 2026-11-02：首次實作

設計：`LocalController` 與雙主控的 `Bridge` 介面相同（snapshot／submit／reserve／complete／maintenance），執行相同的 sf:: 命令，所以 1.3.x 的網頁、登入、校準精靈、復電、時鐘輪播與 OTA 流程原封沿用。運動核心從 `mega2560/unit_test` 移到 `libraries/SplitFlapMotion`，Mega 與 ESP32 共用同一份；只新增「步進可延後」的鉤子，預設行為不變。編譯旗標 `SPLITFLAP_SINGLE` 切換兩種韌體。

| 驗證 | 結果與範圍 |
| --- | --- |
| 單主控建置 `dev.py build --single` | 通過；Flash 1,120,477／1,966,080 bytes，全域 RAM 57,936／327,680 bytes；Arduino-ESP32 3.3.12 |
| 雙主控建置 `dev.py build` | 通過；共用核心抽出後仍可建置 |
| 單主控原生測試（`test_local`） | 5 組通過：1～10 位並行、MCP 實際寫入間隔不低於 800µs、未啟用位置輸出為低、稀疏映射、STOP 優先、NACK 拉低 RST、缺少 MCP 拒絕動作且 STOP 可重新偵測、暫存器被重設偵測、NVS 133 個截斷點、micros 溢位、CYCLE／FLAP／GOTO／STEP／REV、霍爾 HOME 與三圈量測、App 介面（reserve／complete 去重、ActionFlow ARM＋HOME、快照序號、外部工作保留、OTA 期間 RST 保持低） |
| 既有 ESP32 原生測試 | 全部通過（UART 橋接、動作流程、OTA guard、顯示、設定、登入） |
| Mega 原生測試 | 8 組通過；共用核心移動後不變 |
| Python 工具測試 | ESP32 15 項、Mega 9 項通過 |
| 手機網頁 Playwright | 8 組雙主控流程通過；另以 `mock_web.py --single` 手動確認單主控狀態、MCP 缺席訊息、接線表及 375px 寬無水平捲動、無 JS 錯誤 |
| 產物 | `esp32cam/dist/splitflap-esp32s-single.sffw`／`.bin`／`.json`（不提交）；OTA 專案 `splitflap-32s-s1`，與雙主控 `splitflap-32s-v1` 互相拒收 |

## 實板驗收步驟（待做）

依序進行，任何一步失敗就停在該步，不往下接更多馬達。

1. **備份**：在現有雙主控網頁匯出設定（含十組 OFFSET／REVSTEPS）。單主控的校準存在 ESP32 NVS，不會自動從 Mega EEPROM 帶過來。
2. **斷電改線**：照 [wiring-single.md](wiring-single.md) 移除 Mega 與 UART 線；確認 P011 狀態良好；四片 MCP 地址各不相同。
3. **燒錄**：只接 USB（拔外部 5V），`python esp32cam/tools/dev.py upload --single --port COM7`。序列埠輸入 `STATUS`，應看到 `MCP=ready present=0x…`。
4. **I²C 波形**：接上 MCP（先不開 12V），量 SDA／SCL 上升時間與低電位；`STATUS` 的 `maxFlush` 應穩定。
5. **1 位原型**：只設 1 位（需 0x20＋0x23），開 12V。匯入備份或重新校準 U0，歸零、偏移、文字、全部停止。
6. **800／800 速度**：在 Wi-Fi 連線、頁面操作同時，連續翻多輪 45 片並回到同一字元，確認不漏步；記錄 `late`／`maxGap`。
7. **擴到 4 位、10 位**：混合不同距離與校準；並行翻頁、時鐘輪播、復電自動復位。
8. **故障**：運轉中拔 SDA、拔一片 MCP、按 ESP32 EN 重開，確認輸出關閉且不會自動轉動；OTA 更新一次並確認更新中無線圈通電。

量到的數據與異常補記在本檔。
