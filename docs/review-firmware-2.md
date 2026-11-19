# EasyDeck firmware review 2: d2f9695..HEAD (0.20.0 to 0.27.0)

Scope: `git diff d2f9695..HEAD -- firmware/src ':!firmware/src/ui/fonts'`. I read the new files in full, along with the
framework and library code they rely on: arduino-esp32 2.0.17 `HTTPClient`, `WiFiClientSecure` and `WiFiGeneric`;
LVGL 9.2 `lv_obj.c`; MeshCore `BaseChatMesh`, `MyMesh`, `Dispatcher`, `MicroNMEALocationProvider` and
`EnvironmentSensorManager`. Every finding below was checked against that code. Line numbers refer to HEAD.

---

## 1. CRITICAL: use-after-free in the tile download task crashes the device at the end of almost every download

`firmware/src/device/TileDownloader.cpp:278` (`HTTPClient http;`), `:322-323` (`delete sec; delete plain;`), and the
closing brace on the next line.

`http` is a local inside the `if` block, so its destructor runs at the closing brace, after `delete sec` / `delete plain`.
In arduino-esp32 2.0.17:

- `HTTPClient::~HTTPClient()` runs `if(_client) _client->stop();`.
- `http.end()` calls `disconnect(false)`. When the connection is kept alive (`_reuse && _canReuse`), `_client` is left
  set. When the socket is already closed, the `else` branch only logs "tcp is closed" and still does not clear
  `_client`. `_client` is cleared only when the server sent `Connection: close` or answered with HTTP/1.0.

With `setReuse(true)` and any normal keep-alive tile server (Thunderforest, nginx, CDNs), `_client` still points at the
deleted `WiFiClientSecure` or `WiFiClient`. The destructor then makes a virtual `stop()` call through freed memory.
The heap allocator has already overwritten the vtable pointer with free-list links, and `WiFiClientSecure::stop()`
would also free `sslclient` a second time.

This runs on every path out of the loop once at least one request has been made: finished (`DL_DONE`), cancelled,
Wi-Fi lost, and too many failures.

**Device symptom:** a panic and reboot (LoadProhibited / InstrFetchProhibited, or later heap corruption) right when a
download finishes or is cancelled. If the loop task is writing a tile at that moment, the SD card's FAT can also be
damaged.

**Fix:** destroy `HTTPClient` before the clients. Either put `HTTPClient http;` and the request loop in their own inner
block that closes before `delete sec; delete plain;`, or declare the clients as stack objects *before* `http`
(`WiFiClientSecure sec; WiFiClient plain; HTTPClient http;`) so they are destroyed in reverse order. Calling `http.end()`
alone is not enough, because it does not clear `_client`.

---

## 2. HIGH: SOS screen keeps a dangling `_map` pointer, so losing the GPS fix during an active SOS can crash the device

`firmware/src/ui/screens/SosScreen.cpp:17` (`show()` runs `lv_obj_clean(root)`), `:151`, `:190-196`.

- `show()` deletes every child of `root`, including the map preview, but does not reset `_map`.
- `buildActive()` assigns `_map` only `if (st.gps_fix)`.
- The sequence: SOS is active with a fix, so `_map` is set. The fix drops, `tick()` calls `show()`, and `buildActive()`
  runs with no fix, so `_map` still points at the deleted object.
- On a later tick, `gps_fix == _map_fix`, so the code goes on to `if (_map && now - _map_ms > 60000)` and calls
  `lv_obj_get_coords(_map)` and then `lv_obj_delete(_map)` on freed memory.
- This happens within about 60 s of losing the fix. It also happens immediately when an SOS is cancelled and restarted
  without a fix on the same screen (`_map` is still stale and `_map_ms` is old).

**Device symptom:** a crash or heap corruption in the middle of an SOS. GPS fixes come and go under tree cover, so this
is a realistic case. The SOS resumes after the reboot, but the device has been down for a while.

**Fix:** set `_map = nullptr;` (and `_map_ms = 0`) at the top of `show()`.

