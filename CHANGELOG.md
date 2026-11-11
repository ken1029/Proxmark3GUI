# Change Log

[中文](doc/CHANGELOG/CHANGELOG_zh_CN.md)  

### V0.3.4 (feature/mfc-workbench, reconnect fix)
+ Restore the persistent client after a one-shot recovery run: methods that poll stdin stop the persistent client to free the serial port, and the GUI now reconnects it (with the same path/args and the normal handshake) once the run ends, instead of leaving the user on "Not Connected" and asking for Connect again. A run started while disconnected still stays disconnected; a manual Connect during the 200 ms port-release window wins
+ `oneShotRunReconnectsClient()` pins the reconnect

### V0.3.3 (feature/mfc-workbench, one-shot output fix)
+ Keep capturing a running task's output when the channel is released: a one-shot recovery run frees the port by stopping the persistent client, and that stop cleared the capture flag, so `commandFinished()` received an empty string and every recovered key was reported as "Method finished without a usable key"
+ Read the methods that print one key instead of a table: `Brute force completed. Key found: XXXXXXXXXXXX` (hardnested), `Found valid key [ XXXXXXXXXXXX ]` (darkside, nested/staticnested per-block lines) and sen's `Reader material recovered sec NNN key A = XXXXXXXXXXXX` / `Key sec NNN key X = ...` / `Found initial key: sector N ...`. hardnested and darkside attribute the key to the command's target sector/key type; a lone key with no target is reported as a warning instead of being guessed into sector 0
+ The mock client now prints the real `[+] ` table prefix and a hardnested result, and `isolatedTaskSurvivesClientStop()`, `mfcRecoverySingleKeyOutput()` and `workbenchRunsHardnestedTask()` pin the invariants

### V0.3.2 (feature/mfc-workbench, parser fix)
+ Read the client key table for real: every row is prefixed with `[+] `, so the row pattern must tolerate the tag instead of anchoring on whitespace; recovery now reports the keys it recovered instead of "Method finished without a usable key"
+ Recognise `Static nonce....... 009080a2` as a static nonce

### V0.3.1 (feature/mfc-workbench, review fixes)
+ Write protection is enforced by one write plan: preview, manual row ticks, "retry failed only" and the command loop all re-check the include options, so Block 0 and trailers never reach the client unless they were explicitly allowed
+ Separate the source document from the card in the field: keys and blocks carry the RF session that produced them, a different UID is not adopted, and a reconnect requires a new identification before evidence is reused
+ Trailer reads keep only the bytes the access conditions return; a candidate key is no longer filled into a "read" trailer
+ Trailer writes validate the access conditions on the card, use the key those conditions accept, and report written+verified / written-not-verified / mismatch / failed instead of re-writing with another key
+ The client channel is owned globally: stopping to wait, a hard timeout or an early error no longer re-enables other pages while the client may still be executing
+ Recovery commands (chk, autopwn, nested, darkside, sen) run in a one-shot client process, because v4.23346 polls stdin and would consume a queued `rem` as a key press
+ Backup `.sha256` sidecars hash the file that was written; the internal document digest keeps its own name
+ The write dialog has one task state: preview/write/retry cannot overlap and the plan is a frozen snapshot

### V0.3.0 (feature/mfc-workbench)
+ Add the MFC workbench page: one document model for data, keys, validity, modification and recovery state
+ Fix RRG `key.bin` interoperability: export all key A then all key B; keep the legacy A0 B0 layout as an explicit import/export choice
+ Refuse to write raw binary dumps with unread blocks; EML uses `?`, JSON keeps block/key state and a SHA-256 digest
+ Validate BIN/EML/JSON and key files before replacing the current document; reject truncated or malformed input with a line/size error
+ Add offline A/B comparison with same/different/unknown/incomparable states, capacity/UID notes and a Markdown/CSV report
+ Add per-sector key state (unknown/candidate/verified/failed) and source (manual/import/dictionary/recovery/card)
+ Add recovery method advice (dictionary, autopwn, nested, staticnested, hardnested, darkside, sen, isen) with explicit preconditions
+ Parse client key tables by the sector/block columns, not row order; partial, duplicate and out-of-range rows cannot shift a key
+ Add long-task execution without an idle timeout, with marker completion, hard timeout, cancel semantics and one-device-task ownership
+ Add controlled write plans: data blocks by default, Block 0/trailers opt-in, card preview, read-back verification and failure list
+ Connect verified recovery keys to reading, backup and comparison without re-typing them

