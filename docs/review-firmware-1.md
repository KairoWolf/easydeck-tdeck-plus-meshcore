# EasyDeck firmware review #1: bug hunt before first hardware run

Scope: `firmware/src/**` (main, EasyMesh, MessageLog, Storage, SyncInterface, DeviceModel/DeviceUI, HAL, board/target, App, Sos, SetupFile, screens spot-checked), checked against the MeshCore sources it calls (`examples/companion_radio/MyMesh.*`, `DataStore.*`, `src/helpers/BaseChatMesh.*`, `MultiSerialInterface.h`, `ArduinoSerialInterface.*`, `esp32/SerialBLEInterface.*`, `sensors/EnvironmentSensorManager.cpp`, `ESP32Board.h`), LVGL 9.2.2, LovyanGFX 1.2.7 and arduino-esp32 2.0.17 (HWCDC, HardwareSerial) from `.pio/libdeps` / `~/.platformio`.
No files were changed. Line numbers refer to the current tree. Stack frame sizes were read from the built `firmware.elf` (`entry a1, N`).

Severity: **Critical** = a core feature is broken or the device crashes in normal use. **High** = data loss or a crash in a reachable path. **Medium** = misbehaviour or a likely watchdog reset. **Low** = minor.

---

## Critical

### C1. EDS sync never advances and floods BLE: `MultiSerialInterface::writeFrame` returns 0 unless *every* enabled interface accepts the frame
- `sync/SyncInterface.cpp:20-23` (`sendNow` returns `_inner.writeFrame(...) > 0`), used at `:256`, `:268`, `:276`.
- `main.cpp:35-37`: `UsbSerialInterface::writeFrame` returns 0 when no USB host is present.
- MeshCore `MultiSerialInterface.h:160-178` writes to all *enabled* interfaces and returns `len` only if all of them returned `len`. `SerialBLEInterface::writeFrame` returns 0 when no phone is connected.
- Both interfaces get enabled by `startInterface()` → `SyncInterface::enable()` → `_inner.enable()`.

**Why it's a bug:** with only a phone on BLE (the normal case), the BLE write gets queued but USB returns 0, so `sendNow()` reports failure. `_stream_seq` never increments. The same record is re-queued on every loop iteration once `isWriteBusy()` clears, about every 60 ms. `EDS_RESP_END` is never sent. `_push_new` is never cleared, so `EDS_PUSH_NEW` repeats every 60 ms. A failed push update is retried forever (`return` before the `memmove`). The reverse also fails: USB connected with BLE enabled but not connected. Sync only works when BLE and USB both have a reader.

**Symptom:** the EasyDeck phone app receives the same record (or `PUSH_NEW`) at about 16 Hz forever. History sync never completes and BLE stays saturated.

**Fix:** don't use the aggregate return value. Either (a) make "not connected" interfaces report success: return `len` from `UsbSerialInterface::writeFrame` when `!Serial`, and wrap `SerialBLEInterface` so it returns `len` when `!isConnected()`; or (b) write per interface in `SyncInterface` (keep pointers to the BLE/USB instances) and count success if any *connected* interface accepted the frame.

### C2. People list, SOS recipients and map markers skip the 8 newest contacts and show the anonymous slots instead
- `device/DeviceModel.cpp:100-108`: `contactCount()` returns `getNumContacts()` (= `num_contacts - MAX_ANON_CONTACTS`). `contactAt(i)` calls `getContactByIdx(i)`, which indexes `contacts[i]` with **no** offset (`BaseChatMesh.cpp:968-973`). Indices 0..7 are the reserved `MAX_ANON_CONTACTS` (=8) transient slots (`BaseChatMesh.h:41,98-99`). Real contacts start at index 8.
- Callers: `PeoplePage.cpp:136`, `Sos.cpp:28-30` (SOS recipients), `MapScreen.cpp:42,143`.

**Symptom:** with 8 or fewer contacts the People page is empty (the anon slots have type 0 and only show under "Repeaters & rooms" as blank rows) and SOS has no recipients ("No SOS contacts yet"). With more contacts, the 8 most recently added are missing everywhere, including SOS fan-out.

**Fix:** `contactAt(i)` → `getContactByIdx(i + MAX_ANON_CONTACTS, c)` (or iterate with `startContactsIterator()`), and skip `type == ADV_TYPE_NONE`.

