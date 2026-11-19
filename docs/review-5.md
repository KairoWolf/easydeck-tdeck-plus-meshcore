# EasyDeck review 5: 6ac9140..HEAD (firmware 0.42.0 to 0.46.0)

Scope: `git diff 6ac9140..HEAD -- firmware/src sim/SimModel.h ':!firmware/src/ui/fonts'`. Note: `6ac9140` is an
amended copy of the 0.42 commit (it differs from `3952357` only in README.md), so that diff leaves out the 0.42
firmware changes. I reviewed those from `8564493..HEAD` instead: Input.cpp, EasyMesh.cpp/.h, App.cpp/.h, Other.cpp,
Emoji.cpp, DeviceModel addHeard, Model.h.

I read the touched files in full where it mattered. I also checked the code they depend on in MeshCore
(`BaseChatMesh.cpp` onAdvertRecv / sendRequest / PAYLOAD_TYPE_REQ handling, `companion_radio/MyMesh.cpp`
onContactRequest / onContactResponse / onDiscoveredContact, `EnvironmentSensorManager.cpp`). Nothing was edited.

I found no crash, use-after-free or memory corruption in the new paths. The top finding is a safety regression: a
3-second SOS hold while the screen is off now only wakes the screen.

---

## 1. HIGH: SOS hold does nothing when the screen is off (regression from the release-time Back)

`firmware/src/ui/App.cpp:333`, together with `firmware/src/hal/Input.cpp:132-140`

```cpp
if (_power == PWR_OFF) { setPower(PWR_ON); return; }   // first key only wakes
```

Before 0.42, a hold sent `IN_BACK` at 0.8 s and `IN_SOS_HOLD` at 3 s. With the screen off, the 0.8 s `IN_BACK` used
up the "first key only wakes" rule, and the 3 s event then opened SOS. Since 0.42, nothing is sent until release or
until 3 s. So with the screen off, `IN_SOS_HOLD` is now the first event, and `App::input` uses it only to wake the
screen and then returns.

The screen-off state is the normal state for a device in a pocket (default 30 s timeout, then dim, then off 8 s
later). That is exactly when someone will try the "hold 3 s from any screen" SOS from the help text.

- **Symptom:** you hold the trackball for 3 s with the screen off. The screen (or the lock screen) comes on, but SOS
  does not open. You have to hold for another 3 s.
- **Fix:**

  ```cpp
  if (_power == PWR_OFF) { setPower(PWR_ON); if (e.type != IN_SOS_HOLD) return; }
  ```

  The lock screen that `setPower(PWR_OFF)` pushed does not get in the way, because `SosScreen` is `lockSafe()`.

## 2. MEDIUM: an "I'm here:" post in a room moves the room server on everyone's map

`firmware/src/device/DeviceUI.cpp:119-121`

```cpp
if ((Sos::isLocText(text) || ... || strncmp(text, "I'm here:", 9) == 0) && Sos::parseLocation(text, la, lo)) {
  device_model.noteLocation(from.id.pub_key, la, lo);
```

Room-server posts arrive through `onMessageRecv` with `txt_type == 2`. In that case `from` is the room server and the
real author is in `easy_mesh.currentAuthor()` (the code a few lines above already uses that). The quick-reply menu
offers "I'm here (send my position)" in room chats as well, because `_key` is a direct key. So any member who uses it
gives the room server contact that member's position on every other EasyDeck in the room.

The same thing already happened with SOS, LOC and check-in posts in rooms. The new quick reply makes it much more
common. This is a DM-only path otherwise: channel messages go through `onChannelMessageRecv`, which never calls
`noteLocation`, so that side is fine.

- **Symptom:** the room server's map marker and its distance jump to wherever the last poster was. Opening a chat
  with the room shows a distance that has nothing to do with the room server.
- **Fix:** only record a location for real DMs: add `txt_type == 0 &&` to the condition. Optionally, for
  `txt_type == 2`, look up the author contact and record the location for that contact.

