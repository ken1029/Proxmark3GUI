# 快速操作說明（mfc-r9）

本文件從 `Start-GUI.bat` 開始，對應 GUI `0.4.0`。日常流程都在「MFC workbench」完成；Classic、Raw、LF、T55xx 舊頁面保留。

## 1. 啟動與連線

1. 解壓縮整個 ZIP，雙擊 `Start-GUI.bat`。GUI 會從 `gui\` 載入 Qt DLL，client 在 `client\proxmark3.exe`。
2. 上方狀態列顯示 `Device:`、`Card:`、`Document:`。`Device:` 未連線時會提醒目前選擇的 COM 埠。
3. 在 `Settings` 頁確認 client 路徑與參數（預設 `<port> -f`），回到 `Mifare` 或 `MFC workbench`，選擇 COM 埠後按「連線」。
4. 連線成功後狀態列顯示 `Connected`；`Diagnostics` 頁可看到 client／韌體版本、USB 序號、capability manifest 與握手錯誤。

> 換 COM 編號：只要 USB 序號相同，GUI 在裝置重新出現時會自動選回同一個裝置並重連；沒有序號或同時有多台候選時，請手動選埠。

## 2. 單張 Classic 卡：辨識 → 驗證 → 讀取 → 備份

1. 開「MFC workbench」，在「資料與備份」頁按「辨識卡片」。狀態列會出現 UID 與卡型。
2. 若已有金鑰檔，按「載入金鑰…」；否則先按「驗證已知金鑰」。只有目前 RF session 通過認證的金鑰才算 `verified`。
3. 按「讀取缺漏區塊」讀完目前金鑰可讀的範圍。未知區塊保持 `?`，不會被當成 `00`。
4. 按「儲存備份…」輸出 JSON／EML／BIN。JSON 會另寫 `<檔名>.sha256`；BIN 遇到未知區塊會先警告。
5. 摘要中的「定位第一個缺漏金鑰／區塊」可直接跳到對應列。

## 3. 補齊金鑰（批次）

1. 按「補齊缺少的金鑰…」。視窗列出每個尚未在目前 session 驗證的 `(sector, Key A/B)`。
2. 方法：
   - `Verify candidates`：只驗證已有候選值的金鑰。
   - `Dictionary (chk)`／`Automatic (autopwn)`：client 一次處理整張卡；結果由同一份輸出合併。
   - `Nested`／`Static nested`／`Hardnested`：GUI 逐目標調度，會自動挑一個已驗證的起始金鑰，也可手動指定。
   - `Darkside`：只處理 sector 0 Key A。
3. 任務列顯示目前 sector／A 或 B、總數、已處理、已驗證、仍缺。單項失敗會記錄後繼續。
4. 按「停止」只停止等待；已驗證結果保留。之後可按「只重試缺漏」或「讀取剩餘區塊」。
5. 命令輸出取得的金鑰若尚未在目前卡片上認證，會標成 `candidate`，不會冒充 `verified`。

## 4. 多文件與草稿

- 「Document」選單：新增、開啟傾印、儲存、重新命名、關閉、恢復草稿。
- 每個分頁有自己的文件、來源、未儲存狀態、篩選、搜尋、選取與捲動位置。分頁標題前的 `*` 表示尚未儲存。
- 未儲存文件每 15 秒寫入 `drafts\`（原子寫入）。重新啟動後用「Document → 恢復草稿…」開啟；恢復後會標示為歷史 RF 證據，讀寫前請重新辨識卡片。
- 執行中的任務會回到啟動它的分頁；同步讀取／驗證迴圈執行時不允許切換分頁，避免結果寫錯文件。

## 5. 連續掃卡

1. 切到「連續掃卡」，按「開始連續掃卡」。
2. 卡片放在天線上時只會建立一輪；移開再放回會建立新的一輪，即使 UID 相同。每輪是一個獨立文件。
3. 可勾「自動讀取可讀區塊」；沒有已驗證金鑰的扇區會保持未知。
4. 掃描結果可多選：命名、比較選取的兩張、匯出 EML、移除。恢復／寫入任務執行時掃描會自動暫停。

## 6. 備份庫

1. 切到「備份庫」，選擇或建立資料夾。
2. 把 `.bin`／`.dump`／`.eml`／`.txt`／`.json` 拖進視窗，或按「匯入檔案／資料夾」。內容以實際檔案的 SHA-256 辨識，重複項目會標記。
3. 可編輯別名、備註、標籤；用 UID／卡型／日期／完整度／重複篩選。
4. 「匯出…」可批次轉成 BIN／EML／JSON；有同名檔時會列出衝突，可覆寫、跳過或取消。來源檔不會被刪除。
5. 「在 workbench 開啟」與「比較選取的兩張」直接接到工作台。

## 7. 比較與寫入

- 比較頁每一側可選：目前文件、卡片快照（只含目前 session 讀到的區塊與已驗證金鑰）、檔案、備份庫。實卡側會標示 UID、時間與讀取範圍。
- 結果保留 `same`／`different`／`unknown`／`incomparable`；差異列附 byte 偏移。未知不等於相同。
- 寫入計畫：勾選要寫的 block；Block 0 與 sector trailer 需要明確勾選且通過 ACL 檢查。預覽會凍結來源與 RF session，換卡或重連後必須重新預覽。
- 寫入結果：`verified`（讀回相符）、`failed`、`written, unverified`、`mismatch`、`blocked`。只有確定沒寫入的列會被「Retry failed only」重選。

## 8. Ultralight／NTAG、Trace、診斷

- 「Ultralight / NTAG」：`Read card`（`hf mfu info`）、`Read pages`（`hf mfu dump`）、`Read NDEF`（`hf mfu ndefread`）；頁面、鎖定、NDEF 記錄與原始輸出分開顯示。未讀頁面顯示 `????????`。
- 「Trace」：抓取裝置或 client buffer 的 `trace list`，依方向／CRC／時間／文字篩選，匯出選取封包為 CSV 或 text2pcap 格式。
- 「Diagnostics」：COM／USB 身分、版本、capability、通道狀態、握手錯誤；可複製摘要或儲存報告。

## 9. 示範模式（免設備）

`Tools → Demonstration mode`。GUI 不開啟序列埠，所有命令由內建 Classic 卡與兩份有差異的 dump 回答；辨識、讀取、比較、批次恢復、寫入預覽都走正常工作台入口，結果標示為示範資料。關閉示範模式後回到真實裝置。

## 10. 實機驗收步驟

1. 接上 Proxmark3 Easy 256 KB，確認 `Diagnostics` 的 client／OS 都是 `v4.23346`。
2. 放一張已知金鑰的 Classic 1K：辨識 → 驗證 → 讀取 → 儲存 JSON；用 `sha256sum -c` 或 `Get-FileHash` 核對 sidecar。
3. 放一張缺金鑰的卡：跑 `Dictionary`，確認任務列逐項更新；中途按停止，確認已驗證金鑰保留，再按「只重試缺漏」。
4. 讀取途中換卡：確認結果留在原文件、新卡需要重新辨識，且不會把舊卡資料寫到新卡。
5. 連續掃卡：同一張卡停留不動只會有一輪；移開再放回會多一輪。
6. 寫入：先預覽，換卡後確認計畫失效；再對測試卡寫入一個資料 block，確認讀回狀態。
7. 拔插 USB、改用另一個 COM 埠：確認 GUI 以 USB 序號選回同一台裝置。
8. 在 1366×768 與 1920×1080、100%／125%／150% 縮放檢查主要按鈕、表格與繁中文字。
