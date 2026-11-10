<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_TW">
<context>
    <name>MF_Attack_hardnestedDialog</name>
    <message>
        <location filename="../src/ui/mf_attack_hardnesteddialog.ui" line="14"/>
        <source>Hardnested Attack</source>
        <translation>Hardnested攻擊</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_attack_hardnesteddialog.ui" line="20"/>
        <source>Known Block:</source>
        <translation>已知塊：</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_attack_hardnesteddialog.ui" line="29"/>
        <location filename="../src/ui/mf_attack_hardnesteddialog.ui" line="104"/>
        <source>Block:</source>
        <translation>塊：</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_attack_hardnesteddialog.ui" line="68"/>
        <location filename="../src/ui/mf_attack_hardnesteddialog.ui" line="143"/>
        <source>A</source>
        <translation></translation>
    </message>
    <message>
        <location filename="../src/ui/mf_attack_hardnesteddialog.ui" line="73"/>
        <location filename="../src/ui/mf_attack_hardnesteddialog.ui" line="148"/>
        <source>B</source>
        <translation></translation>
    </message>
    <message>
        <location filename="../src/ui/mf_attack_hardnesteddialog.ui" line="95"/>
        <source>Target Block:</source>
        <translation>目標塊：</translation>
    </message>
</context>
<context>
    <name>MF_Sim_simDialog</name>
    <message>
        <location filename="../src/ui/mf_sim_simdialog.ui" line="14"/>
        <source>Simulate</source>
        <translation>模擬</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_sim_simdialog.ui" line="67"/>
        <source>UID 4 or 7 bytes. If not specified, the UID 4B from emulator memory will be used</source>
        <translation>4或7位元組的UID，如果不指定，則使用模擬器記憶體中的4位元組UID</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_sim_simdialog.ui" line="134"/>
        <source>Provide explicit ATQA (2 bytes)</source>
        <translation>指定ATQA（2個位元組）</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_sim_simdialog.ui" line="201"/>
        <source>Provide explicit SAK (1 byte)</source>
        <translation>指定SAK（1個位元組）</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_sim_simdialog.ui" line="268"/>
        <source>Automatically exit simulation after &lt;numreads&gt; blocks have been read by reader. 0 = infinite</source>
        <translation>在讀卡器讀取&lt;n&gt;個塊後自動退出模擬，n為0或不指定時永遠不退出</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_sim_simdialog.ui" line="319"/>
        <source>Interactive, means that console will not be returned until simulation finishes or is aborted</source>
        <translation>互動模式，勾選後PM3客戶端將在模擬完成或者模擬中斷後才可繼續使用</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_sim_simdialog.ui" line="370"/>
        <source>Crack, performs the &apos;reader attack&apos;, nr/ar attack against a legitimate reader, fishes out the key(s)</source>
        <translation>破解，對讀卡器進行攻擊，透過nr/ar攻擊來釣出密碼(無卡嗅探)</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_sim_simdialog.ui" line="421"/>
        <source>set keys found from &apos;reader attack&apos; to emulator memory (implies -x and i)</source>
        <translation>在獲得密碼後自動將密碼寫入模擬器記憶體（自動勾選-x和i）</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_sim_simdialog.ui" line="590"/>
        <source>verbose output</source>
        <translation>更多輸出內容</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_sim_simdialog.ui" line="488"/>
        <source>get UIDs to use for &apos;reader attack&apos; from file &apos;f &lt;filename.txt&gt;&apos; (implies x and i)</source>
        <translation>從&lt;filename.txt&gt;當中獲取用於破解讀卡器的UID（批次模擬）（自動勾選x和i）</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_sim_simdialog.ui" line="539"/>
        <source>Generate random nonces instead of sequential nonces. Standard reader attack won&apos;t work with this option, only moebius attack works</source>
        <translation>生成隨機nonce而不是順序的nonce，這種情況下PM3將不對讀卡器進行標準攻擊，只進行moebius攻擊</translation>
    </message>
</context>
<context>
    <name>MF_UID_parameterDialog</name>
    <message>
        <location filename="../src/ui/mf_uid_parameterdialog.ui" line="14"/>
        <source>Set Parameter</source>
        <translation>設定卡引數</translation>
    </message>
</context>
<context>
    <name>MF_trailerDecoderDialog</name>
    <message>
        <location filename="../src/ui/mf_trailerdecoderdialog.ui" line="14"/>
        <source>Trailer Decoder</source>
        <translation>Trailer解碼</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_trailerdecoderdialog.ui" line="22"/>
        <source>Blocks</source>
        <translation>塊大小</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_trailerdecoderdialog.ui" line="67"/>
        <source>Trailer Data:
(like &quot;FF0780&quot; or &quot;FF 07 80&quot;)</source>
        <translation>輸入控制位資料