## 3. MEDIUM: `Sos::parseLocation` matches ordinary text, and it now runs on every chat bubble and drives "Show on map"

`firmware/src/ui/Sos.cpp:127`; used at `ChatScreen.cpp:344` (clickable bubbles) and `:239` (`newestPlace`)

```cpp
if (sscanf(p, "%lf,%lf", &a, &b) == 2 && a >= -90 && a <= 90 && b >= -180 && b <= 180 && strchr(p, '.')) {
```

- `%lf` accepts integers.
- `strchr(p, '.')` looks for a dot anywhere in the rest of the message, not inside the two numbers.

So "Room 5, 3.5", "back at 10, 20 mins." and "I have 2,3 left." all parse as coordinates. Before 0.46, this parser ran
only on SOS/LOC/check-in texts. Now it runs on every message in every chat, including untrusted group text.

The input itself is safe: the string is NUL-terminated, NaN and inf fail the range checks, and the input is short.

- **Symptom:**
  - Normal bubbles become tap targets that open the map in the ocean near (5, 3.5).
  - The quick-reply menu's top item, "Show on map  Them: 5.0000, 3.5000", points at nonsense, and it pushes every
    other item down by one row.
- **Fix:** require a decimal point inside each number, and ideally at least 2 decimals:

  ```cpp
  int n1 = 0, n2 = 0;
  if (sscanf(p, "%lf%n,%lf%n", &a, &n1, &b, &n2) == 2 &&
      memchr(p, '.', n1) && memchr(p + n1 + 1, '.', n2 - n1 - 1) && <range checks>)
  ```

  Even better, have `newestPlace` and the bubble hook only accept the known prefixes (`Spot:`, `I'm here:`, `SOS`,
  `LOC `, `OK check-in`).

## 4. LOW: after 50 taken numbers, a screenshot silently overwrites an existing file

`firmware/src/ui/App.cpp:427-431`

```cpp
for (int tries = 0; tries < 50 && _m->fileExists(path); tries++) { ... }
bool ok = _m->writeFile(path, bmp, size);   // SD.open(path, "w") truncates
```

If all 50 tries hit existing files, the loop ends with `path` pointing at an existing file, and `writeFile` replaces
it. That happens when `shot_no` is back at 0 while the card already has more than 50 screenshots: after a factory
reset, if the settings file fails `safeLoad`, or on the first boot of this build with a card used before 0.42. It also
happens after the counter wraps from 9999 to 1.

- **Symptom:** `easydeck-0051.bmp`, a screenshot from earlier, is replaced without warning. Later screenshots continue
  correctly from 52.
- **Fix:** if the file still exists after the loop, store `no` in `shot_no` (so the next press carries on scanning)
  and show "Screenshot folder busy, try again" instead of writing. Or write to a temporary name and only rename it
  over a free number.

## 5. LOW: "I'm here" (and the auto-locate gate) use the last known position, which can be hours old

`firmware/src/ui/screens/ChatScreen.cpp:208, 217`; `DeviceModel::status()` at `DeviceModel.cpp:416`

`st.gps_fix` is computed as `gps_enabled && (node_lat != 0 || node_lon != 0)`. MeshCore's
`EnvironmentSensorManager::loop` only ever overwrites `node_lat` when a fix is valid and never clears it (checked at
`EnvironmentSensorManager.cpp:945-950`). After you walk indoors or the GPS loses its fix, "I'm here" still sends the
old coordinates with nothing to show their age. In an emergency, "I'm here: <stale position>" is worse than "no fix".

`GpsDetail.valid` (fix younger than 5 s, `GpsTap.cpp:112`) is already used by the map's direction-of-travel line.

- **Fix:** gate "I'm here" on `gpsDetail().valid`. Alternatively, when `fix_age_s > 120`, append the age, e.g.
  "I'm here: ... (12 min ago)".

## 6. LOW: "Show on map" ignores places sent after the first 120 bubbles of a session

`firmware/src/ui/screens/ChatScreen.cpp:239-245` and `:385`

