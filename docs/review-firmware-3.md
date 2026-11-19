# EasyDeck firmware review 3: 868938d..HEAD (0.29.0 to 0.35.0)

Scope: `git diff 868938d..HEAD -- firmware/src ':!firmware/src/ui/fonts'`. That covers message search, contact notes,
battery saver, route view, owner & medical info, tones and the Easy mode changes. I read the touched files in full and
checked the MeshCore (`lib/MeshCore`), LVGL 9.2 (`.pio/libdeps`) and Arduino-ESP32 2.0.17 / IDF 4.4
(`~/.platformio/packages/framework-arduinoespressif32`) sources that the new code relies on. Nothing was edited.

Severity reflects what happens on the device. Findings 2 and 3 break documented ESP-IDF requirements. The exact BLE
failure mode depends on the closed Bluetooth controller library, so it needs a hardware test, but the fix is cheap.

---

## 1. HIGH: owner info makes any PIN that starts with "i" impossible to type, so the only way in is to erase the device

`firmware/src/ui/screens/Lock.cpp:137` (and `:136` for `?`)

```cpp
if ((e.ch == 'i' || e.ch == 'I') && _pin.empty() && _has_info) { showInfo(); return; }
```

Settings accepts any PIN or passphrase of 4 or more characters (`SettingsPage.cpp:267-269`), so "iris2024" or
"Isla..." is valid. Once owner or medical info is saved, typing `i` or `I` as the first character on the lock screen
opens Emergency info instead of adding to the PIN. The PIN can never be entered. "? forgot PIN" offers only a full
erase.

**Symptom:** after filling in Owner & medical info, the next screen lock locks the user out. They lose identity,
contacts, groups and messages. The existing `?` shortcut has the same problem, but `?` is a rare first character.

**Fix:** open the info with an input that can't be part of a PIN, for example a trackball roll (`IN_UP`), which the
lock screen ignores today. Update the hint text to match. Do the same for "forgot PIN", or reject PINs that start with
a reserved character when the PIN is set.

## 2. HIGH (needs a hardware test): battery saver light-sleeps with the Bluetooth controller running and advertising

`firmware/src/main.cpp:133-137`, `firmware/src/device/DeviceUI.cpp:225-240`

`SerialBLEInterface::begin()` always runs `BLEDevice::init()`. With Bluetooth on, the default, it advertises whenever
no phone is connected. `canSleep()` only refuses to sleep while a phone is *connected*, so `esp_light_sleep_start()`
runs about once per second with the controller enabled and advertising. The prebuilt sdkconfig has
`# CONFIG_BT_CTRL_MODEM_SLEEP is not set` and `CONFIG_PM_ENABLE` off, so the controller is never told that the clocks
are about to stop.

ESP-IDF's sleep-mode docs require Wi-Fi and BT to be disabled (`esp_bluedroid_disable()` /
`esp_bt_controller_disable()`) before light or deep sleep. Upstream MeshCore follows this: `companion_radio/main.cpp:305-308`
sleeps only on NRF52, and the ESP32 `board.sleep()` is used only by the repeater and room-server builds, which have no
BLE.

**Symptom:** at best, advertising pauses while the device sleeps, which the info text already admits. Possible worse
outcomes:
- The controller's advertising schedule ends up broken after many sleeps. MeshCore restarts advertising only after a
  disconnect (`SerialBLEInterface.cpp:222-245`), so the phone could stay unable to connect even after the user wakes
  the device, until a reboot.
- A controller assert or panic. Overnight, that becomes a reboot loop.

**Fix:**
- Cheapest: only sleep when Bluetooth is off, `if (!device_model.ui().bt_off) return false;`, and say so in the info
  text.
- Otherwise: stop advertising when the saver engages (`bluetooth_interface.disable()`) and re-enable it when the
  screen turns on. Disabling is not the same as deinit, so this still leaves the controller enabled.

Either way, test before shipping: leave it overnight with BT on, then check that the phone reconnects and that
`esp_reset_reason()` shows no panic.

## 3. MEDIUM: plugged into a computer with no serial app open, the device sleeps and its USB port stops responding