（形如“FF0780”或“FF 07 80”）</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_trailerdecoderdialog.ui" line="120"/>
        <source>Or set bits manually</source>
        <translation>手動設定訪問情況：</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_trailerdecoderdialog.ui" line="323"/>
        <source>Data Block Permission:</source>
        <translation>資料塊訪問許可權：</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_trailerdecoderdialog.ui" line="340"/>
        <source>Block0</source>
        <translation>塊0</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_trailerdecoderdialog.ui" line="345"/>
        <source>Block1</source>
        <translation>塊1</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_trailerdecoderdialog.ui" line="350"/>
        <source>Block2</source>
        <translation>塊2</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_trailerdecoderdialog.ui" line="355"/>
        <location filename="../src/ui/mf_trailerdecoderdialog.ui" line="395"/>
        <source>Read</source>
        <translation>讀</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_trailerdecoderdialog.ui" line="360"/>
        <location filename="../src/ui/mf_trailerdecoderdialog.ui" line="400"/>
        <source>Write</source>
        <translation>寫</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_trailerdecoderdialog.ui" line="365"/>
        <source>Increase</source>
        <translation>增加</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_trailerdecoderdialog.ui" line="370"/>
        <source>Decrease/Transfer/Restore</source>
        <translation>減少/從緩衝區寫入/讀入至緩衝區</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_trailerdecoderdialog.ui" line="378"/>
        <source>Trailer Block Permission:</source>
        <translation>Trailer訪問許可權：</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_trailerdecoderdialog.ui" line="405"/>
        <location filename="../src/ui/mf_trailerdecoderdialog.cpp" line="130"/>
        <source>KeyA</source>
        <translation>金鑰A</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_trailerdecoderdialog.ui" line="410"/>
        <source>Access Bits</source>
        <translation>控制位</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_trailerdecoderdialog.ui" line="415"/>
        <location filename="../src/ui/mf_trailerdecoderdialog.cpp" line="134"/>
        <source>KeyB</source>
        <translation>金鑰B</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_trailerdecoderdialog.ui" line="425"/>
        <source>Reference:
MF1S70YYX_V1 Product data sheet
Rev. 3.2 — 23 November 2017</source>
        <translation>參考資料：
MF1S70YYX_V1 Product data sheet
Rev. 3.2 — 23 November 2017</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_trailerdecoderdialog.ui" line="446"/>
        <source>Note:the Access Bits usually contains 4 bytes(8 hex symbols), but only the first 3 bytes matters. You can set the 4th byte randomly.</source>
        <translation>注意：Access Bits一般包含4個位元組（8個16進位制字元），但只有前3個位元組決定訪問情況，最後一個位元組可任意設定。</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_trailerdecoderdialog.cpp" line="45"/>
        <source>Invalid!
It could make the whole sector blocked irreversibly!</source>
        <translation>無效！
可能導致整個扇區被不可逆轉地鎖定！</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_trailerdecoderdialog.cpp" line="83"/>
        <location filename="../src/ui/mf_trailerdecoderdialog.cpp" line="88"/>
        <source>Valid</source>
        <translation>有效</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_trailerdecoderdialog.cpp" line="83"/>
        <source>KeyB cannot serve for authentication</source>
        <translation>金鑰B無法用於認證</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_trailerdecoderdialog.cpp" line="105"/>
        <location filename="../src/ui/mf_trailerdecoderdialog.cpp" line="106"/>
        <location filename="../src/ui/mf_trailerdecoderdialog.cpp" line="107"/>
        <location filename="../src/ui/mf_trailerdecoderdialog.cpp" line="111"/>
        <location filename="../src/ui/mf_trailerdecoderdialog.cpp" line="112"/>
        <location filename="../src/ui/mf_trailerdecoderdialog.cpp" line="113"/>
        <source>Block</source>
        <translation>塊</translation>
    </message>
    <message>
        <location filename="../src/ui/mf_trailerdecoderdialog.cpp" line="138"/>
        <source>KeyA+B</source>
        <translation>金鑰A+B</translation>
    </message>