### V0.2.10
+ Marker mode now accepts only the matching `remark:` reply; a split `pm3 --> ` echo is no longer a completion boundary (writes, emulator, trace load/save, handshake)
+ Add explicit interactive-prompt mode for clients that really print a trailing prompt
+ Connect attempts get a GUI-side generation: cancelling before the worker starts or after a queued success cannot revive an old connection
+ Add deterministic split-prompt and pre-start cancellation regressions

### V0.2.9
+ Connect over a pipe immediately: send `hw version` at startup and use a unique `rem` marker as the completion boundary (v4.23346 client has no startup/trailing prompt on a pipe)
+ Add a completion marker to writes and trace load/save so they stop waiting for a prompt that never arrives
+ Report connect phases (process start, version, handshake) and the total button-to-connected time
+ Make Disconnect cancel an in-flight handshake; restore the UI on failure; ignore duplicate Connect clicks
+ Fix the QApplication shutdown order and bound the PM3-thread cleanup
+ Disable the live LF/T55xx docks when the capability manifest removes LF

### V0.2.8
+ Add support for Iceman/RRG repo v4.16717  
+ Fix some bugs  
+ Make it easier for testing this GUI across multiple clients  
+ Add support for Bluetooth and TCP connection  

### V0.2.7
+ Fix writing to Block 0 failure when using with RRG repo v4.15864  
+ Disable disconnection detection on Linux/macOS by default  
+ Fix a little bug in the config file  
+ Fix the Trailer Decoder  
+ Show more details in the Trailer Decoder  
+ Add dark theme(from https://github.com/ColinDuquesnoy/QDarkStyleSheet)  
+ Add support for customizable theme, opacity and fonts  
+ Fix translations  

### V0.2.6
+ Add support for Iceman/RRG repo v4.15864 [#37](https://github.com/wh201906/Proxmark3GUI/issues/37)  
+ Optimize mifare classic block writing logic  
+ Fix the default lf config  
+ Add feedback for the GUI failing to start the client  
+ Add feedback for the client failing to connect to PM3 hardware  
+ Detect PM3 hardware when searching serial ports  
+ Remove extra empty lines in raw command output  
+ Use embedded config files  
+ Remove the wait time between performing nested attack then switching to staticnested attack  

### V0.2.5
+ Fix bug [#28](https://github.com/wh201906/Proxmark3GUI/issues/28)  

### V0.2.4
+ Clone EM410x card to T55xx card  

### V0.2.3
+ Fix bug [#27](https://github.com/wh201906/Proxmark3GUI/issues/27)  
+ Try to support Non-ASCII path  

### V0.2.2
+ Load command format from external json file  
+ Fix bug [#20](https://github.com/wh201906/Proxmark3GUI/issues/20), [#21](https://github.com/wh201906/Proxmark3GUI/issues/21), [#22](https://github.com/wh201906/Proxmark3GUI/issues/22)  
+ Support Iceman/RRG repo v4.13441

### V0.2.1
+ Optimize MIFARE Classic reading logic  
+ Fix bug [#16](https://github.com/wh201906/Proxmark3GUI/issues/16)  
+ Fix bug [#15](https://github.com/wh201906/Proxmark3GUI/issues/15) partially (the path can contain spaces now)  

### V0.2
+ Use Dock widget for more flexible layout  
+ Support basic LF commands  
+ Fix a bug in RawCommand tab  

### V0.1.4
+ Optimize performance  
+ Optimize UI  
+ Search available ports automatically  
+ Add High-DPI support  
+ Support configuring environment variables by script  
(Useful when the client requires specific environment variables)  
+ All functions are compatible with Iceman/RRG repo(tested on v4.9237)  
+ Support specifying client working directory
+ Fix some bugs

### V0.1.3
+ Fix Trailer Decoder
+ Add feedback when writing selected blocks

### V0.1.2
+ Optimize read logic
+ Make UI Customizable
+ Save client path automatically
+ Add Trailer Decoder(Deprecated, plz use V0.1.3 or higher version)
+ Support read/write selected blocks
+ Support a few Iceman functions
+ Fix some bugs

### V0.1.1
+ Complete Mifare module(support simulate and sniff)

### V0.1
+ Able to deal with Mifare card and related files

### V0.0.1
+ a dumb version with a useless GUI and a serial choose box.