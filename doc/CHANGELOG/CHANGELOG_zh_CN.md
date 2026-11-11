# 更新日志

[English](../../CHANGELOG.md)  

### V0.3.3（feature/mfc-workbench，一次性程序輸出修正）
+ 釋放通道時不得停止收集進行中任務的輸出：一次性恢復任務會先停掉常駐 client 以釋放序列埠，而那個停止動作會清掉收集旗標，導致 `commandFinished()` 收到空字串，所有已取得的金鑰都被當成「方法結束，但沒有取得可用金鑰」
+ 讀得懂「只印一把金鑰」的方法：hardnested 的 `Brute force completed. Key found: XXXXXXXXXXXX`、darkside／nested 的 `Found valid key [ XXXXXXXXXXXX ]`，以及 sen 的 `Reader material recovered sec NNN key A = ...`、`Key sec NNN key X = ...`、`Found initial key: sector N ...`。hardnested／darkside 會把金鑰歸到命令的目標 sector／key type；沒有目標可歸屬時只回報警告，不會猜成 sector 0
+ mock client 改為輸出真實的 `[+] ` 表格前綴與 hardnested 結果，並以 `isolatedTaskSurvivesClientStop()`、`mfcRecoverySingleKeyOutput()`、`workbenchRunsHardnestedTask()` 固定這些不變式

### V0.3.2（feature/mfc-workbench，解析修正）
+ 真正讀得懂 client 的金鑰表：每一列都有 `[+] ` 前綴，樣式必須容許標籤而不是只允許空白；恢復完成後會回報已取得的金鑰，不再顯示「方法結束，但沒有取得可用金鑰」
+ 認得 `Static nonce....... 009080a2` 這種由 client 印出 nonce 值的靜態 nonce 行

### V0.3.0（feature/mfc-workbench）
+ 新增 MFC 工作台：以單一文件模型保存資料、金鑰、有效性、修改與恢復狀態
+ 修正 RRG `key.bin` 互通：匯出時先全部 Key A、再全部 Key B；舊 GUI 的 A0 B0 排列改為明確的匯入／匯出選項
+ 未讀取的區塊不得默默存成 00：raw bin 會拒絕不完整資料，EML 以 `?` 表示未知，JSON 保留狀態與 SHA-256
+ 匯入 BIN／EML／JSON／金鑰檔前先完整驗證；截斷、格式錯誤會回報行號或大小，且不破壞目前文件
+ 新增離線 A/B 比較：相同／不同／未知／無法比較、容量與 UID 說明、Markdown／CSV 報告
+ 每個 sector 的 Key A／B 具明確狀態（未知／候選／已驗證／失敗）與來源（手動／匯入／字典／恢復／卡片）
+ 新增方法建議（字典、autopwn、nested、staticnested、hardnested、darkside、sen、isen）與缺少前提說明
+ 依 sector／block 欄位解析 client key 表，不依列序；分段、重複、超出範圍的列不會把 key 填錯 sector
+ 長任務不使用閒置 timeout：以完成標記、硬上限、取消語意與單一設備任務所有權管理
+ 新增受控寫入計畫：預設只寫資料區塊，Block 0／trailer 需明確勾選，寫入前預覽、寫後讀回驗證與失敗清單
+ 恢復得到的已驗證金鑰直接接到讀取、備份與比較，不需要手動重抄

### V0.2.10
+ 標記模式只接受本次 `remark:` 回覆；分段送出的 `pm3 --> ` 命令回顯不再視為完成邊界（寫入、模擬器、trace 讀寫、握手）
+ 為真正會印尾端提示字元的 client 保留明確的互動提示字元模式
+ 連線 attempt 改由 GUI 端配置 generation；worker 尚未開始前排隊取消、或成功訊號已排入佇列後取消，都不會讓舊連線復活
+ 新增分段提示字元與開始前取消的確定性回歸測試