</context>
<context>
    <name>MainWindow</name>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="20"/>
        <source>Proxmark3GUI</source>
        <translation></translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="124"/>
        <source>Connect</source>
        <translation>連線</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="137"/>
        <source>Disconnect</source>
        <translation>斷開</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="156"/>
        <source>Mifare</source>
        <translation>Mifare(IC)卡</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="259"/>
        <source>Select Trailer</source>
        <translation>選中密碼塊</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="402"/>
        <source>Card Type</source>
        <translation>卡片型別</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="423"/>
        <source>MINI</source>
        <extracomment>320</extracomment>
        <translation></translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="436"/>
        <source>1K</source>
        <extracomment>1024</extracomment>
        <translation></translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="452"/>
        <source>2K</source>
        <extracomment>2048</extracomment>
        <translation></translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="465"/>
        <source>4K</source>
        <extracomment>4096</extracomment>
        <translation></translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="475"/>
        <source>File</source>
        <translation>檔案</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="496"/>
        <location filename="../src/ui/mainwindow.ui" line="1202"/>
        <source>Load</source>
        <translation>載入</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="509"/>
        <location filename="../src/ui/mainwindow.ui" line="1215"/>
        <source>Save</source>
        <translation>儲存</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="226"/>
        <location filename="../src/ui/mainwindow.ui" line="529"/>
        <source>Data</source>
        <translation>資料</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="539"/>
        <source>Key</source>
        <translation>金鑰</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="549"/>
        <source>Attack</source>
        <translation>破解</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="583"/>
        <source>Card Info</source>
        <translation>讀卡資訊</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="596"/>
        <source>Check Default</source>
        <translation>驗證預設密碼</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="609"/>
        <source>Nested</source>
        <translation>Nested攻擊</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="622"/>
        <source>Hardnested</source>
        <translation>Hardested攻擊</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="635"/>
        <source>Darkside</source>
        <translation>Darkside攻擊</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="660"/>
        <source>Read/Write</source>
        <translation>讀/寫</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="673"/>
        <source>Block:</source>
        <translation>塊：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="705"/>
        <source>Key:</source>
        <translation>金鑰：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="724"/>
        <source>Key Type:</source>
        <translation>金鑰型別：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1189"/>
        <source>List Data</source>
        <translation>列出嗅探資料</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="747"/>
        <location filename="../src/ui/mainwindow.ui" line="2403"/>
        <source>Data:</source>
        <translation>資料：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="769"/>
        <source>Normal(Require Password)</source>
        <translation>普通卡（需要密碼）</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="851"/>
        <source>Dump</source>
        <translation>Dump命令</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="870"/>
        <source>Restore</source>
        <translation>Restore命令</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="880"/>
        <source>Chinese Magic Card(Without Password)</source>
        <translation>UID卡（不需要密碼）</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="901"/>
        <source>Lock UFUID Card</source>
        <translation>鎖定UFUID卡</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="914"/>
        <location filename="../src/ui/mainwindow.cpp" line="916"/>
        <source>About UID Card</source>
        <translation>關於UID卡</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="985"/>
        <source>Set Parameter</source>
        <translation>設定卡引數</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1004"/>
        <source>Wipe</source>
        <translation>擦除</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1030"/>
        <location filename="../src/ui/mainwindow.ui" line="1106"/>
        <source>Simulate</source>
        <translation>模擬</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="522"/>
        <location filename="../src/ui/mainwindow.ui" line="1093"/>
        <source>Clear</source>
        <translation>清空</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="56"/>
        <source>Client Path:</source>
        <translation>客戶端路徑：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="82"/>
        <source>Port:</source>
        <translation>埠：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="111"/>
        <source>Refresh Ports</source>
        <translation>重新整理埠</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="252"/>
        <source>Select All</source>
        <translation>全選</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="288"/>
        <source>KeyBlocks-&gt;Key</source>
        <translation>密碼區-&gt;密碼</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="301"/>
        <source>KeyBlocks&lt;-Key</source>
        <translation>密碼區&lt;-密碼</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="314"/>
        <source>Fill Keys</source>
        <translation>填充密碼</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="327"/>
        <source>Trailer Decoder</source>
        <translation>Trailer解碼</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="793"/>
        <location filename="../src/ui/mainwindow.ui" line="927"/>
        <source>Read One</source>
        <translation>讀取單個塊</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="806"/>
        <location filename="../src/ui/mainwindow.ui" line="940"/>
        <source>Write One</source>
        <translation>寫入單個塊</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="819"/>
        <location filename="../src/ui/mainwindow.ui" line="953"/>
        <location filename="../src/ui/mainwindow.ui" line="1080"/>
        <source>Read Selected</source>
        <translation>讀取選中塊</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="832"/>
        <location filename="../src/ui/mainwindow.ui" line="966"/>
        <location filename="../src/ui/mainwindow.ui" line="1067"/>
        <source>Write Selected</source>
        <translation>寫入選中塊</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1129"/>
        <location filename="../src/ui/mainwindow.ui" line="1163"/>
        <location filename="../src/ui/mainwindow.ui" line="1800"/>
        <source>Sniff</source>
        <translation>嗅探</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1176"/>
        <source>Sniff(14a)</source>
        <translation>嗅探(14a)</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1252"/>
        <source>LF Config</source>
        <translation>低頻配置</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1273"/>
        <source>Frequency</source>
        <translation>頻率</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1396"/>
        <source>You might need a modified LF antenna if the freq is not 125k/134k.
When setting the freq, the &quot;hw setlfdivisor&quot; will also be called.</source>
        <translation>如果頻率不為125k/134k，則原裝天線可能不適用。
