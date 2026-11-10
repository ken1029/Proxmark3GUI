# MIFARE Classic 工作台（feature/mfc-workbench）

本文件說明 `feature/mfc-workbench` 新增的工作區、檔案格式與驗收方式。基準為 r3 GUI 0.2.10、Windows x64 client v4.23346 與目前 PM3 Easy 256 KB 韌體；本分支不修改、不重刷韌體。

## 1. 入口與頁面

主視窗新增「MFC workbench」浮動分頁（與 Classic、LF、T55xx 相同，可在底部拖曳／浮動），內含四個子頁：

| 子頁 | 內容 |
|---|---|
| Keys & access | 每個 sector 的 Key A／B 值、狀態、來源與是否可讀 |
| Data / backup | 區塊 hex、狀態、篩選、讀取缺漏、備份、受控寫入 |
| Compare | 離線載入 A／B 文件、逐 block 比較、匯出報告 |
| Key recovery / decryption | 方法建議、字典、autopwn、nested／staticnested／hardnested／darkside／SEN／ISEN、長任務狀態與取消 |

上方摘要顯示卡型、UID、nonce 類型、已驗證 key 數、已讀 block 數、未知／修改數、階段與經過時間。數值皆來自目前文件，不是寫死的示意值。

## 2. 文件模型與狀態

`Mfc::Document` 是唯一的資料來源，保存：

- 卡型：Mini（20 blocks）、1K（64）、2K（128）、4K（256）。
- 每個 block 的 32 個 hex 字元（允許 `?` 表示未讀）與狀態：`unknown`、`read`、`modified`、`verified`、`failed`。
- 每個 sector 的 Key A／B：值、狀態（`unknown`、`candidate`、`verified`、`failed`）、來源（手動／匯入／字典／恢復／卡片）與驗證時間。
- UID、來源檔名、建立時間與 SHA-256。

規則：

- `?` 不是 00。未知 block 不會被當成已讀，也不會在比較時算成相同或不同。
- 匯入失敗不修改目前文件；錯誤會指出大小或行號。
- 候選 key 不會因為字串是 12 個 hex 就變成已驗證；只有認證成功（讀取該 sector trailer）才標記 `verified`。

## 3. 檔案格式

### 3.1 資料 dump

| 格式 | 讀取 | 寫入 |
|---|---|---|
| `.bin` / `.dump` | 必須是 320／1024／2048／4096 bytes，依大小判定卡型 | 任何未讀 block 都會拒絕；除非使用者明確同意以 00 佔位（會警告） |
| `.eml` / `.txt` | 每個 block 一行 32 個 hex／`?`；接受 CRLF；行數必須是 20／64／128／256 | 未知 block 寫成 32 個 `?` |
| `.json` | `mfc-document` v1；另可讀官方 client 的 `FileType: mfc v2/v3` dump（`blocks` + `SectorKeys`） | 完整文件，適合備份與比較 |

JSON 備份旁會另寫 `<檔名>.sha256`。比較報告可輸出 Markdown 或 CSV。

### 3.2 金鑰檔

| 排列 | 內容 |
|---|---|
| RRG／官方 client `key.bin` | `A0 A1 … A15 B0 B1 … B15`（先全部 Key A，再全部 Key B） |
| Legacy GUI `key.bin` | `A0 B0 A1 B1 …` |
| Text | 每行 `sector:type:key`、`type key` 或 `keyA keyB` |
| JSON | 完整文件（含狀態與來源） |

兩種二進位排列大小相同，無法可靠自動判別。`auto` 會回報 ambiguous，GUI 會要求選擇 RRG 或 Legacy；不會默默猜測。官方 `key.bin` 的 `FFFFFFFFFFFF` 可能只是未知佔位，匯入後一律是「候選」，需驗證。

## 4. 金鑰恢復／解密流程

1. 按「Identify card」讀取 UID、ATQA、SAK 與卡型；必要時按「Nonce diagnostics」執行 `hf mf info`。
2. 載入已知 key 或既有 key.bin；按「Verify known keys」以 `hf mf rdbl` 認證每個候選 key。
3. 查看方法建議表：每一列說明「目前知道什麼、缺什麼前提、為何建議」。未知 nonce 時不會給出確定結論。
4. 選擇方法執行：
   - 字典：`hf mf chk --<card type> [-f <dict>] [--no-default]`
   - 自動：`hf mf autopwn --<card type> --ns`
   - Nested／Staticnested：需要至少一個已驗證 key，使用其 sector trailer。
   - Hardnested：需要已驗證 key 與目標 sector／type；已驗證的目標會拒絕執行。
   - Darkside：沒有已知 key、且非 static／static encrypted nonce 時才建議。
   - SEN：只用於 static encrypted nonce；ISEN 為唯讀診斷。