### C3. Opening the repeater terminal overflows the 8 KB loop-task stack
- `ui/screens/Admin.cpp:60`: `CliLine lines[40];` is 40 × 162 B. The ELF shows `TerminalScreen::tick()` with `entry a1, 0x1a30` = **6704 bytes**. `CONFIG_ARDUINO_LOOP_STACK_SIZE` is 8192, and no `SET_LOOP_TASK_STACK_SIZE` is set.
- `tick()` is called from `build()` (input → `App::input` → screen → `push` → `build` → `tick`) and every 500 ms from `App::loop`. On top of 6.7 KB it calls `snprintf` (`_svfprintf_r` frame 784 B), `lv_label_create` and the LVGL layout code.

**Symptom:** "Stack canary watchpoint triggered (loopTask)" panic and reboot on Contact → Admin → Terminal.

**Fix:** make the buffer `static` (or a member, or heap-allocated), or fetch the lines one at a time. Optionally raise the loop stack with `SET_LOOP_TASK_STACK_SIZE(16*1024)`.

### C4. Turning GPS on at runtime closes the GPS UART; GPS stays dead until reboot (Setup wizard, SOS, GPS toggle)
- `device/DeviceModel.cpp:364-370`: `setGps(true)` calls `board.prepareGps()`, which ends with `Serial1.end()` on every path (`TDeckPlusBoard.cpp:67,72,84`). It then calls `sensors.setSettingValue("gps","1")` → `start_gps()` (`EnvironmentSensorManager.cpp:890-905`), which only calls `_location->begin()/reset()` (pin control) and **never calls `Serial1.begin()` again**. Only `initBasicGPS()` at boot opens the UART.
- Callers: `Setup.cpp:232` (every fresh device: the first-run wizard enables GPS), `Sos.cpp:80` (SOS start when GPS was off), `Tools.cpp:130,140,146`, `SettingsPage.cpp:146`, `SetupFile.cpp` `gps=yes` (runs after `sensors.begin()`).
- At boot this works only because `main.cpp:94-95` runs `prepareGps()` *before* `sensors.begin()`.

**Symptom:** after the setup wizard, or after starting an SOS with GPS off, the GPS never gets a fix ("no GPS fix" in every SOS message, no clock from GPS) until the next reboot. `prepareGps()` also blocks the loop for up to about 2.75 s (two 1.3 s probes plus 150 ms), which uses up much of the 5 s loop watchdog.

**Fix:** after `prepareGps()`, re-open the port the way `initBasicGPS` does (`Serial1.setPins(PIN_GPS_TX, PIN_GPS_RX); Serial1.begin(GPS_BAUD_RATE);`), or make `prepareGps()` leave `Serial1` open at 38400. Run the probe only once per boot, or make it non-blocking (a state machine in `board.loop()`).

---

## High

### H1. No-SD mode: the PSRAM cache (1500) is larger than the flash ring (300); "mark read" overwrites live records with stale ones
- `store/MessageLog.cpp:11-12` (`FLASH_CAPACITY 300`, `CACHE_RECORDS 1500`). `cached()` at `:134-137` only checks `seq < _next_seq` plus magic/seq, not `seq >= firstSeq()`. The `lo = _next_seq - _cache_n` windows at `:173,193,214,224,234,266` reach below `firstSeq()`.
- `markConversationRead()` at `:191-210` writes every matching cached record to offset `(r->seq % _capacity) * 256`. For a stale record (seq < firstSeq) that slot now holds a newer live record (seq + k·300), which gets overwritten.

**Symptom:** without an SD card, after more than 300 messages, opening a chat with old unread messages (for example a busy Public channel) silently replaces up to 300 recent messages on flash with old copies. After a reboot `loadCache` sees seq mismatches and the recent history is gone. `get()`, `conversationSeqs()` and `listConversations()` also return records that are no longer on flash, so the UI disagrees with the phone sync (`firstSeq`) and with what you see after a reboot. On SPIFFS, hundreds of random rewrites can also trip the loop watchdog (see M5).

**Fix:** `_cache_n = min(CACHE_RECORDS, _capacity)` in `loadCache()`. Add `seq >= firstSeq()` to `cached()`, and clamp every `lo` to `firstSeq()`.

### H2. SOS countdown screen: the LVGL timer outlives its widgets, so use-after-free when you long-press during the countdown
- `ui/screens/SosScreen.cpp:239-253`: `SosScreenTimed` runs a 100 ms `lv_timer` → `frame()` → `loopArming()` → `lv_label_set_text(_num)`, `lv_arc_set_value(_ring)`, and at 10 s `show()` → `lv_obj_clean(root)`. The timer is deleted only in the destructor.
- `ui/App.cpp:298-300`: `IN_BACK` (trackball held 800 ms) is handled by `App::input` *before* the screen's `onInput` (which would cancel the countdown), so it pops the SOS screen. The pop animation (200 ms, `auto_del=true`) deletes `root`, but the `Screen` object (and its timer) is only deleted by `reap()` after `GRAVE_MS = 600` ms (`App.cpp:7,171-179`). The same happens via `replaceAll()`, which deletes roots synchronously.