設定頻率後GUI會使用&quot;hw setlfdivisor&quot;改變底層分頻係數。</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1427"/>
        <source>Bits per sample:</source>
        <translation>取樣精度(Bits per sample)：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1434"/>
        <source>Decimation:</source>
        <translation>抽取(Decimation)：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1451"/>
        <source>Averaging:</source>
        <translation>平均化(Averaging)：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1576"/>
        <source>Reset</source>
        <translation>重置</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1601"/>
        <source>LF Operation</source>
        <translation>低頻操作</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1626"/>
        <source>Search</source>
        <translation>搜尋</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1657"/>
        <source>Read and search for valid known tag.</source>
        <translation>讀卡並尋找已知型別的卡</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1683"/>
        <source>Read</source>
        <translation>讀取</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1714"/>
        <source>Sniff low frequency signal with LF field ON.
Use this to get raw data from a tag.</source>
        <translation>啟用低頻電磁場並讀取原始訊號。
該功能用於獲取卡片原始資料。</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1741"/>
        <source>Tune</source>
        <translation>調諧</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1772"/>
        <source>Measure LF antenna tuning.
If the antenna voltage has a obvious drop after putting card on the antenna, it is likely that the tag is a LF tag.
On Iceman/RRG repo, press the button on PM3 to stop measuring</source>
        <translation>測量低頻天線諧振頻率。
如果天線電壓在放置卡片後明顯下降，則該卡片很可能是低頻卡。
在冰人版韌體下，如果需要停止測量，請按下PM3側面的按鈕</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1831"/>
        <source>Sniff low frequency signal with LF field OFF.
Use this to get raw data from a reader 
or the communication between a tag and a reader.</source>
        <translation>直接讀取低頻訊號。
該功能用於獲取讀卡器的資料
或者卡片與讀卡器的互動過程。</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1883"/>
        <location filename="../src/ui/mainwindow.cpp" line="48"/>
        <source>T55xx</source>
        <translation></translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1895"/>
        <source>Basic Configuration(Page 0 Block 0)</source>
        <translation>基本配置區(頁0 塊0)</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1909"/>
        <location filename="../src/ui/mainwindow.ui" line="2185"/>
        <source>Hex:</source>
        <translation>16進位制：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1916"/>
        <location filename="../src/ui/mainwindow.ui" line="2192"/>
        <source>Bin:</source>
        <translation>2進位制：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1960"/>
        <location filename="../src/ui/mainwindow.ui" line="2236"/>
        <source>Get from Data</source>
        <translation>從資料區匯入</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1967"/>
        <location filename="../src/ui/mainwindow.ui" line="2243"/>
        <source>Set to Data</source>
        <translation>匯出到資料區</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1997"/>
        <location filename="../src/ui/mainwindow.ui" line="2276"/>
        <source>Locked:</source>
        <translation>鎖定(Locked)：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2007"/>
        <source>Master Key:</source>
        <translation>主金鑰：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2014"/>
        <source>Data Bit Rate:</source>
        <translation>資料位元率(Data Bit Rate)：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2024"/>
        <source>eXtended Mode:</source>
        <translation>擴充套件模式(eXtended Mode)：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2031"/>
        <location filename="../src/ui/mainwindow.ui" line="2430"/>
        <source>Modulation:</source>
        <translation>調製方式(Modulation)：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2038"/>
        <source>PSK Clock Freq:</source>
        <translation>PSK 時脈頻率：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2045"/>
        <source>Answer on Request:</source>
        <translation>應答請求：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2058"/>
        <source>One Time Pad:</source>
        <translation>一次性密碼本：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2065"/>
        <source>Max Block:</source>
        <translation>最大區塊：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2072"/>
        <source>Password:</source>
        <translation>密碼(Password):</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2079"/>
        <source>Seq. Terminator:</source>
        <translation>序列終止符：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2086"/>
        <source>Seq. Start Marker:</source>
        <translation>序列起始標記：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2093"/>
        <source>Fast Downlink:</source>
        <translation>快速下行：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2100"/>
        <source>Inverse Data:</source>
        <translation>反相資料：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2107"/>
        <source>Init-Delay:</source>
        <translation>初始化延遲：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2171"/>
        <source>Analog Front-End Option(Page 1 Block 3)</source>
        <translation>模擬前端選項區(頁1 塊3)</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2286"/>
        <source>Option Key:</source>
        <translation>選項金鑰：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2296"/>
        <source>Soft Modulation:</source>
        <translation>軟調變：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2306"/>
        <source>Clamp Voltage:</source>
        <translation>箝位電壓：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2316"/>
        <source>Modulation Voltage:</source>
        <translation>調變電壓：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2326"/>
        <source>Clock Detection Threshold:</source>
        <translation>時脈偵測門檻：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2336"/>
        <source>Gap Detection Threshold:</source>
        <translation>間隙偵測門檻：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2346"/>
        <source>Write Dampling:</source>
        <translation>寫入阻尼：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2353"/>
        <source>Demod Delay:</source>
        <translation>解調延遲：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2360"/>
        <source>Downlink Protocol:</source>
        <translation>下行協定：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2422"/>
        <source>T55xx Read Config</source>
        <translation>T55xx讀卡配置區</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2437"/>
        <source>Bit Rate:</source>
        <translation>位元率(Bit Rate)：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2444"/>
        <source>Seq. Term.</source>
        <translation>序列終止</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2451"/>
        <source>Offset:</source>
        <translation>偏移：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2458"/>
        <source>Inverted:</source>
        <translation>反相</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2492"/>
        <source>T5577</source>
        <translation></translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2499"/>
        <source>T5555</source>
        <translation></translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2523"/>
        <source>RawCommand</source>
        <translation>原始命令</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2570"/>
        <location filename="../src/ui/mainwindow.cpp" line="360"/>
        <source>History:</source>
        <translation>命令歷史：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2602"/>
        <source>ClearHistory</source>
        <translation>清空歷史</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2625"/>
        <source>Send</source>
        <translation>傳送</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2632"/>
        <source>ClearOutput</source>
        <translation>清空輸出</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2642"/>
        <source>Settings</source>
        <translation>設定</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2689"/>
        <source>Client</source>
        <translation>客戶端</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2695"/>
        <source>GUI working directory:</source>
        <translation>GUI工作路徑：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2730"/>
        <source>If the client requires some enviroment variables, you can make a script file(*.bat on Windows or *.sh on Linux) to configure them, then put the path of the script there.
