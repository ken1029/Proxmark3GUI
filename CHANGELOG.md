### V0.4.0 (mfc-r9, multi-document workbench and device-free demo)
+ Multi-document workbench: each tab owns its document, source, unsaved state, filter, search, selection, scroll position and undo/redo history. A task is bound to the tab that started it; late output returns to that document and never to another card's tab. Documents that were never saved are written to an atomic local draft store and can be restored from the Document menu
+ Data table: Sector/Block/type/Hex/ASCII/state columns, sector and state filters, hex/ASCII search, a details pane (source, RF session, last operation, trailer Key A/B, access-bit complement and permissions), copy/paste of multiple rows with validation, selected-block export, and undo/redo
+ Batch key recovery: a queue of pending (sector, Key A/B) items with per-item state. Candidate verification, whole-table methods (chk/autopwn/SEN) and per-target methods (nested/staticnested/hardnested/darkside) share one task line; a failed item never blocks the next, cancelling keeps verified results, and "Retry missing only"/"Read remaining blocks" continue from there. Recovered values stay candidates until the card authenticates them
+ Continuous scan: read-only identification loop that keeps a card in one round while it stays on the antenna, starts a new round after removal, and optionally reads readable blocks. Scan results are normal documents with their own UID/source, selectable two at a time for comparison and batch export
+ Backup library: local index with real file SHA-256, duplicate marking, aliases/notes/tags, UID/type/date/completeness filters, drag-in of files and folders, conflict-aware batch conversion to BIN/EML/JSON, and direct "open in workbench"/"compare two"
+ Compare: each side can be the current document, a card snapshot taken in the current RF session, a file or a library backup; byte-level difference notes, difference/unknown filters and sector location. Write plan shows source, target UID/type, preview and post-write read-back side by side
+ New read-only pages: Ultralight/NTAG (type/page model, lock bits, NDEF TLV records, raw output) and Trace (packet table, direction/CRC/time filters, selected-packet CSV/pcapng export). Diagnostics page reports COM/USB identity, client/firmware versions, capability manifest, channel ownership and handshake errors, with clipboard summary and report export
+ `Ctrl+K` command search, a fixed top status strip (device/COM, connection, card UID/type, document), serial hot-plug with USB-serial reconnection across COM renumbering, remembered window/dock state, and Traditional Chinese translations for the new strings
+ Demonstration mode: the normal workbench commands are answered by a canned Classic card and two differing dumps, so identify/read/compare/write-preview can be exercised without a device; every result is labelled as demo data and uses its own source/session. Imported real backups still work offline
+ No firmware or client change: capabilities 11, client v4.23346

# Change Log

### V0.4.1 (mfc-r10, card snapshot from imported documents)
+ Fix "Card snapshot" showing 0 blocks after importing a BIN/EML/JSON dump. `Read missing blocks` now reads every block that is not evidence in the current RF session (`blockReadIn(block, targetSession)`) instead of skipping every complete block, so imported and older-session data is read again from the card
+ Pressing "Card snapshot" with no current-session blocks now offers to read the card and then build the snapshot, reports missing card identity or verified keys, and keeps manual edits; reading never downgrades a complete historical block to failed when the read does not authenticate
+ The data table marks a `Read` block from an older session as "historical" (歷史), so an empty snapshot is explainable before it happens
+ Traditional Chinese strings for the new flow, plus fixes for the old `格擋`/`塊` block translations
+ `workbenchSnapshotReadsImportedDocument()` and `workbenchSnapshotKeepsModifiedBlocks()` pin the read-again and manual-edit-preservation behavior

[中文](doc/CHANGELOG/CHANGELOG_zh_CN.md)  

### V0.3.7 (feature/mfc-workbench, identify on demand and nonce on tab)
+ "Verify known keys" no longer requires a separate Identify: when the document does not match the card in the field, `verifyKeys()` identifies the card first (the existing card-change dialog still applies) and then authenticates every candidate against that RF session; a failed identification keeps the document and stops before any key is marked failed
+ Opening the "Key recovery / decryption" tab diagnoses the nonce type once (keyless `hf mf info`) while it is still unknown, so the applicability table starts from evidence instead of "unknown (run diagnostics)"; a known nonce is not re-checked on every tab switch
+ `verifyKeysIdentifiesCardFirst()` and `recoveryTabDiagnosesNonce()` pin both behaviors

### V0.3.6 (feature/mfc-workbench, one-shot session preservation)
+ Keep the RF session across a one-shot recovery run: the GUI itself stops the persistent client to free the serial port, so that stop is not a card change. After the automatic reconnect the workbench re-reads the UID; the same card keeps `targetSession`, so recovered keys are immediately usable in Data/Backup, while a different UID (or no card) drops the old evidence and requires Identify. Read/verify/write/recovery are blocked while the UID confirmation is in flight
+ `oneShotSessionSurvivesReconnect()` and `oneShotSessionDropsChangedCard()` pin same-card and changed-card behavior

### V0.3.5 (feature/mfc-workbench, trailer Key B fix)
+ Complete masked trailer blocks from the verified key store: a MIFARE Classic trailer read masks Key B for access conditions 3..5, so the workbench stored 12 `?` for it and every sector stayed "unknown" (16 unknown on a 1K card) even though all keys were already recovered/verified. `trailerReadback()` now fills the masked Key B from the verified key B of that sector, exactly like Key A, and logs why a trailer is still incomplete when the key is genuinely unknown
+ `trailerReadbackUsesVerifiedKeyB()` pins both cases: verified key B completes the trailer; a pending/unverified key B keeps the bytes unknown
+ "Verify keys" re-authenticates historical keys: after the automatic reconnect every recovered key is `Verified` in the previous RF session, and the old selection (`state == Pending`) skipped them, leaving nothing to verify and no key the data page may use. Keys with a value that is not verified in the current session are now offered for re-verification; `verifyKeysRevalidatesHistoricalKeys()` pins it

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