### V0.2.9
+ 管線模式下立即連線：啟動即送出 `hw version`，並以唯一的 `rem` 標記作為完成邊界（v4.23346 client 在管線模式沒有啟動提示字元，也沒有命令後空提示字元）
+ 寫入與 trace 讀寫加入完成標記，不再等待永遠不會出現的提示字元
+ 記錄連線各階段耗時（程序啟動、版本、握手）與按下連線到完成的總時間
+ 連線中斷線可立即取消握手；失敗時恢復介面；忽略重複點擊連線
+ 修正 QApplication 結束順序，並為 PM3 執行緒清理加上有界等待
+ capability manifest 移除 LF 時，一併停用實際顯示的 LF／T55xx 浮動分頁

### V0.2.8
+ 支持冰人版客户端 v4.16717  
+ 修复若干Bug  
+ 便于在不同版本客户端之间切换  
+ 支持蓝牙及TCP连接  

### V0.2.7
+ 修复使用冰人版v4.15864时无法写入块0的问题  
+ 默认关闭Linux/macOS系统下对PM3硬件断连的检测  
+ 修复配置文件中的小Bug  
+ 修复访问控制位解码器  
+ 在访问控制位解码器中显示更多细节  
+ 添加暗黑主题(源于https://github.com/ColinDuquesnoy/QDarkStyleSheet)  
+ 支持自定义主题，透明度，字体  
+ 修复若干翻译问题  

### V0.2.6
+ 支持冰人版客户端 v4.15864 [#37](https://github.com/wh201906/Proxmark3GUI/issues/37)  
+ 优化Mifare Classic卡写卡逻辑  
+ 修复lf config默认配置  
+ 添加客户端无法启动的提示  
+ 添加PM3硬件连接失败的提示  
+ 为PM3对应串口添加提示，并自动选中  
+ 修复原始指令框中有多余空行的问题  
+ 内嵌不同客户端的配置文件  
+ 去除从nested attack切换到staticnested attack的等待时间  

### V0.2.5
+ 修复 [#28](https://github.com/wh201906/Proxmark3GUI/issues/28)

### V0.2.4
+ 复制EM410x卡（一种常见的低频ID卡）  

### V0.2.3
+ 修复 [#27](https://github.com/wh201906/Proxmark3GUI/issues/27)
+ 尝试支持中文启动路径

### V0.2.2
+ 从外部文件加载客户端命令格式  
+ 修复 [#20](https://github.com/wh201906/Proxmark3GUI/issues/20), [#21](https://github.com/wh201906/Proxmark3GUI/issues/21), [#22](https://github.com/wh201906/Proxmark3GUI/issues/22)  
+ 兼容冰人版客户端 v4.13441

### V0.2.1
+ 优化MIFARE Classic读卡逻辑
+ 修复 [#16](https://github.com/wh201906/Proxmark3GUI/issues/16) （配合新版RRG固件时无法读取扇区数据）
+ 修复 [#15](https://github.com/wh201906/Proxmark3GUI/issues/15) （路径中支持空格）

### V0.2
+ 使用浮动窗口，界面配置更加灵活  
+ 支持部分低频命令  
+ 修复原始命令选项卡中的一个Bug  

### V0.1.4
+ 优化性能  
+ 优化用户界面  
+ 自动搜索可用端口  
+ 支持高分屏  
+ 可通过外部脚本配置环境变量  
(在客户端需要配置环境变量时很有用)  
+ 全功能兼容冰人版（在v4.9237上测试通过）  
+ 支持指定客户端工作路径
+ 修复部分bug

### V0.1.3
+ 修复访问控制位解码器
+ 写多个块时显示写入结果

### V0.1.2
+ 优化读卡逻辑
+ UI自定义
+ 自动保存客户端路径
+ 添加访问控制位解码器（也可用于自己构造访问控制位）（有bug，请使用V0.1.3或更高版本）
+ 支持仅读写选中块
+ 支持部分冰人功能
+ 修复部分bug

### V0.1.1
+ 完成整个Mifare模块（支持模拟卡和嗅探功能）

### V0.1
+ 支持处理Mifare卡片及相关数据文件

### V0.0.1
+ 一个带串口选择框的实验版本