The &quot;&lt;client dir&gt;&quot; will be replaced by the directory of the &quot;Client Path&quot;</source>
        <translation>如果客戶端需要配置環境變數才能正常執行，可以將配置環境變數所需的指令碼檔案(Windows系統內為*.bat，linux系統內為*.sh)路徑填入此處。
當中的&lt;client dir&gt;會被替換為“客戶端路徑”所對應的父目錄</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2755"/>
        <source>../data</source>
        <translation></translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2786"/>
        <source>&lt;port&gt; -f</source>
        <translation></translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2810"/>
        <source>Config file(Reconnect to apply):</source>
        <translation>配置檔案(重連後生效)：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2953"/>
        <source>UI</source>
        <translation>介面</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2961"/>
        <source>Language: </source>
        <translation>語言： </translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2968"/>
        <source>Choose Language</source>
        <translation>選擇語言</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2975"/>
        <source>(Restart this app to use new language)</source>
        <translation>（重啟此程式以使用新語言）</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="3012"/>
        <source>Opacity:</source>
        <translation>透明度：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="3056"/>
        <source>Theme:</source>
        <translation>主題：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="3073"/>
        <location filename="../src/ui/mainwindow.ui" line="3114"/>
        <location filename="../src/ui/mainwindow.ui" line="3148"/>
        <location filename="../src/ui/mainwindow.ui" line="3182"/>
        <source>Set</source>
        <translation>設定</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="3080"/>
        <source>(Restart this app to use new theme)</source>
        <translation>（重啟此程式以使用新主題）</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="3091"/>
        <source>Global Font:</source>
        <translation>全域性字型：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="3125"/>
        <source>Data Font:</source>
        <translation>資料字型：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="3159"/>
        <source>Command Font:</source>
        <translation>命令字型：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="3196"/>
        <source>Latest Release:</source>
        <translation>檢查更新：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="3226"/>
        <source>Report Bugs:</source>
        <translation>反饋問題：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2892"/>
        <source>Keep buttons enabled even the client is running or disconnected</source>
        <translation>保持所有按鈕可點選，即使未連線客戶端或有任務正在執行</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1244"/>
        <source>LF</source>
        <translation>低頻</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1331"/>
        <source>other</source>
        <translation>其它</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1338"/>
        <source>Divisor:</source>
        <translation>分頻係數(Divisor)：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1358"/>
        <source>Actural Freq: 125.000kHz</source>
        <translation>實際頻率： 125.000kHz</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1465"/>
        <source>Trigger threshold:</source>
        <translation>觸發閾值(Trigger threshold)：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1479"/>
        <source>Samples to skip:</source>
        <translation>跳過前n個取樣(Samples to skip)：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1550"/>
        <source>Get Config</source>
        <translation>獲取當前配置</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="1569"/>
        <source>Set Config</source>
        <translation>改變當前配置</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2716"/>
        <source>Preload script path(Reconnect to apply):</source>
        <translation>預載入指令碼路徑(重連後生效)：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2748"/>
        <source>Client working directory(Reconnect to apply):</source>
        <translation>客戶端工作路徑(重連後生效)：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2762"/>
        <source>On Windows, the client working directory should not be identical to the path of GUI, otherwise the client will use the wrong .dll file.</source>
        <translation>在Windows系統中，客戶端工作路徑與GUI程式所在路徑不能相同，否則客戶端會使用錯誤的.dll檔案。</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2779"/>
        <source>Start arguments(Reconnect to apply):</source>
        <translation>啟動引數(重連後生效)：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2793"/>
        <source>-f is necessary because the GUI need to handle the output in time. In some cases, the arguments should be set to &quot;-p /dev/&lt;port&gt; -f&quot; or &quot;-p &lt;port&gt; -f&quot;.</source>
        <translation>-f選項用於使客戶端實時返回命令回顯，必須新增。部分情況下啟動引數需設定為&quot;-p /dev/&lt;port&gt; -f&quot;或&quot;-p &lt;port&gt; -f&quot;。</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2841"/>
        <source>config.json</source>
        <translation></translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2848"/>
        <source>Different clients require different config files. You can change the content of config file if the command format changes.</source>
        <translation>不同客戶端需要使用不同的配置檔案。若命令格式發生改變，你可以嘗試手動修改配置檔案以適配。</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="2938"/>
        <source>Keep the client active even the PM3 hardware is disconnected.(Experimental)</source>
        <translation>在PM3斷開後保持客戶端執行（實驗性功能）</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="91"/>
        <location filename="../src/ui/mainwindow.cpp" line="198"/>
        <location filename="../src/ui/mainwindow.cpp" line="205"/>
        <location filename="../src/ui/mainwindow.cpp" line="277"/>
        <location filename="../src/ui/mainwindow.cpp" line="282"/>
        <location filename="../src/ui/mainwindow.cpp" line="463"/>
        <location filename="../src/ui/mainwindow.cpp" line="677"/>
        <location filename="../src/ui/mainwindow.cpp" line="697"/>
        <location filename="../src/ui/mainwindow.cpp" line="710"/>
        <location filename="../src/ui/mainwindow.cpp" line="729"/>
        <location filename="../src/ui/mainwindow.cpp" line="742"/>
        <location filename="../src/ui/mainwindow.cpp" line="769"/>
        <location filename="../src/ui/mainwindow.cpp" line="782"/>
        <location filename="../src/ui/mainwindow.cpp" line="987"/>
        <location filename="../src/ui/mainwindow.cpp" line="1028"/>
        <source>Info</source>
        <translation>資訊</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="205"/>
        <source>Plz choose a port first</source>
        <translation>請先選擇埠</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="293"/>
        <source>Connected</source>
        <translation>已連線</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="299"/>
        <location filename="../src/ui/mainwindow.cpp" line="1113"/>
        <source>Not Connected</source>
        <translation>未連線</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="729"/>
        <location filename="../src/ui/mainwindow.cpp" line="742"/>
        <location filename="../src/ui/mainwindow.cpp" line="987"/>
        <source>Failed to open</source>
        <translation>無法開啟</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="463"/>
        <source>Continue?</source>
        <translation>確定？</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="12"/>
        <source>Dock all windows</source>
        <translation>吸附所有懸浮視窗</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="14"/>
        <source>Ver: </source>
        <translation>版本： </translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="15"/>
        <source>Check Update</source>
        <translation>檢查更新</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="91"/>
        <source>Failed to load config file</source>
        <translation>無法開啟配置檔案</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="277"/>
        <source>Failed to start the client</source>
        <translation>無法啟動客戶端</translation>
    </message>
    <message>
        <source>The client process did not start: %1</source>
        <translation>客戶端程序無法啟動：%1</translation>
    </message>
    <message>
        <source>Connection cancelled</source>
        <translation>連線已取消</translation>
    </message>
    <message>
        <source>The client exited during the handshake</source>
        <translation>客戶端在握手期間結束</translation>
    </message>
    <message>
        <source>Timed out waiting for the client version</source>
        <translation>等待客戶端版本逾時</translation>
    </message>
    <message>
        <source>The client reported no firmware version (offline?)</source>
        <translation>客戶端未回報韌體版本（離線？）</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="282"/>
        <source>Failed to connect to the hardware</source>
        <translation>無法連線到PM3硬體</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="463"/>
        <source>Some of the data and key will be cleared.</source>
        <translation>部分資料和密碼將被清除</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="677"/>
        <source>Data must consists of 32 Hex symbols(Whitespace is allowed)</source>
        <translation>資料必須由32個十六進位制字元組成（中間可含有空格）</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="697"/>
        <location filename="../src/ui/mainwindow.cpp" line="710"/>
        <source>Key must consists of 12 Hex symbols(Whitespace is allowed)</source>
        <translation>金鑰必須由12個十六進位制字元組成（中間可含有空格）</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="722"/>
        <source>Plz select the data file:</source>
        <translation>請選擇資料檔案：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="723"/>
        <location filename="../src/ui/mainwindow.cpp" line="763"/>
        <location filename="../src/ui/mainwindow.cpp" line="767"/>
        <source>Binary Data Files(*.bin *.dump)</source>
        <translation>二進位制資料檔案(*.bin *.dump)</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="723"/>
        <location filename="../src/ui/mainwindow.cpp" line="736"/>
        <location filename="../src/ui/mainwindow.cpp" line="974"/>
        <source>All Files(*.*)</source>
        <translation>所有檔案(*.*)</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="735"/>
        <source>Plz select the key file:</source>
        <translation>請選擇金鑰檔案：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="762"/>
        <source>Plz select the location to save data file:</source>
        <translation>請選擇資料檔案儲存的位置：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="769"/>
        <location filename="../src/ui/mainwindow.cpp" line="782"/>
        <location filename="../src/ui/mainwindow.cpp" line="1028"/>
        <source>Failed to save to</source>
        <translation>無法儲存至</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="775"/>
        <source>Plz select the location to save key file:</source>
        <translation>請選擇金鑰檔案儲存的位置：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="736"/>
        <location filename="../src/ui/mainwindow.cpp" line="776"/>
        <location filename="../src/ui/mainwindow.cpp" line="780"/>
        <source>Binary Key Files(*.bin *.dump)</source>
        <translation>二進位制密碼檔案(*.bin *.dump)</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="198"/>
        <source>The client path is invalid</source>
        <translation>客戶端路徑無效</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="723"/>
        <location filename="../src/ui/mainwindow.cpp" line="763"/>
        <source>Text Data Files(*.txt *.eml)</source>
        <translation>文字資料檔案(*.txt *.eml)</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="901"/>
        <source>    Normally, the Block 0 of a typical Mifare card, which contains the UID, is locked during the manufacture. Users cannot write anything to Block 0 or set a new UID to a normal Mifare card.</source>
        <translation>    普通Mifare卡的塊0無法寫入，卡號也不能更改</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="902"/>
        <source>    Chinese Magic Cards(aka UID Cards) are some special cards whose Block 0 are writeable. And you can change UID by writing to it.</source>
        <translation>    UID卡（在國外叫Chinese Magic Card）的塊0可寫，卡號可變。</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="904"/>
        <source>There are two versions of Chinese Magic Cards, the Gen1 and the Gen2.</source>
        <translation>國外把UID卡分為Chinese Magic Card Gen1和Gen2</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="905"/>
        <source>    Gen1:</source>
        <translation></translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="905"/>
        <source>    also called UID card in China. It responses to some backdoor commands so you can access any blocks without password. The Proxmark3 has a bunch of related commands(csetblk, cgetblk, ...) to deal with this type of card, and my GUI also support these commands.</source>
        <translation>    指通常所說的UID卡，可以通過後門指令直接讀寫塊而無需密碼，在PM3和此GUI中有特殊命令處理這類卡片</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="906"/>
        <source>    Gen2:</source>
        <translation></translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="906"/>
        <source>    doesn&apos;t response to the backdoor commands, which means that a reader cannot detect whether it is a Chinese Magic Card or not by sending backdoor commands.</source>
        <translation>    這個叫法在國內比較罕見，在國外指CUID/FUID/UFUID這類對後門指令不響應的卡（防火牆卡）</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="908"/>
        <source>There are some types of Chinese Magic Card Gen2.</source>
        <translation>以下是Gen2卡的詳細介紹</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="909"/>
        <source>    CUID Card:</source>
        <translation>    CUID卡：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="909"/>
        <source>    the Block 0 is writeable, you can write to this block repeatedly by normal wrbl command.</source>
        <translation>    可透過普通的寫塊命令來寫塊0，可重複擦寫</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="910"/>
        <source>    (hf mf wrbl 0 A FFFFFFFFFFFF &lt;the data you want to write&gt;)</source>
        <translation>    (hf mf wrbl 0 A FFFFFFFFFFFF &lt;待寫入資料&gt;)</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="911"/>
        <source>    FUID Card:</source>
        <translation>    FUID卡：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="911"/>
        <source>    you can only write to Block 0 once. After that, it seems like a typical Mifare card(Block 0 cannot be written to).</source>
        <translation>    塊0只能寫入一次</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="912"/>
        <source>    (some readers might try changing the Block 0, which could detect the CUID Card. In that case, you should use FUID card.)</source>
        <translation>    (更高階的穿防火牆卡，可以過一些能識別出CUID卡的讀卡器)</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="913"/>
        <source>    UFUID Card:</source>
        <translation>    UFUID卡：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="913"/>
        <source>    It behaves like a CUID card(or UID card? I&apos;m not sure) before you send some special command to lock it. Once it is locked, you cannot change its Block 0(just like a typical Mifare card).</source>
        <translation>    鎖卡前和普通UID/CUID卡一樣可以反覆讀寫塊0，用特殊命令鎖卡後就和FUID卡一樣了</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="915"/>
        <source>    Seemingly, these Chinese Magic Cards are more easily to be compromised by Nested Attack(it takes little time to get an unknown key).</source>
        <translation>    所有UID卡都似乎更容易被Nested攻擊破解</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="973"/>
        <source>Plz select the trace file:</source>
        <translation>請選擇trace檔案：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="974"/>
        <location filename="../src/ui/mainwindow.cpp" line="1011"/>
        <source>Trace Files</source>
        <translation>Trace檔案</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="1010"/>
        <source>Plz select the location to save trace file:</source>
        <translation>請選擇trace檔案儲存的位置：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="1138"/>
        <source>(None)</source>
        <translation>(無)</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="1139"/>
        <source>Dark</source>
        <translation>暗黑</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="1140"/>
        <source>Light</source>
        <translation>明亮</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="1201"/>
        <source>External file</source>
        <translation>外部檔案</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="1114"/>
        <location filename="../src/ui/mainwindow.cpp" line="1371"/>
        <source>Idle</source>
        <translation>空閒</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="1116"/>
        <source>Stop</source>
        <translation>停止</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="216"/>
        <location filename="../src/ui/mainwindow.ui" line="380"/>
        <source>Sec</source>
        <translation>扇區</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="221"/>
        <source>Blk</source>
        <translation>塊</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="385"/>
        <source>KeyA</source>
        <translation>金鑰A</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.ui" line="390"/>
        <source>KeyB</source>
        <translation>金鑰B</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="1308"/>
        <source>HW Version:</source>
        <translation>韌體版本：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="1310"/>
        <source>PM3:</source>
        <translation>連線狀態：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="1312"/>
        <source>State:</source>
        <translation>執行狀態：</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="1367"/>
        <source>Running</source>
        <translation>正在執行</translation>
    </message>
    <message>
        <location filename="../src/ui/mainwindow.cpp" line="1555"/>
        <source>Actural Freq: </source>
        <translation>實際頻率： </translation>
    </message>