**Symptom:** holding the trackball to back out of the SOS countdown (a natural gesture) writes into freed LVGL objects 3–4 times. Expect heap corruption or a LoadProhibited panic. If the 10 s mark falls inside that window, the SOS starts and `show()` cleans a freed root.

**Fix:** delete the timer when the screen is popped or replaced. Better, for all screens: free the `Screen` from an `LV_EVENT_DELETE` handler on `root` instead of using a fixed 600 ms graveyard (this also fixes M7). Alternatively let SosScreen treat `IN_BACK` as "cancel" while `_mode == 1`.

### H3. One interrupted meta write wipes the whole message history
- `store/MessageLog.cpp:44-53`: if `messages.meta` is short or invalid, a new `log_id` is generated and `fs.remove(_data_path)` **deletes the data file**.
- `saveMeta()` (`:70-77`) reopens the meta file with `"w"` (truncate) after **every** `append()` (`:149`). A reset between the truncate and the write leaves a 0-byte meta: brownout on TX at low battery, a watchdog panic, a crash, or pulling the battery.
- The same pattern applies to `DeviceModel::saveUi()` (`DeviceModel.cpp:34-41`, written on every SOS update). A truncated UI file falls back to defaults: `setup_done=0` (wizard again) and `sos_active=0` (an active SOS is not resumed after the reset).

**Symptom:** after an unlucky reset, all chat history is gone, and the phone app sees a new `log_id` and drops its cache.

**Fix:** never delete data because of bad meta. Rebuild `next_seq` by scanning slots (keep the highest valid `seq`), and keep `log_id` in a second file or derive it from the first record. Write meta/UI atomically: write to `.tmp` then `rename`, or use two alternating copies with a generation counter. Saving meta every N appends is fine because the scan recovers the rest.

---

## Medium

### M1. "Clock valid" is always true: the threshold is below MeshCore's boot seed
- `DeviceModel.cpp:400` (`clock_valid = now > 1735689600` = 1 Jan 2025) and `:690` (rooms). MeshCore's `ESP32RTCClock::begin()` seeds the clock to `RTC_TIME_MIN = 1772323200` (1 Mar 2026, `ESP32Board.h:206-225`), and `bootstrapRTCfromContacts()` moves it further forward.

**Symptom:** the "Clock not set" warnings (Launcher, Settings, Signal) never appear. `App::autoAdvert` (`App.cpp:306-313`) sends the first flood advert 20 s after boot with an unsynchronised clock. Peers drop adverts whose timestamp is not newer than the last one they saw ("nobody can see me"). Room re-logins (`loopRooms`) also go out with a stale timestamp. Chat times and the status bar clock are wrong until GPS or the phone sets the clock.

**Fix:** track a "clock set by GPS/phone/user" flag (set it in `setTime`, on GPS fix, and when the companion app sends `CMD_SET_DEVICE_TIME`), or at least compare against a value greater than `RTC_TIME_MIN`.

### M2. SOS cadence and elapsed time break when the clock jumps
- `Sos.cpp:79` stores `sos_started = status().now`. `:69-71` derives the interval from `now - sos_started`.

**Symptom:** an SOS started before GPS or the phone set the clock (likely, since SOS also switches GPS on) gets a clock jump of possibly months once the GPS fixes. Elapsed becomes huge, so the cadence drops straight from every 2 min to **every 30 min**, and the "SOS ACTIVE mm:ss" timer shows nonsense.

**Fix:** measure the cadence with `millis()` (persist the elapsed seconds or the update count rather than the wall-clock start), or rebase `sos_started` when the clock first becomes trustworthy.

### M3. SOS fan-out silently fails with many favourites
- `EasyMesh.cpp:55-58`: when all 8 outbox slots are in use, `_outbox[0]` (whatever is in slot 0, not the oldest) is finished as `ST_FAILED` with no retries. `MyMesh` uses `StaticPoolPacketManager(16)` (`MyMesh.cpp:901`). Each DM holds a pool packet until it goes on air (hundreds of ms at SF9), so a burst can exhaust the pool and `sendMessage` returns `MSG_SEND_FAILED`.

**Symptom:** with more than 8 SOS favourites (plus `#emergency`, LOC beacons and pending chats), some recipients get "not delivered" right away and are never retried.

