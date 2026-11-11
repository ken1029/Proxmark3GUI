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
- **RF session**：每次連線、斷線與已知換卡都開啟新的 session。每個 key 記住「在哪個 session 通過認證」，每個 block 記住「哪個 session 讀到」。只有目前 session 的驗證結果可以用來讀寫目前這張卡；舊 session 的值仍留在文件裡當歷史，不會被清掉，也不會被當成本次證據。
- **卡片身分**：Identify 讀到 UID 後才把文件綁到這張卡。同容量但 UID 不同時，文件不會被改成新 UID；畫面標示「not the card in the field」，並提供「先存檔再開新文件」或「直接開新文件」。UID 只是核對資訊之一，不宣稱能區分所有卡（例如 UID 可寫的卡）。

### 2.1 來源文件與目前目標

| 狀態 | 意義 |
|---|---|
| 文件 UID 空白 | 還沒 Identify；不可驗證、讀取或寫入 |
| 文件 UID = 目前卡片 UID | 文件屬於場上的卡；仍只採目前 session 的驗證結果 |
| 文件 UID ≠ 目前卡片 UID | 文件是另一張卡的歷史；讀寫一律拒絕，除非開新文件 |

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

### 4.1 長任務語意（v0.3.1 修正）

v4.23346 client 在這些命令執行期間會輪詢 stdin：

| 方法 | client 來源 | 會讀 stdin |
|---|---|---|
| `hf mf chk` | `cmdhfmf.c` `CmdHF14AMfChk` → `kbd_enter_pressed()` | 是 |
| `hf mf autopwn` | `CmdHF14AMfAutoPWN` → `kbd_enter_pressed()` | 是 |
| `hf mf nested` | `CmdHF14AMfNested` → `kbd_enter_pressed()` | 是 |
| `hf mf darkside` | `mfc_read_tag()` → `kbd_enter_pressed()` | 是 |
| `hf mf sen` | `cmdhfmfsen.c` → `kbd_enter_pressed()` | 是 |
| `hf mf staticnested`／`hardnested`／`isen` | 目前沒有 `kbd_enter_pressed()` | 否（仍採同一協定） |

POSIX client 的 `kbd_enter_pressed()` 以 `getchar()` 排空 stdin，任何預先排入的換行都會讓命令提早結束；Windows client 用 `kbhit()/getch()` 讀主控台，pipe 內容不會被看到。為了不依賴平台差異，**所有恢復方法都在自己的單次 client 程序中執行**：

- 啟動方式：`proxmark3 <連線參數> -c "<命令>"`。完成邊界是**行程結束**，因此不會有任何命令被排在執行中的命令後面，也不會有 `rem` 被當成按鍵。
- 執行前會先停掉常駐 client，避免兩個程序同時開啟同一個序列埠；結束後保持斷線，由使用者重新連線。
- 取消（Stop waiting／Abort client）＝終止這個單次程序；序列埠由其結束釋放。
- 每個方法仍有硬上限（例如 autopwn／hardnested 60 分鐘）；逾時終止程序並回報失敗，不宣稱成功。
- 短命令（`rdbl`／`wrbl`／`hf 14a info`）仍走常駐 client；它們不輪詢 stdin，完成邊界是 `rem <token>` 或互動模式的最後提示。
- 卡片移走、client 結束、斷線、逾時與使用者取消各有獨立狀態；輸出依 session 歸屬，失敗任務的後續輸出不會寫進下一個任務。

### 4.1.1 輸出收集與「只印一把金鑰」（v0.3.3 修正）

- 一次性任務啟動時會先停掉常駐 client 以釋放序列埠；那個「client 結束」事件會走到 `Util::setRunningState(false)` → `releaseChannel()`。**釋放通道不得清掉進行中任務的輸出收集旗標**，否則 `commandFinished()` 收到空字串，所有已取得的金鑰都會被當成「沒有可用金鑰」。`finishAsync()` 仍會在任務結束時收尾。
- 不是每個方法都印表格：`hardnested` 只印 `Brute force completed. Key found: XXXXXXXXXXXX`，`darkside` 印 `Found valid key [ XXXXXXXXXXXX ]`，`nested`／`staticnested` 另有 `target block N key type X -- found valid key [ ... ]` 的行，`sen` 印 `Reader material recovered sec NNN key A = ...`／`Key sec NNN key X = ...`／`Found initial key: sector N ...`。這些都由 `MfcRecovery::parseOutput()` 解析。
- `hardnested`／`darkside` 的輸出不帶 sector，改由命令的目標（`--tblk`／`--blk`／`-b`）決定；沒有目標可歸屬時只回報警告，不會猜成 sector 0。block 會透過 trailer 表反查 sector，因此 4K 卡（32 個 4-block sector ＋ 8 個 16-block sector）不會錯位。

### 4.2 全域通道所有權

`Util::isChannelOwned()` 是唯一的「client 可能還在忙」狀態：

- 指令送出前就取得所有權；只有看到完成邊界（marker／提示）或 client 行程結束才釋放。
- **Stop waiting**、硬上限、提早出現的錯誤訊息只結束「UI 等待」，不動所有權；其他頁面（Raw、Classic、工作台）與新任務都必須等所有權釋放。
- 舊頁面的按鈕會因此被停用；即使被觸發，`Util` 也會拒絕送出（`EXEC_BUSY`），不會與執行中的命令交錯。