### 2b. MEDIUM, related: the map refresh reads absolute coordinates, which are wrong during a screen animation

`SosScreen.cpp:193-196` recreates the preview at `lv_obj_get_coords(_map)`. Those are absolute screen coordinates, and
they include the root's x offset while a screen slide is running (`lv_screen_load_anim` moves the root by ±320 px). The
offset becomes permanent because the new map is placed with `lv_obj_set_pos` relative to `root`.

`_map_ms` starts at 0, so the first tick after a push always refreshes. Ticks come every 500 ms and the slide lasts
200 ms (`T::ANIM_SCREEN`), so this happens roughly 40% of the time:

- when you re-open the SOS screen while an SOS is active (trackball hold, or the X key), or
- when you return from a chat you opened from the SOS list.

**Symptom:** the SOS map preview jumps off-screen, or lands on top of the contact list, for a minute.

**Fix:** keep the map rectangle in members (`lw + 4, STATUS_H + 56, 128, ...`) and reuse those values. Do not read
coordinates back from the object. Set `_map_ms = lv_tick_get()` when the map is built.

---

## 3. HIGH: the screen lock can be bypassed through the SOS screen

`firmware/src/ui/App.cpp:337-340`, `firmware/src/ui/screens/SosScreen.cpp:78`, `:182`, `:349`, `:359`.

Holding the trackball for 3 s opens the SOS screen on top of the lock screen, which is intended. From there, these
routes reach the rest of the UI without the PIN:

- SOS (idle), then **"SOS contacts"**, opens `makePageScreen(PAGE_CONTACTS)`, the full contact list. From there:
  contact, "Send message", and the whole conversation history. You can also remove contacts, change favourites and
  ask for locations.
- SOS (active): tapping any contact row runs `app->openChat(...)`.
- Incoming SOS alarm: "Open chat" opens that chat. "M" opens the full map, then "D" opens Offline maps, which lets you
  change the Wi-Fi network and password and the tile URL / API key.

None of these routes check `App::locked()`.

**Fix:** when the device is locked, make `App::push()` / `openChat()` refuse every screen except Lock, SOS, the SOS
alarm and their confirm dialogs. Alternatively, hide those rows in `SosScreen` / `SosAlarmScreen` while
`app->locked()`. Starting, arming and cancelling an SOS must still work.

---

## 4. HIGH: the screen lock silences and hides an unacknowledged incoming SOS alarm

`firmware/src/ui/App.cpp:295` (`setPower(PWR_OFF)` calls `lockNow()`), `:407`, `:420`.

- An incoming SOS pushes `SosAlarmScreen`. It rings from `tick()` every 4 s.
- Only the screen on top of the stack gets `tick()`: `_stack.back()->tick()`.
- Showing the alarm does not reset the idle timer, and `!_sos.active()` only checks *my own* SOS. So the screen still
  dims and turns off after the screen timeout + 8 s, and then `lockNow()` pushes the lock screen *on top of the alarm*.

Before 0.26 the alarm kept ringing with the screen off. Now:

- ringing stops after about 23 s at the shortest timeout (at most about 5 min),
- waking the device shows the lock screen instead of the alarm, and
- a follow-up SOS update from the same sender no longer replaces the alarm, because `isSosAlarmFor` checks `back()`,
  which is now the lock. Alarms pile up underneath the lock.

**Device symptom:** someone's SOS goes quiet and out of sight if the user doesn't react within the screen timeout.

**Fix:** never turn the screen off, or never lock, while an SOS alarm is anywhere in the stack. For example, in
`lockNow()`, return early if any screen's `isSosAlarmFor` matches, or have the alarm screen refresh `_last_activity`
from `tick()`. Alternatively, insert the lock screen *below* the alarm.

---

## 5. MEDIUM: the download can get stuck in "Connecting" / "Downloading" forever, and Cancel does nothing

`firmware/src/device/TileDownloader.cpp:195` (`if (!_running) return;`), `:198`, `:203`, `:267`, `:289`, `:326`.