**Fix:** queue the SOS sends (stagger them, at most N in flight), evict the oldest entry by deadline instead of slot 0, and size `OUTBOX_SIZE` for the maximum number of favourites.

### M4. EDS session state isn't reset on a BLE reconnect while a USB host is attached
- `SyncInterface.cpp:236-242` resets `_eds_active` and `_streaming` only when the *aggregate* `MultiSerialInterface::isConnected()` changes. `UsbSerialInterface::isConnected()` is `HWCDC::isCDC_Connected()`, which is true whenever the device is plugged into a PC (SOF present), even with no terminal open.

**Symptom:** while plugged into a laptop, a phone reconnecting over BLE inherits the previous session's EDS state. The official MeshCore app (which never sent HELLO) receives `0xCE`/`0xCF` pushes, which contradicts the protocol promise.

**Fix:** track the BLE `isConnected()` edge (and the USB edge) separately, and reset EDS state on either edge.

### M5. Opening a chat with many unread messages does O(N) synchronous file writes and UI refreshes in one loop pass, which can trip the 5 s watchdog
- `MessageLog.cpp:191-210`: one seek+write per unread record (SD sector read-modify-write, or SPIFFS page rewrites with possible GC), plus a `notifyUpdated` per record. Each notification runs `App::onMessage` → for example `Launcher::refresh()` (`Launcher.cpp:151`: `totalUnread()` scans 1500 records, plus `status()` and badge re-creation) and `SyncInterface::onLogUpdated`.

**Symptom:** after a night of traffic on a busy channel (hundreds to 1500 unread), opening it can freeze for seconds and reboot through the loop watchdog. This is much worse in no-SD mode with H1.

**Fix:** batch the writes (collect the slot indices first, sort by offset), send one aggregated update notification (or push only the last N to EDS), and/or call `feedLoopWDT()` / `delay(0)` inside long loops.

### M6. The GPS track file grows without bound in the same SPIFFS partition as contacts and prefs
- `DeviceModel.cpp:471-478`: `FILE_APPEND`, 12 B per point, a point every 10 s while moving, and never trimmed (`trackLoad` only *reads* the last 1500). With `STORAGE_FLASH` this is the 448 KB `edfs` SPIFFS, which also holds `messages.log` (77 KB) and `contacts3` (up to 53 KB).

**Symptom:** after tens of hours of recorded movement SPIFFS fills up. MeshCore's `saveContacts()` / `savePrefs()` open with `"w"` (truncate) and then fail partway, so contacts or prefs are lost after a reboot. Near-full SPIFFS garbage collection can also block for seconds (watchdog).

**Fix:** cap the file (for example rewrite to the newest 1500 points when it exceeds 2× that, or use a ring file like MessageLog). Keep it on SD when one is present.

### M7. A pop issued before a push animation has started frees a `Screen` whose root is still the active screen
- `App.cpp:181-208`: `push()` starts `lv_screen_load_anim(..., auto_del=false)`. In LVGL 9.2 the active screen only switches in the animation start callback, which runs on the next `lv_timer_handler()`. If `pop()` runs before that (same `DeviceUI::loop` pass, for example a trackball click that pushes and a keyboard Backspace that pops in the same `input.poll()`), `lv_screen_load_anim(top->root)` hits `act_scr == new_scr` and returns early (`lv_display.c:592`). The push animation then continues, so the popped screen becomes active and is never auto-deleted. Its `Screen` object is still freed by `reap()` 600 ms later.

**Symptom:** the display shows a screen whose callbacks (`HeaderScreen::backCb`, `FocusList::clickCb`, `ChatScreen::sendCb` with `this`/`&_list` user data) point to freed memory. Touching it causes a use-after-free. Input goes to a different screen than the one shown.

**Fix:** in `pop()`/`push()`, if a screen load is still pending (LVGL 9.2 has no public getter for `scr_to_load`; track it in App, e.g. remember the push time and treat anything under `T::ANIM_SCREEN` + one `lv_timer_handler` pass as pending), finish it first (non-animated load of the current top), or skip animations when one is pending. Free `Screen` objects from the root's `LV_EVENT_DELETE` (see H2).

### M8. The display is initialised microseconds after the peripheral rail switches on (needs a check on hardware)
- `TDeckPlusBoard.cpp:5-23` sets `PIN_POWER_ON` HIGH, and `main.cpp:59-62` goes straight to `tft.start()`. `pin_rst = -1`, so there's no hardware reset. LilyGO's reference code waits about 500 ms after `BOARD_POWERON` before touching peripherals. If the ST7789 sits on the switched rail it is still in power-on reset (the panel needs about 120 ms) when LovyanGFX sends SLPOUT/DISPON.