`firmware/src/device/DeviceUI.cpp:232`, `firmware/src/main.cpp:33`

The T-Deck runs USB-Serial/JTAG (`ARDUINO_USB_MODE=1`), so `(bool)Serial` is `HWCDC::isCDC_Connected()`. That
returns true only after the host has polled the CDC IN endpoint (`HWCDC.cpp:116-125`). In practice that means an
application has opened the port. With the cable in a laptop just for charging, `isConnected()` is false and the
device light-sleeps. Light sleep gates the APB and USB PHY clocks, so the controller stops answering USB transactions.
ESP-IDF's USB-Serial/JTAG "sleep mode considerations" describe this: the host sees an unresponsive device and may
reset it or drop it.

**Symptom:**
- On the computer, the device repeatedly vanishes and re-enumerates, and Windows may play the connect/disconnect
  sounds.
- The MeshCore app or a serial monitor can't open the port.
- esptool can't auto-reset the board into the bootloader until the user wakes the screen and works inside the
  15 s window.

**Fix:** add `if (HWCDC::isPlugged()) return false;` to `canSleep()`. `isPlugged()` is true while the host sends
start-of-frame packets, even when no app has the port open, and false on a plain charger, so the saver still works on
a wall charger.

## 4. MEDIUM: every wrong PIN now plays the full SOS siren, even with sounds off

`firmware/src/ui/screens/Lock.cpp:124`, `firmware/src/device/DeviceModel.cpp:442`

`submit()` still calls `m->playSound(SOUND_ALERT)` on a wrong PIN. Before this range, `SOUND_ALERT` was a chime and
only when sounds were on. Now it is `TONE_ALARM`, which ignores the Sounds setting: 8 notes × 180 ms, about 1.4 s of
the loudest pattern.

**Symptom:** a mistyped PIN, or a child poking the keyboard, sounds the same siren as a real SOS. That happens in
meetings and at night, and it trains the people nearby to ignore the real one.

**Fix:** play `SOUND_CLICK` (or a new "error" tone that respects `_ui.sound`) in `Lock.cpp:124`. Keep `SOUND_ALERT`
for SOS only.

## 5. MEDIUM: contact notes can never be deleted, and the 64-note limit fills up for good

`firmware/src/ui/screens/Other.cpp:116` (prompt says "Empty it to delete"), `firmware/src/ui/screens/Common.cpp:180`,
`:186`, `firmware/src/device/DeviceModel.cpp:856`

`TextEntryScreen` ignores Enter when the trimmed text is empty (`if (t.empty()) return;`), and Backspace on an empty
box cancels (`app->pop()`). So the callback never receives `""`, and the "clear" branch of `setContactNote` can't be
reached. Typing only spaces doesn't help either, because they are trimmed.

**Symptom:**
- A note can be changed but never removed. Following the on-screen instruction (empty it, press Enter) does nothing.
- Notes of removed contacts also stay in the 64-entry file forever and can't be listed. After enough churn, every new
  note fails with "Too many notes".
- Owner & medical info has a related usability issue: clearing needs "-". That hint is only the placeholder, which
  never shows because the box is pre-filled.

**Fix:** add an `allow_empty` flag to `TextEntryScreen` and set it for the note entry, or accept "-" as owner info
does and say so in the prompt. Delete a contact's note in `removeContact`.

## 6. LOW-MEDIUM: battery saver can sleep while the map-download task still has Wi-Fi on

`firmware/src/device/DeviceUI.cpp:233-234`, `firmware/src/device/TileDownloader.cpp:134-139`, `:345-347`

`canSleep()` checks only `state` (CONNECTING or RUNNING). `finish()` sets `state` to DONE, FAILED or CANCELLED at
once. The network task keeps running until its current `http.GET()` returns, which can take up to the 15 s timeout
plus TLS, and only then calls `stopWifi()` and clears `_running`. A download can run with the screen already off,
since `_off_since` started long before. When it ends or fails, the next pass sleeps immediately while Wi-Fi is
associated and the task on core 0 is mid-request. Same IDF rule as finding 2.