</context>
<context>
    <name>Mifare</name>
    <message>
        <location filename="../src/module/mifare.cpp" line="650"/>
        <location filename="../src/module/mifare.cpp" line="721"/>
        <source>Succeed!</source>
        <translation>成功！</translation>
    </message>
    <message>
        <location filename="../src/module/mifare.cpp" line="216"/>
        <location filename="../src/module/mifare.cpp" line="650"/>
        <location filename="../src/module/mifare.cpp" line="654"/>
        <location filename="../src/module/mifare.cpp" line="678"/>
        <location filename="../src/module/mifare.cpp" line="721"/>
        <location filename="../src/module/mifare.cpp" line="734"/>
        <location filename="../src/module/mifare.cpp" line="791"/>
        <source>Info</source>
        <translation>資訊</translation>
    </message>
    <message>
        <location filename="../src/module/mifare.cpp" line="216"/>
        <source>Plz provide at least one known key</source>
        <translation>請至少提供一個已知密碼</translation>
    </message>
    <message>
        <location filename="../src/module/mifare.cpp" line="475"/>
        <location filename="../src/module/mifare.cpp" line="654"/>
        <source>Failed!</source>
        <translation>失敗！</translation>
    </message>
    <message>
        <location filename="../src/module/mifare.cpp" line="679"/>
        <source>The Access Bits is invalid!