**Symptom:** possibly a black screen after a cold power-on (warm resets are fine).

**Fix:** `delay(150–300)` after enabling the rail, before `tft.start()`. The cost is small.

---

## Low

- **L1. EDS frames can exceed the BLE notify limit.** `SyncInterface.cpp:131-152` fills records up to `MAX_FRAME_SIZE` = 176. `SerialBLEInterface` sets MTU 176, so a notification can carry at most 173 bytes. Bluedroid truncates silently: long messages lose their last 3 bytes, possibly in the middle of a UTF-8 sequence. Cap BLE frames at `MTU-3` (173).
- **L2. Each incoming SOS update pushes another full-screen alarm.** `App.cpp:243-250` pushes `SosAlarmScreen` for every `SOS!` message, which arrives every 2–30 min per sender and from `#emergency`. The stack grows without limit and the screen is forced on each time if nobody acknowledges. Update the existing alarm when the top screen is already an alarm for the same conversation.
- **L3. Touch can't wake the screen.** While the screen is off, `lv_timer_handler()` isn't called (`DeviceUI.cpp:213`), so `touch_cb` (and `app.touched()`) never runs. Check `input.touch().pressed` in `DeviceUI::loop` to wake.
- **L4. `EDS_CMD_GET_SINCE` with `from > nextSeq`** replies `EDS_RESP_END{from}`, a bogus next_seq (`SyncInterface.cpp:169-178`). Clamp `from` to `nextSeq()`.
- **L5. Keyboard-light flashing does an I2C write on every loop pass.** `DeviceModel::loopFlash()` (`DeviceModel.cpp:429-434`) writes on every loop iteration for 2.4 s. Only write when the on/off state changes.
- **L6. Bluetooth off is not saved, and contact removal doesn't refresh lists.** `DeviceModel::setBluetooth` doesn't persist, so Bluetooth comes back on after a reboot. `DeviceModel::removeContact` doesn't call `app.onContactsChanged()`, so lists stay stale until the next refresh.
- **L7. `DeviceModel::onResponse` takes any response as the login reply.** While `ADM_PENDING`, any response from the admin key is treated as the login reply (`DeviceModel.cpp:564-571`), including status or telemetry. It also doesn't recognise the legacy `"OK"` login reply, which it reports as a failure.

---

## Checked and found OK (no action)

- **EDS frame layout:** `encodeRecord` matches the offsets in `EasyDeckProtocol.h` (header 27 + 1 code byte), with no buffer overflow. The HELLO, END and OK sizes are correct. Phone send parsing (`[2,type,attempt,ts,prefix6,text]`, `[3,type,idx,ts,text]`) matches `MyMesh::handleCmdFrame`. `RESP_CODE_SENT` is 10 bytes with the ack at `[2..5]`.
- **Shared SPI host:** LovyanGFX 1.2.7 on `SPI2_HOST` under Arduino calls `SPI.end(); SPI.begin()` and uses `SPI.bus()`. `use_lock` takes Arduino's bus mutex, and `endTransaction` restores the full-duplex `USER` register for SD and RadioLib. With no DMA and every bus user on loopTask, there's no conflict. Init order (display → radio → SD) is fine because SD and radio come after LovyanGFX re-begins the bus.
- **`flush_cb`:** `writePixels(..., swap=true)` is correct for native little-endian LVGL RGB565.
- **Static initialisation:** `DataStore`/`IdentityStore` only store `&DataFS`, the `MyMesh` constructor only stores references, and `DataFS`/`data_impl` live in the same translation unit in the right order. No cross-TU order problem.
- **I2C (keyboard, touch, keyboard light, RTC probe):** all on loopTask. The trackball ISRs only increment `volatile uint32_t` counters, which is safe.
- **SD ring:** with SD (capacity 20000 > cache 1500), the seq/slot/`firstSeq` math, wrap-around and meta-lag recovery are correct.
- **MeshCore conventions:** the factory-reset intercept avoids `DataStore::formatFileSystem()`'s `SPIFFSFS*` cast. The scope key (`SHA256("#name")[0..15]`) matches `TransportKeyStore`. The discover request/response layout matches `simple_repeater`. The LPP GPS parse is right (24-bit signed, /10000).
- **LVGL deletes inside event callbacks:** `FocusList::clickCb` → `clear()` / `lv_obj_clean` is safe in 9.2 (`lv_indev_reset` plus event-deleted marking).

---

## Status (2026-11-19)

Fixed in 0.17.0: C1-C4, H1-H3. Fixed in 0.18.0: M1-M8. Low-severity items still open.