5. 任務完成後解析 key 表，依 sector／block 欄位合併；已驗證的 key 不會被空值或未驗證值覆蓋，衝突會記錄。
6. 「Read missing blocks」以已驗證 key 讀取未知區塊；完成後可備份、比較或進入寫入計畫。

### 4.1 長任務語意

- 不使用一般短命令的 15 秒無輸出 timeout；CPU 計算可以安靜數分鐘。
- 完成邊界是唯一的 `rem <token>` 回覆；這些恢復命令不會輪詢 stdin，因此不會把佇列中的 `rem` 當成按鍵。
- 每個方法有硬上限（例如 autopwn／hardnested 60 分鐘）；逾時只回報失敗，不宣稱成功。
- 同一時間只有一個需要設備的任務；恢復完成後才可讀取／寫入。
- 取消提供兩種語意：
  - **Stop waiting**：停止等待，保留已收到的 key／輸出；client 可能仍在執行，必須重新連線才能再操作設備。
  - **Abort client**：中斷並重建 client 連線；已完成的結果仍保留。
- 卡片移走、client 結束、斷線、逾時與使用者取消各有獨立狀態；不會把失敗任務的後續輸出寫進新任務。

## 5. 比較

- 每個 block：`same`、`different`、`unknown`（至少一側未讀）、`incomparable`（較小容量沒有該 block）。
- 容量不同、UID 不同會另外列出；trailer 差異會標註「keys or access bits differ」。
- 「Only show differences」隱藏相同列；報告匯出包含摘要與所有列。
- 未知不等於相同，也不等於與 00 不同。

## 6. 受控寫入

「Write plan...」：

1. 預設只勾選一般資料區塊；Block 0 與 sector trailer 必須由使用者明確勾選。
2. 「Read card & preview」以已驗證 key 讀取目前卡片值，標記 same／different／unreadable／no key；相同的 block 自動取消勾選。
3. 寫入前再次確認；每個 block 使用 `hf mf wrbl`，成功後讀回比對。
4. 讀回不同（且非 trailer 遮罩）即列為失敗；失敗清單會顯示 block 與原因。
5. 取消後保留已完成清單；本工作台不會在恢復流程中自動寫卡。

## 7. 建置與測試

Linux 端以 MXE 交叉編譯 Windows x64，Wine 執行 QtTest：

```bash
export MXE=$PWD/work/win/mxe TC=x86_64-w64-mingw32.shared
export PATH=/tmp/mingwshim:$MXE/usr/bin:$PATH   # MXE 的 as/ld shim
qmake /path/to/Proxmark3GUI/tests/tests.pro "SRCDIR_DIR=."
make -j"$(nproc)"
QT_QPA_PLATFORM=offscreen xvfb-run -a wine ./gui-tests.exe
```

新增測試涵蓋：

- Mini／1K／2K／4K 的 sector／block／trailer 對應。
- RRG 與 legacy key 排列、ambiguous 判定、文字 key 檔。
- BIN／EML／JSON round-trip、未知值、截斷與錯誤行。
- 比較的 same／different／unknown／incomparable、容量與 UID。
- key 表解析（亂序、重複、超出範圍）、nonce 判定、合併衝突與失敗候選。
- 方法建議與命令建構。
- 長任務無閒置 timeout、完成標記、busy 保護、取消保留輸出。
- 工作台頁面存在、離線比較、key 匯入。

fixture／mock 只能證明狀態機與解析，不代表實體卡恢復成功。Windows 實機與實體卡驗收仍需另外記錄。

## 8. 尚未完成／限制

- 未修改或重刷韌體；不支援已被裁掉的協定（Hitag、iCLASS、ISO15693、DESFire 模擬等）。
- 未取得 r3 在原生 Windows、不同 DPI 的實機截圖；視覺驗收仍以 Windows 1366×768、1920×1080、100%／125%／150% 為準。
- Hardnested 的離線檔案續算、nonce 檔管理與 trace Crypto1 解碼仍屬後續項目；本分支只包裝 client 已提供的命令與結果。
- 寫入計畫的「Retry failed only」只會勾選上一輪標記為 failed 的 block；不可讀／未知仍需先解決金鑰或卡片問題。
- 4K 的 16-block sector 與 Mini 的 5-sector 對應已由單元測試覆蓋，但實體卡仍需驗證。