`newestPlace` walks `_seq[0.._n)`. `_seq` stops filling at `MAX_TRACK` (120): up to 50 bubbles from history plus
every new one. In a busy chat left open for a long time, newer bubbles are not tracked, so the newest place in
`_seq` is old.

- **Symptom:** the quick-reply menu offers an older place than the one just received. Tapping the new bubble itself
  still works, because `placeCb` uses the seq stored on the bubble.
- **Fix:** remember the seq of the newest place in `addBubble`, which already calls `parseLocation`, in a member
  such as `_place_seq`. Then `newestPlace` needs a single `getMessage`.

## 7. LOW: the "joined your contacts" toast now shows names on the lock screen

`firmware/src/device/DeviceUI.cpp:156-160`

Before 0.42 this toast could never fire (`is_new && added` is never true in MeshCore 1.18). The `advertWasKnown()`
fix brings it to life, but without the `!app.locked()` guard that the location toast at `:194` uses ("no names/places
on the lock screen"). With auto-add set to everyone in a busy mesh, the toast also keeps replacing more important
toasts.

- **Fix:** `if (!app.locked() && added && !easy_mesh.advertWasKnown() && contact.type == ADV_TYPE_CHAT)`.

## 8. LOW: the telemetry permission wording doesn't match what MeshCore does

`firmware/src/ui/screens/SettingsPage.cpp:149`; `DeviceModel.cpp:133`

- "Everyone: any node that has you as a contact" is wrong. MeshCore can only decrypt, and therefore answer, a request
  from a node that is in **your** contact list (`BaseChatMesh::onPeerDataRecv` searches `contacts[]`). Better wording:
  "anyone in your contacts". With auto-add off, that is a much smaller group than the text suggests.
- "Battery & status: People I choose = people you share your location with" is not quite right either.
  `setShareLocation(false)` clears only flag `0x04` and leaves the base flag `0x02` set. So people you stop sharing
  your location with can still read your battery. Either clear `0x02` too when unsharing (if nothing else uses it),
  or change the subtitle to "people you have shared your location with".

The permission logic itself is right. `setTelemetryMode` keeps base at least as open as location, which matches
`onContactRequest`: it replies only when `TELEM_PERM_BASE` passes. The `TM_*` values match `TELEM_MODE_*` (0/1/2),
`telemetryMode()` clamps the value 3 that the phone app could set, and the `order[]`/`names[]` mapping is correct.

## 9. LOW (hardening): `SosScreen::show()` doesn't reset the new `HeaderScreen::_sub`

`firmware/src/ui/screens/SosScreen.cpp:18-19` resets `_header` and `_content` after `lv_obj_clean(root)`, but not the
new `_sub`. Nothing calls `setSubtitle` on the SOS screen today, so nothing crashes. But any future call after a mode
change would write to a freed label. Add `_sub = nullptr;` there.

---

## Checked and found OK

- **Trackball gesture (`Input.cpp`):**
  - Every press now ends in exactly one of: select, back, SOS or screenshot.
  - Keys pressed during a hold are never typed.
  - Releasing after SOS or after a screenshot sends nothing.
  - Bounces shorter than 30 ms are ignored.
- **`IN_BACK` consumers:** nobody relied on the press-time event.
  - `App::input` pops unless the stack has one screen or the top is the lock screen.
  - On the lock screen and on Easy home, `IN_BACK` goes to `onInput`, which ignores it.
  - Setup (one screen) handles it as before.
  - The SOS arming screen ("any key aborts") no longer gets a stray Back on the way to SOS.
  - The only gap is item 1.
- **TraceResult key:** set on every path through `startTrace` (including the failure paths). `onTraceRecv` doesn't
  reset it. RouteScreen's `fill()` and `tick()` both filter by key. The sim does the same.
- **`UiSettings`:** the size is unchanged (`reserved[4]` became `shot_no[2]` plus `reserved[2]`, all `uint8_t`), so
  `safeLoad`'s size check still passes and old files read `shot_no = 0`, which maps to 1.
- **`EasyMesh::onAdvertRecv`:**
  - MyMesh doesn't override it, so the base call reaches `BaseChatMesh::onAdvertRecv`.
  - The flag is set before and reset after that call, and `onDiscoveredContact` runs inside it synchronously.
  - Loop-back imports go through the same path, and nothing re-enters it.
  - MeshCore really does pass `is_new=false` after auto-adding (`BaseChatMesh.cpp:157` is never set to true).
- **`addHeard`:** the duplicate check and the `lastmod` refresh are correct. `addContact` copies the contact.
- **Emoji dedupe:** `_codes` maps each cell to its shortcode in `select()` and in the pick path. n is small.
- **`HeaderScreen::_sub`:** it is null when there is no subtitle, and `setSubtitle` checks for null. ChatScreen always
  passes a subtitle. The Tools screens' own `_sub` member hides the base one, which is harmless.
- **`autoLocate`:**
  - `static recent[8]`: the lv_tick difference is wrap-safe. Choosing the oldest slot by raw value is wrong only
    after 49 days, and harmless then.
  - A slot is recorded only when sending succeeds.
  - It never floods (`hops == 0xFF` is excluded) and only asks people (type 1).
- **Sending a telemetry request on chat open: safe.**
  - `sendRequest` is fire-and-forget: no ACK, no outbox entry, no retries.
  - The reply's tag can't match the phone app's `pending_telemetry` / `pending_req` (both come from
    `getCurrentTimeUnique`). So it reaches `DeviceUI::onUnhandledResponse`, and the phone never sees replies that
    contain GPS.
  - A reply without GPS (location denied, battery allowed) is passed to a connected phone as an unsolicited
    `PUSH_CODE_BINARY_RESPONSE`. The official app ignores tags it doesn't know.
  - The one collision: if the phone app has a status request pending to the same contact, MyMesh's legacy
    `pending_status` match by pubkey prefix (MyMesh.cpp:724) takes the reply. That is rare for people contacts and
    was already possible before this change.
  - Cost:
    - MeshCore's default for telemetry is DENY, so most peers won't answer.
    - A peer that does answer but has no path back to you floods its reply across the mesh
      (`BaseChatMesh.cpp:326-327`). The 10-minute per-contact limit keeps that bounded.