**Fix:** expose `_running` and `_scanning` and refuse to sleep while either is true.

## 7. LOW-MEDIUM: with GPS on, battery saver leaves the GPS powered but unable to receive, so the clock may never sync after boot

`firmware/src/device/DeviceUI.cpp:238`

Sleep is refused only when GPS is needed for a track or for sharing. Otherwise the UART is clock-gated for about
99.9 % of each second, so NMEA sentences arrive as fragments and fail their checksum. Results:
- no fix or position updates, so adverts carry a stale location;
- the GPS keeps drawing about 20-30 mA for nothing, which is likely more than everything else the saver saves;
- if the clock wasn't trusted before the saver engaged (no phone, GPS cold start takes longer than
  timeout + 8 s + 15 s), `clockTrusted()` stays false. `App::autoAdvert()` then never sends the boot advert or the
  periodic ones until the user wakes the device long enough.

**Fix:** stay awake while `gps_enabled && (!gps_fix || !clockTrusted())`. Once there is a time fix, either keep
sleeping and accept stale fixes, or put the GPS into standby while asleep.

## 8. LOW: Emergency info can't be opened during the wrong-PIN wait

`firmware/src/ui/screens/Lock.cpp:133`

`if (_wait_until) return;` comes before the `I` handler. After a few wrong guesses by the finder or a bystander, a
first responder gets no medical info for 30 s, rising to 8 min after repeated rounds.

**Fix:** handle the info key (see finding 1) before the `_wait_until` check.

## 9. LOW: the new confirm default makes some confirmations a single double-press

`firmware/src/ui/screens/Common.cpp:244`

`danger=false` confirms now start on the action instead of Cancel. That suits "I'm OK", but it also changes:
- "Open Launcher" (`SettingsPage.cpp:251`): a stray double-press reboots out of EasyDeck.
- The Easy-mode "Open the settings?" step (`Easy.cpp:118`): it exists "so little fingers don't change things by
  accident", but a double tap now goes straight through.

**Fix:** pass `danger=true` for these two, or add a separate "default to Cancel" parameter.

## 10. LOW: starting your own SOS now always sounds the siren

`firmware/src/ui/screens/SosScreen.cpp:236`, `firmware/src/device/DeviceModel.cpp:442`

Starting your own SOS, after the 10 s countdown, now forces a 1.4 s siren even with sounds off. That is right for an
*incoming* SOS. For the sender it removes any way to send a silent SOS, for example when hiding from a threat, and
before this range it respected the setting.

**Fix:** keep the forced siren for incoming alarms (`SosAlarmScreen`) only. For your own start, use the click or
respect `_ui.sound`.

## 11. LOW: the notes buffer is a permanent 4 KB of internal RAM

`firmware/src/device/DeviceModel.cpp:839`

`calloc(1, sizeof(NoteFile))` (4100 B) is never freed. With `SPIRAM_MALLOC_ALWAYSINTERNAL=4096`, a 4100 B block goes
to PSRAM only by a few bytes, and any future shrink of the struct would move it into internal RAM. Internal RAM is
what decides whether a map download starts ("Not enough memory: turn Bluetooth off").

**Fix:** use `ps_calloc` explicitly.

---

## Checked and found correct

- **A_NOTE pop and re-push** (`Other.cpp:111-121`):
  - `TextEntryScreen` copies its callback and pops itself (graveyard, not freed). Inside the callback, `this` (the
    ContactScreen) is still alive when it reads `m` and `app`, then pops itself (also graveyard) and pushes a new
    ContactScreen.
  - `k` is captured by value. `reap()` runs only from `App::loop`, so nothing is freed during the callback.
  - LVGL 9.2 `lv_screen_load_anim` (`lv_display.c:598-611`) completes the first pending load. It deletes the
    TextEntry root (`del_prev`), and the push deletes the old Contact root, so there is no leak and no double free.
- **InfoScreen marked lockSafe.** All 12 `makeInfoScreen` call sites run from input handlers or dialog callbacks on
  screens that are not lock-safe. None of them is reachable from Lock, Sos, SosAlarm or the fixed Map, and nothing
  pushes an InfoScreen from `onMessage`, `tick` or other async paths. The emergency InfoScreen only pops itself. No
  new bypass.