On a Wi-Fi failure ("network not found", "password rejected", "connection lost"), the task does three things in order:

1. sets `_net_failed = true`,
2. calls `stopWifi()`, which runs `esp_wifi_stop()` and `esp_wifi_deinit()` and takes tens of ms,
3. sets `_running = false`.

`loop()` returns at `if (!_running) return;` *before* it looks at `_net_failed`. If one loop-task pass (LVGL redraw,
SD write) takes longer than `stopWifi()`, the failure is never handled:

- `_st.state` stays `DL_CONNECTING` / `DL_RUNNING`,
- `cancel()` is a no-op because `_running` is false, and
- the Offline maps screen only offers "Cancel download".

Any results still in `_resq` are also never freed until the next start.

**Symptom:** Offline maps shows "Connecting to X..." forever after a wrong password or a missing network. A reboot is
needed before downloading again.

**Fix:** in `loop()`, handle `_net_failed` before the `_running` check. Also add a catch-all: if `!_running` while
`state` is `CONNECTING` or `RUNNING`, call
`finish(DL_FAILED, _net_msg[0] ? _net_msg : "Download stopped")`.

---

## 6. MEDIUM: leaving the Wi-Fi list during a scan leaves Wi-Fi switched on until reboot

`firmware/src/device/TileDownloader.cpp:53`, `:58-81`; `firmware/src/ui/screens/Maps.cpp:37-96` (`WifiScreen` has no
`onHide`).

Wi-Fi is switched off again only from `scanResults()`, and only the `WifiScreen` tick calls it. If the user backs out
before the scan finishes (it takes several seconds), the scan results are never collected:

- `_scanning` stays true,
- the radio stays on in STA mode,
- the scan result list is never freed, and
- about 40-50 KB of internal RAM stays in use by the Wi-Fi driver while BLE is running.

A later `start()` doesn't check `_scanning` either, so `WiFi.begin()` can run while a scan is still in progress. The
connect then fails and, after 20 s, reports "Couldn't connect to Wi-Fi".

**Symptom:** battery drain and less free heap after simply opening and closing the Wi-Fi picker.

**Fix:** add `WifiScreen::onHide()`, which calls a new `wifiScanCancel()`: `WiFi.scanDelete(); _scanning = false;` plus
`stopWifi()` if no download is running. In `start()`, also clear any pending scan.

---

## 7. LOW: PIN set in Settings is trimmed, PIN typed on the lock screen is not

`firmware/src/ui/screens/Common.cpp:182-183` versus `firmware/src/ui/screens/Lock.cpp:117`, `:95`.

`TextEntryScreen` strips leading and trailing spaces before calling back, so `setPin()` hashes the trimmed string. The
lock screen hashes exactly what was typed. A passphrase with a leading or trailing space ("PIN or passphrase" is
offered) can never be entered correctly, and the only way out is "Forgot PIN", which erases the device.

**Fix:** trim on the lock screen as well, or reject PINs that start or end with a space when setting one.

## 8. LOW: on the lock screen, location toasts still show names

`firmware/src/device/DeviceUI.cpp:184-190`.

The banner deliberately hides the sender while locked. The "contact shared location" toast still shows
`"<name>: 2.4 km NE"` over the lock screen.

**Fix:** skip the toast, or make it generic, while `app.locked()`.

## 9. LOW: muting a 17th group says "Group muted" but does nothing

`firmware/src/device/DeviceModel.cpp:813`.

The mute file holds 16 keys, but `MAX_GROUP_CHANNELS` is 40. `setChannelMuted` silently returns while the UI toasts
"Group muted". Mutes for groups left from the phone app are never removed, so they use up slots.

**Fix:** return a bool and show "Too many muted groups". Better, size the table to `MAX_GROUP_CHANNELS`. That changes
`sizeof(MuteFile)`; see item 11 before doing it.

## 10. LOW: wrong failure message when the SD card is full

`firmware/src/device/TileDownloader.cpp:209-218`.