## 5. 比較

- 每個 block：`same`、`different`、`unknown`（至少一側未讀）、`incomparable`（較小容量沒有該 block）。
- 容量不同、UID 不同會另外列出；trailer 差異會標註「keys or access bits differ」。
- 「Only show differences」隱藏相同列；報告匯出包含摘要與所有列。
- 未知不等於相同，也不等於與 00 不同。

## 6. 受控寫入

「Write plan...」由 `Mfc::WritePlan` 決定一切（v0.3.1）：

1. 計畫是**快照**：開啟時凍結文件與目前 RF session；換卡或重新連線後計畫失效，必須重新 Identify。
2. 只有「完整 block」會進表；Block 0 與 sector trailer 需要對應的 Include 選項**且**通過政策檢查才可能被勾選。預覽、手動勾選、Retry 與真正送出命令都呼叫同一個 `blockAllowed()`／`selectedBlocks()`，因此不會有「選項沒勾但預覽把它勾起來」的路徑。
3. 「Read card & preview」以目前 session 已驗證的 key 讀取卡片值：
   - 一般資料區塊：same／different／unreadable／no key。
   - sector trailer：只比較讀得到的欄位（access bits，以及 ACL 允許讀出時的 Key B）。Key A 永遠不會被讀回，也不會被當成「相同」或「不同」。
   - 只有 `different` 且政策允許的列會自動勾選；`unreadable` 不會被當成安全可寫。
4. trailer 寫入前檢查卡片**目前**的 access conditions：
   - 文件中的 access bits 必須合法（互補位檢查），Key A／Key B 都必須已知，否則拒絕（避免把 sector 鎖死）。
   - 必須先預覽，才知道目前 ACL 允許用哪一把已驗證 key 寫入；不允許時直接阻擋並說明原因。
5. 寫入結果分為：`failed`（命令未回報成功）、`verified`（可驗證欄位全部相符）、`written, unverified`（命令回報成功但讀回不足）、`mismatch`（讀回不符）。trailer 以**新的** key 驗證：Key A 以「認證成功」證明，access bits 與可讀的 Key B 逐位比較；遮罩值不當成明文比對。
6. 「Retry failed only」只會重新勾選 `failed`（確定沒有寫入成功）的列；`written, unverified` 需要重新確認，不會自動重寫。
7. 對話框只有一個任務狀態：預覽／寫入／Retry 期間按鈕與表格凍結，關閉視窗等於要求停止；不會有兩輪預覽或寫入交錯，取消旗標也不會被另一輪重設。

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

新增（v0.3.1 複核修正）回歸：

- `mfcWritePlanProtection`：Block 0／trailer 的政策、預覽、手動勾選、retry 與失效判定。
- `workbenchWritePlanProtectsBlocks`：實際開對話框、按 Preview／手動勾選／按 Write，檢查送出的命令只有允許的 block。
- `workbenchRequiresCardIdentity`：同容量換卡、重連、歷史專案與新文件流程。
- `mfcSessionScopedEvidence`：key／block 的 session 歸屬與歷史保留。
- `mfcTrailerAclRules`：trailer ACL 解碼與各權限（Key B 可讀、Key A 由 B 寫入等）。
- `cancelKeepsChannelOwned`／`isolatedCommandUsesOneShotClient`：取消後仍封鎖通道、晚到 marker 才釋放；一次性 client 不排入任何 stdin。
- `workbenchBackupSidecarHashesFile`：JSON／EML／BIN 的 sidecar 與檔案實際 bytes 相符。
- `workbenchRunsDictionaryTask`：以 mock client 真正跑完一次 `-c "hf mf chk --1k"`。

fixture／mock 只能證明狀態機與解析，不代表實體卡恢復成功。Windows 實機與實體卡驗收仍需另外記錄。

## 8. 尚未完成／限制

- 未修改或重刷韌體；不支援已被裁掉的協定（Hitag、iCLASS、ISO15693、DESFire 模擬等）。
- 未取得 r3 在原生 Windows、不同 DPI 的實機截圖；視覺驗收仍以 Windows 1366×768、1920×1080、100%／125%／150% 為準。
- Hardnested 的離線檔案續算、nonce 檔管理與 trace Crypto1 解碼仍屬後續項目；本分支只包裝 client 已提供的命令與結果。
- 寫入計畫的「Retry failed only」只會勾選上一輪標記為 failed 的 block；`written, unverified` 與不可讀／未知仍需先解決金鑰或卡片問題。
- 恢復方法以單次 client 程序執行，代價是每次任務前後都必須重新連線（序列埠不會同時被兩個程序佔用）。
- `hardnested`／`staticnested`／`isen` 目前沒有輪詢 stdin，但仍採同一種一次性協定；若未來 client 改變行為，不需要再改 GUI。
- Key B 是否可讀採 client／韌體一致的判定（trailer AC 值 0、1、2 視為可由 Key A 讀出）；不同卡片的實際行為仍以實測為準。
- 4K 的 16-block sector 與 Mini 的 5-sector 對應已由單元測試覆蓋，但實體卡仍需驗證。