- **`UiSettings` size.** 3 new bytes plus `reserved[4]` replace `reserved[7]`: same size, and old files load with the
  new fields at 0.
- **Buffers:**
  - `showInfo` `t[220]`: at most 208 bytes.
  - Lock owner line `b[96]`.
  - Route `hx[12]`: at most 8 hex characters.
  - Search `snip[168]`: 3 + 160 + NUL.
  - `contactNote` / `setContactNote`: termination is correct.
  - Owner fields: `snprintf` with the field size, and they are re-terminated on load.
  - `ChoiceItem` copies the strings from the local `OwnerInfo`.
- **Route view.** It decodes `out_path_len` exactly like `Packet::getPathHashCount/Size`. `OUT_PATH_UNKNOWN` (0xFF)
  is tested first, and length is bounded by `MAX_PATH_SIZE`. Contact iteration starts at `MAX_ANON_CONTACTS`, the same
  as `startContactsIterator`.
- **Search.** The loop bounds match the other cache scans. It returns at most 60 rows, and row indexes stay aligned
  with `_hits`. A worst-case scan of 1500 cached records takes tens of milliseconds, nowhere near the watchdog.
- **Battery saver and the radio:**
  - Light sleep wakes on DIO1 at high level, and `ESP32Board::sleep` skips sleep when DIO1 is already high.
  - After waking, the latched edge sets `state |= STATE_INT_READY`.
  - DIO1 stays high until `readData` or `finishTransmit` clears the IRQ, so a pending packet always prevents sleep.
  - Within one `Dispatcher::loop`, TX completion puts the radio back into RX before the end of the pass.
  - Queued and scheduled TX (including ACKs) keeps the device awake through `getOutboundTotal()`. A TX in progress
    wakes the device on TxDone.
  - `rx_delay_base` defaults to 0, so nothing waits in the inbound queue.
  - The EasyMesh outbox (retries) and SOS keep the device awake.
- **Battery saver and timers.** `lv_tick` and every app timer use `millis()`, which IDF advances across light sleep.
  MeshCore timers use `millis()`. A 1 s sleep is well under the 5 s loop watchdog, and the watchdog timer pauses during
  sleep. Room re-login, auto-advert and contact lazy-save run at 1 s granularity.
- **GPIO0 wake versus the trackball.** `gpio_wakeup_enable`/`disable` change only `int_type` and `wakeup_enable`
  (plus the RTC-IO equivalent on GPIO0). GPIO0 has no interrupt attached and is polled with `digitalRead`, so polling,
  long press and the 3 s SOS hold keep working. A held button makes the sleep return immediately.
- **I2S.** `busy()` covers queued notes. The last ≤96 ms already in DMA may be cut short by a sleep, which is
  inaudible in practice because the screen is already off.
- **Tones.** `TONE_ALARM` queues exactly 8 notes, matching `_notes[8]`. `tone` and `group_tone` are taken modulo
  their counts.
- **Easy home.** It uses a 3×2 grid with `N`/`COLS` bounds in `select` and navigation. All new icons (`IC_OK_CIRCLE`,
  `IC_MAP`, `IC_LIFEBUOY`, `IC_BELL`, `IC_HASH`, `IC_BATT`, `IC_EDIT`, `IC_NAV`, `IC_ANTENNA`, `IC_SEARCH`) are present
  in every `icons_*` font. The lambdas that capture `this` target EasyHome, which stays under the dialog.
- **Lock shake.** `_field`'s `user_data` is otherwise unused. LVGL deletes the animation with the object, so the
  completed callback never runs on a freed field.
- **Cancel SOS.** It now starts on Cancel (`danger=true`) as intended.

---

## Status (2026-11-20)

All 11 findings fixed in 0.36.0. Battery saver now only sleeps with Bluetooth off and no USB cable (finding 2 needs a hardware check of its own). Regression scenarios: owner.txt (trackball-up info), notedel.txt.