It could make the whole sector blocked irreversibly!
Continue to write?</source>
        <translation>控制位無效！
使用該控制位可能導致目標扇區損壞且無法恢復！
確定要寫入嗎？</translation>
    </message>
    <message>
        <location filename="../src/module/mifare.cpp" line="734"/>
        <source>Failed to write to these blocks:</source>
        <translation>寫入以下塊失敗：</translation>
    </message>
    <message>
        <location filename="../src/module/mifare.cpp" line="738"/>
        <source>Select them?</source>
        <translation>選中這些塊？</translation>
    </message>
    <message>
        <location filename="../src/module/mifare.cpp" line="791"/>
        <source>Failed to read card.</source>
        <translation>讀卡失敗。</translation>
    </message>
</context>
<context>
    <name>T55xxTab</name>
    <message>
        <location filename="../src/ui/t55xxtab.ui" line="17"/>
        <source>Clone to T55xx</source>
        <translation>複製到T55xx卡</translation>
    </message>
    <message>
        <location filename="../src/ui/t55xxtab.ui" line="25"/>
        <source>Target Type:</source>
        <translation>目標卡片型別：</translation>
    </message>
    <message>
        <location filename="../src/ui/t55xxtab.ui" line="70"/>
        <source>Read</source>
        <translation>讀卡</translation>
    </message>
    <message>
        <location filename="../src/ui/t55xxtab.ui" line="80"/>
        <source>Clone</source>
        <translation>複製</translation>
    </message>
</context>
<context>
    <name>Util</name>
    <message>
        <location filename="../src/common/util.cpp" line="122"/>
        <source>Load from external file</source>
        <translation>從外部檔案載入</translation>
    </message>
    <message>
        <location filename="../src/common/util.cpp" line="126"/>
        <source>Choose a language:</source>
        <translation>選擇語言：</translation>
    </message>
    <message>
        <location filename="../src/common/util.cpp" line="131"/>
        <source>Select the translation file:</source>
        <translation>選擇翻譯檔案：</translation>
    </message>
</context>
</TS>