`writeTile()` failures (card full, card removed) count towards the failure streak with `r.data != nullptr` and
`http == 200`. After 12 of them the message is "Tiles aren't 8-bit PNG: choose another source".

**Fix:** track write failures separately and report "SD card full or not writable".

## 11. LOW (latent): `safeLoad`'s legacy fallback reads a new-format file as legacy when the size doesn't match

`firmware/src/store/Storage.cpp:126-132`. This file is unchanged in this range, but the new callers depend on it.

If both copies fail `readCopy`, the legacy path opens `path`. Two cases cause this:

- the struct size changed, so `h.len != len`, or
- both copies are corrupt.

The legacy path checks only the first 4 bytes, the magic, which match the new `SafeHdr` too. It then loads `len` bytes
starting 4 bytes into the file, so `gen`, `len` and `sum` end up shifted into the start of the data. Nothing breaks
today, because `UiSettings` keeps its size (`pin_hash[4]` + `reserved[7]` replace `reserved[11]`) and the other files
are new. But any future size change to `UiSettings`, `WpFile`, `MuteFile` or `Cfg` would load garbage and return
success. For `UiSettings` that garbage can land in `pin_hash`, which locks the user out with an unknown PIN.

**Fix:** take the legacy path only when the file size is exactly `4 + len`.

---

## Checked and found correct

- **Tile cursor maths.** `tileRange` / `tilesInArea` / `nextTile`: the ranges are clamped and match the totals, the
  zoom roll-over is correct, and `done` adds up to `total`.
- **Download queue sizes.** 2 requests in flight, queues of 4, and the stop marker is cleared by `xQueueReset` on the
  next start.
- **Download buffers.** `ps_malloc` buffers are freed on every path; results that arrive after a cancel are drained
  once the state is no longer `RUNNING`.
- **Download URL.** The URL fits: at most 197 bytes in `char url[224]`.
- **`GpsTap`.** It is transparent to MeshCore:
  - all of MicroNMEA's reads go through the `Stream`, and `EnvironmentSensorManager` only calls `Serial1.available()`
    for detection;
  - the line buffer is bounded and overlong lines are dropped;
  - the checksum is verified;
  - empty fields are handled;
  - the GSV parse stays in bounds, including the NMEA 4.10 signal-ID field.
- **Ring animation in map previews.** LVGL 9.2 `obj_delete_core` calls `lv_anim_delete(obj, NULL)`, so the pulsing ring
  animation dies with its object. Packing the coordinates into `user_data` handles negative values.
- **`UiSettings` size.** Unchanged, so old files load and `pin_hash` reads as zero (lock off).
- **Echo counting.**
  - The hash is taken before the packet is queued.
  - Repeats are counted in `logRx` before the duplicate filter.
  - Group records are linked by the 4-byte hash in `ack`, and `findByAck` searches newest first.
  - The phone-send link re-reads the record, so the later `RESP_OK` update keeps the `ack`.
  - The `processAck` fallback only touches `ST_PENDING` records.
- **Waypoint delete index.** It is captured when the choice opens, and nothing can change the list while the choice
  dialog is on top.
- **Mute.** It is checked after the SOS branch in `App::onMessage`, so an SOS is never muted.
- **Lock against an SOS started from the device.** The trackball hold is handled before the lock screen sees the
  input, and an active SOS keeps the screen from turning off.
- **Lambdas that capture `this`** in Maps, Waypoints, MapScreen, Lock and Settings: each one targets a screen that is
  still underneath the popped dialog. The pop-then-callback pattern copies the callback first.
- **Emoji shortcodes, smileys and double space.** Only ASCII bytes are deleted, and the inserted text fits in
  `insert[12]` (at most 6 bytes). `send()` re-checks the byte limit, which is needed because a smiley grows by 2 bytes.

---

## Status (2026-11-20)

All 11 findings fixed in 0.28.0 (critical 1, high 2-4, medium 5-6, low 7-11). Regression scenario: sim/scenarios/lockbypass.txt.