- **Location toast suppression:** `chatOnTop` checks the top of the stack. When the screen is locked, the lock screen
  is on top and the existing `!locked()` check covers it.
- **MapScreen:**
  - `_head_pts` is a member array, and `render()` cleans `_view` (deleting the old line) before rewriting it.
  - Screen roots are deleted at pop or after the 200 ms animation, well before the screen object itself is freed at
    600 ms. So no `lv_line` outlives its points.
  - The `GpsDetail` new/delete is balanced.
  - The heading line is clipped where it goes past the left or top edge of its object. That is cosmetic only.
- **`sendSpot`:** `ChoiceScreen` pops itself and then calls the callback. The map screen underneath is still on the
  stack and alive, and `_targets` is rebuilt together with `items`. The lock-screen keys are guarded (`strchr` is safe
  here because `IN_CHAR` is never NUL).
- **Quick reply:**
  - The `i--` index arithmetic is right in all four combinations of have_place / can_here.
  - `Place` is captured by value, and `MapScreen` copies the label into `_plabel`.
- **`placeCb`:** `current_target` is the bubble the callback was attached to, and the seq is stored as user data.
  `this` stays valid because the bubble is deleted with the screen root.

---

## Status (2026-11-20)

All 9 findings fixed in firmware 0.47.0 (SOS hold wakes and opens SOS; locations only from plain direct messages; stricter coordinate parser needing 3+ decimals in both numbers; screenshot never overwrites; I'm here needs a live fix and SOS marks a stale position "(last fix)"; Show on map scans the conversation; joined toast hidden while locked; telemetry wording "All my contacts" and unsharing clears battery too; SosScreen resets _sub).
