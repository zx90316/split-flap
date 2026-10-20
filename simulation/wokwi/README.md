# ESP32 十字元數位接線示範

本目錄為獨立的 Wokwi 診斷專案，對應本專案 ESP32、四片 MCP23017、十組 ULN2003／12V 28BYJ-48，以及十路 Hall 的邏輯通道。不是硬體正式韌體，也不執行翻頁歸零或字元定位。

線上專案：[開啟已驗證的十通道 Wokwi 示範](https://wokwi.com/projects/478367216064916481)。直接開啟後按播放即可；修改後可另存複本。

## 快速複現

1. 開啟 https://wokwi.com/projects/new/esp32 。
2. 程式檔案列右側選單選 **Upload file(s)...**。
3. 上傳三組 `*.chip.c` 與 `*.chip.json`，共六個模型檔案。網站不允許上傳同名既有檔案；切到 `sketch.ino` 與 `diagram.json` 頁籤，分別全選後貼上本目錄同名檔案的完整內容。
4. 按綠色播放鍵。預期序列埠顯示四個 MCP 位址初始化檢查及 READY。
5. 切換 H_U0–H_U9 開關，觀察序列埠的十位 Hall 狀態。馬達觀察器顯示四相訊號與假定位置。
6. 序列埠輸入 `s` 停止、`a` 全部運轉、`0`–`9` 單獨測試一路、`r` 反轉。

`python3 generate_diagram.py` 可決定性重建 diagram.json。所有四十相線與十路 Hall 都明確列在檔案中，不需要手動拖線。

## 模型與硬體的界線

- MCP23017 僅實作 BANK=0 所需方向、極性、上拉、GPIO、OLAT、IOCON SEQOP 與低有效 RESET；沒有中斷或完整錯誤／類比模型。
- 每個自訂馬達方塊合併 ULN2003 的理想低側驅動及四線圈單極馬達相序觀察，不使用雙極步進馬達冒充。指針依 4096 半步／圈的示範假設繪製，實際減速比須校正。
- 5V、12V 標記都是布林 HIGH，不能驗證電壓、電流、ULN 壓降、散熱、扭矩、失步、供電時序或 USB 回灌。
- 模擬圖省略電位轉換的電氣作用，ESP32 與 MCP 的直接數位線僅適用模擬。實機必須依 `docs/esp32/wiring.md` 經 3.3V↔5V 轉換，禁止照模擬圖直連不同電源域。
- Hall 用手動接地／斷開開關代替磁感測器；與轉軸位置無連動，不能當成自動歸零測試。
- 本診斷程式在模擬啟動後會自動輸出相序；不要燒進實機。正式韌體位於 `firmware/esp32`，預設上電不運轉。

## Wokwi MCP

官方 `wokwi-cli` 提供實驗性 stdio MCP：`wokwi-cli mcp`。執行模擬需要 Wokwi CI token，並非免登入遠端 MCP endpoint。不要把 token 寫入 repo 或聊天訊息。此次網站配線途徑不需要 token；沒有宣稱 MCP 已連線測試成功。

參考：[官方 CLI／MCP](https://github.com/wokwi/wokwi-cli/blob/main/README.md)、[Custom Chips API](https://docs.wokwi.com/chips-api/getting-started)。

## 驗證狀態

本目錄的線路拓撲可以用 `python3 verify_diagram.py` 檢查。2026-10-20 已在 Wokwi 網站完成編譯並運行：四個位址方向／OLAT 檢查 PASS；H_U0、H_U7、H_U9 手動切換分別對應正確十位輸出；`s` 回報四十輸出為低，`a` 恢復全通道相序。其餘 Hall 映射經靜態拓撲檢查，未逐個點擊驗證。這些只證明數位示範，不代表正式韌體或實機驗證。
