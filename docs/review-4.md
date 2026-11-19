# EasyDeck review 4: a9c8455..HEAD (firmware 0.37.0 to 0.41.0, Android 0.5.0)

Scope: `git diff a9c8455..HEAD -- firmware/src android/app/src ':!firmware/src/ui/fonts'`. That covers screenshots,
quick replies, the emoji picker, route ping (TRACE), contact auto-add settings, add-from-Heard and the Android people
radar. I read the touched files in full. I also checked the code they rely on: MeshCore (`lib/MeshCore`:
`Mesh.cpp`, `Packet.cpp`, `BaseChatMesh.cpp`, `companion_radio/MyMesh.cpp`), LVGL 9.2.2 (`.pio/libdeps`:
`lv_snapshot.c`, `lv_draw_buf.c`, `lv_mem.c`, `lv_display.c`) and Arduino-ESP32 2.0.17 (`vfs_api.cpp`). Nothing was
edited.

No crash-level bug turned up in the new firmware paths. The most important findings are a screenshot gesture that
easily turns into Back plus a hotkey, a route-ping result that shows up on the wrong contact, and a screenshot file
scan that gets slow enough to trip the watchdog.

---

## 1. MEDIUM: the screenshot gesture works only in the first 0.8 s of the hold, so a normal "hold" becomes Back plus a keypress

`firmware/src/hal/Input.cpp:87` (the gesture), `:132` (long press at `BTN_LONG_MS` = 800 ms)

```cpp
if (_btn_down && !_btn_long_fired) {   // key while the trackball is held: screenshot
```

The screenshot fires only if the key arrives before the long press has fired, which is 800 ms after the trackball
goes down. The help text says "hold the trackball and press any key". Most people press and hold first, then reach
for a key, which takes longer than 0.8 s. Then:

- At 800 ms, `IN_BACK` fires and the screen you wanted to capture is popped.
- `_btn_long_fired` is now true, so the key goes through as a normal `IN_CHAR` to whatever screen is now on top.
  On Home that is a hotkey (`x` opens SOS, `k` opens the I'm OK confirm, `a` opens Advertise). In a chat it types
  into the box.
- If the hold reaches 3 s, the SOS screen opens.
- Once a screenshot has fired, any further key while the ball is still held (a second key, or keyboard bounce) is
  also delivered as a normal character, because `_btn_long_fired` is true.
- The reverse case is a safety problem. A key bumped in the first 0.8 s of a deliberate 3 s SOS hold sets
  `_btn_sos_fired = true`, so that hold never opens SOS.

**Symptom:** "screenshots don't work". Instead the device goes back a screen and opens or types something random.
Occasionally an SOS hold silently does nothing.

**Fix:** treat every key that arrives while the ball is down as part of the gesture, and never pass it on as a
character. Before the long press: take the screenshot and set `_btn_long_fired`, as now. After `IN_BACK` has fired:
still take the screenshot (or drop the key), but never deliver the key. Leave `_btn_sos_fired` alone so an SOS hold
still works. To stop the Back from destroying the screen, either delay `IN_BACK` until release when a key could still
follow, or change the gesture to "press a key, then click the trackball" and update the Launcher help text.

## 2. MEDIUM: a route-ping result is global, so it appears on every contact's Route screen

`firmware/src/EasyMesh.cpp:446-486` (single `_trace` / `_trace_tag`), `firmware/src/ui/screens/Other.cpp:445-450`
(`fill()` shows `m->traceResult()` unconditionally), `:457-475` (`showResult` labels the links with this screen's
`_names`)

`TraceResult` doesn't record which contact was pinged. Ping contact A, go back, and open the Route screen of contact
B:

- B's screen shows "ROUND TRIP x s" and A's per-link SNRs, labelled with B's repeater names ("You > B-repeater
  6.5 dB good").
- If A's ping is still running, B shows "Pinging..." and then A's result.
- "NO ANSWER - A REPEATER MAY BE DOWN" from A's ping is shown on B too.

**Symptom:** wrong signal numbers, presented as measurements of this route. The user might blame or trust the wrong
repeater.

**Fix:** store the 6-byte key (and the hop count used) in `TraceResult` when `startTrace` runs. In `RouteScreen::fill`
and `tick`, ignore the result unless `memcmp(t.key, _key, LOG_KEY_LEN) == 0`. Also check that `t.hops == 2 *
_names.size()`, so a route that changed since the ping isn't mislabelled. The alternative is to reset the trace in
`RouteScreen::build()`.

## 3. MEDIUM-LOW: the screenshot file-number scan restarts at 1 on every boot and can freeze the loop past the watchdog

`firmware/src/ui/App.cpp:421-425`

```cpp
char path[48];
for (; _shot_no < 10000; _shot_no++) {
  snprintf(path, ...);
  if (!_m->fileExists(path)) break;
}
```

`_shot_no` starts at 1 after every boot. The first screenshot after a reboot therefore calls `SD.exists()` once per
screenshot already on the card, and nothing feeds the watchdog in this loop (only `writeFile` feeds it). In
Arduino-ESP32 2.0.17, `VFSImpl::exists` builds a `VFSFileImpl`: a `stat()` plus an `fopen()`/`fclose()`, which is two
FAT directory lookups. Each lookup scans `/screenshots` from the start, and every `easydeck-NNNN.bmp` takes 3
directory entries (LFN). The total cost is roughly N²/5 sector reads for N existing files.

Rough numbers (my estimate, not measured on hardware): about 1 to 2 s at 150 files, and 5 s or more at about 300
files. On top of that come the two snapshots and the BMP conversion. The loop watchdog is 5 s (`enableLoopWDT`).

The same loop has a second bug. If the scan reaches 10000 (all numbers taken, or `_shot_no` already ≥ 10000 after a
save), the loop body never runs. `path` is then used uninitialised by `fileExists`/`writeFile`, which is undefined
behaviour.

**Symptom:** after a few hundred screenshots, the first screenshot after every boot freezes the UI for seconds and
can reset the device.

**Fix:** call `feedLoopWDT()` inside the scan. Better, find the next number once: walk the folder with
`openNextFile()` and take the highest NNNN + 1, or persist `_shot_no` in `UiSettings`. Initialise `path` and refuse
politely when `_shot_no >= 10000`.

## 4. LOW-MEDIUM: the "X joined your contacts" toast can never show

`firmware/src/device/DeviceUI.cpp:153-155`

```cpp
bool added = easy_mesh.contactByKey(contact.id.pub_key) != nullptr;
if (!added) device_model.noteDiscovered(contact);
if (is_new && added && contact.type == ADV_TYPE_CHAT) {   // toast
```

In MeshCore, `is_new` means "not in contacts[]" (`BaseChatMesh.cpp:156`). It is `true` only on the three
"not added" paths (`:161`, `:170`, `:177`). When `onAdvertRecv` allocates a slot and adds the node, it calls
`onDiscoveredContact(*from, is_new=false, ...)` (`:200`), because `is_new` is never set to true there. So
`is_new && added` is always false, and the toast is dead code.

The old code (`is_new && type==CHAT`) was wrong the other way: it announced nodes that were *not* added. The
`noteDiscovered` branch, which keys on `added`, is correct.

**Symptom:** auto-added people never get the "joined your contacts" toast.

**Fix:** record whether the node was already a contact before MeshCore processes the advert. For example, override
`onAdvertRecv` in `EasyMesh`:

```cpp
_advert_known = contactByKey(id.pub_key) != nullptr;
BaseChatMesh::onAdvertRecv(...);
```

Then in DeviceUI use `bool newly_added = added && !easy_mesh.advertWasKnown();`.

## 5. LOW: add-from-Heard skips parts of MeshCore's normal add flow

`firmware/src/device/DeviceModel.cpp:977-988` (`addHeard`), `:961-969` (`noteDiscovered`)

`BaseChatMesh::addContact` (`BaseChatMesh.cpp:882`) only copies the struct into a free slot. Compared with the
auto-add path in `onAdvertRecv` and with the phone's add command, three things are missing:

1. **No duplicate check.** `canAddHeard` checks `contactByKey` when the row is tapped, but `addHeard` runs later, after
   the confirm dialog. If the node becomes a contact meanwhile (a second advert under a different hop count or mode,
   or the phone app adds it), the confirm adds a second copy with the same key. Fix: return true early in `addHeard`
   if `easy_mesh.contactByKey(key)` is already set.
2. **Stale `lastmod`.** The cached `ci.lastmod` is the time the advert was heard. The official MeshCore app syncs
   contacts incrementally (`CMD_GET_CONTACTS` with `since`; `MyMesh.cpp:2397` sends only `lastmod > since`). A contact
   heard before the app's last sync and added afterwards is never sent to the app, until that node adverts again.
   Fix: set `ci.lastmod = getRTCClock()->getCurrentTime()` before `addContact`.
3. **No advert blob.** The raw advert is saved only on the add path (`putBlobByKey`, `BaseChatMesh.cpp:190`), so a
   contact added from Heard has none. "Share contact" (`shareContactZeroHop`, which calls `getBlobByKey`) fails for
   that contact until its next advert. Fix: keep the raw advert in `Disc` (for example via an `onAdvertRecv` override)
   and `putBlobByKey` it in `addHeard`. Otherwise, document the limitation.

## 6. LOW: the emoji picker opens only when the message box is empty

`firmware/src/ui/screens/ChatScreen.cpp:372`, `:160-167`

The only way into the picker is Quick reply → "Emoji...", and Quick reply opens only on `IN_RIGHT` with an empty box.
So the "insert at the cursor" code and the "Message is full" check (`:163`) can never matter. An emoji can only start a
message. You can't add one to text you're already writing, which is what the hint "Emoji go into your message"
suggests.

The grid also shows three emoji twice, because it lists the shortcode table, which has aliases: `cool`/`sunglasses`,
`thumbsup`/`+1`, `thumbsdown`/`-1`.

**Fix:** give the picker a key that works while typing, for example `:` followed by Enter on an empty word, or a
dedicated key. Skip shortcodes whose code point already appeared earlier in the grid.

## 7. LOW: "Ping this route" never reaches the contact, but the screen suggests it does

`firmware/src/EasyMesh.cpp:455-458`, `firmware/src/ui/screens/Other.cpp:447`

`out_path` holds only the repeaters (the destination is implied), so the TRACE goes out to the last repeater and comes
back. That part is implemented correctly (see the checks below). But the screen lists the contact as the last stop
just above "Ping this route / every hop, both ways". A good result says nothing about the last link (repeater to
contact), which is the most common reason a DM fails.

**Fix:** change the subtitle to something like "every repeater, both ways (not the contact itself)". Optionally, when
the contact is a repeater or room, append its own hash so the ping does reach it.

## 8. LOW (Android): tapping a person on the radar crashes the app when no map app is installed

`android/app/src/main/java/com/easydeck/app/ui/People.kt:60` (new), also `:68` (the existing list row)

`ctx.startActivity(Intent(ACTION_VIEW, "geo:..."))` throws `ActivityNotFoundException` when nothing handles `geo:`.
That happens on de-Googled phones, or if Maps was disabled. The radar is advertised as offline, which makes those
users likelier to try it.

**Fix:** wrap the call in `try { ... } catch (e: ActivityNotFoundException) { toast("No map app installed") }`, or
check `intent.resolveActivity(ctx.packageManager)` first.

## 9. LOW (Android): radar taps can hit stale positions, and the centre never updates while connected

`android/app/src/main/java/com/easydeck/app/ui/People.kt:109-116`, `MeshRepo.kt:175`

- `placed` is `remember(pts, cLat, cLon)`, but the tap handler is `pointerInput(pts)`. When the centre changes and the
  contact positions don't, `placed` becomes a new list that the draw lambda fills. The tap coroutine isn't restarted
  and keeps the old list, frozen at the previous layout, so taps open the wrong person or nobody. Fix:
  `pointerInput(pts, cLat, cLon)` (or key it on `placed`). Alternatively, compute the positions during composition and
  read them through `rememberUpdatedState`.
- `self` (and with it the T-Deck's own lat/lon) is fetched only once per connection, in `appStart`. A GPS fix gained
  after connecting never moves the radar off "middle of the group". Fix: re-send `CMD_APP_START` periodically, or
  read `CMD_GET_SELF_TELEMETRY`, while the People map is open.

---

## Checked and found correct

- **TRACE path construction** (`EasyMesh::startTrace`):
  - `out_path` is ordered first hop first: `Mesh::onRecvPacket` matches `pkt->path[0]` and removes itself, and
    `sendDirect` copies the path as given.
  - `[H1..Hn, Hn-1..H1]` with n hashes is the right out-and-back list. Hash size decodes like
    `Packet::getPathHashSize()`, mapped to the TRACE flag (1→0, 2→1, 4→2; 3-byte routes refused).
  - `hops*2-1 <= 15` keeps `n*sz <= 60 = sizeof(path)`, and the payload is 9 + 60 < 184.
  - `createTrace(tag, 0, path_sz)` and `sendDirect` append the hashes to the payload and set `path_len = 0`.
- **Revisiting H1 on the way back works.** `Packet::calculatePacketHash` includes `path_len` for TRACE, so
  `wasSeen` doesn't drop the return trip. The originator `markSeen`s only the `path_len = 0` version.
- **`onTraceRecv` fires on the originator.** When the last hop (H1) retransmits with `path_len == n`,
  `offset = n << path_sz >= len` (`Mesh.cpp:55-57`), so every listener, including us, gets `onTraceRecv`. That branch
  doesn't check the seen table. A companion never forwards (`allowPacketForward` is false), so a 1-byte hash
  collision with us can't divert it. The tag filter drops other traces.
- **SNR indexing.** The `path_len` argument is the byte length of the hash list, so `links = len >> path_sz = n` is
  right for 1-, 2- and 4-byte hashes. The repeaters add one SNR byte each, so `pkt->path` holds `n` SNRs. `snr4[n]` is
  the final link, `hops = n + 1 = 2·hops_out`, and that matches `seq` (2·hops_out + 1 names). `snr4[16]` is enough for
  n ≤ 15.
  - `MyMesh::onTraceRecv` is still called first, so the phone app gets the push. A trace started from the phone is
    still delivered to it, and our tag check ignores it.
- **Timeout** `4000 + 2500·n` ms is generous compared with the direct retransmit delays. A late reply after "no
  answer" is ignored (`state != 1`).
- **Snapshot formats.**
  - XRGB8888 and ARGB8888 are both accepted by `lv_snapshot_take_to_draw_buf`, and both are rendered by the SW
    renderer (`LV_DRAW_SW_SUPPORT_*` default 1).
  - The code reads rows through `header.stride`, the byte order (B,G,R,A) matches `lv_color32_t`, and the top layer is
    straight (non-premultiplied) alpha, so the blend formula is right.
  - The BMP header (54 bytes, positive height = bottom-up, 24 bpp, rows padded to 4) is correct.
  - Allocation failure is graceful: `lv_draw_buf_create` and `lv_malloc` return NULL without asserting, and every
    path frees what it allocated.
  - The input runs outside `lv_timer_handler`, and the display flush is synchronous (no DMA), so offscreen rendering
    doesn't race the display or the shared SPI bus.
- **Screenshot memory and time.** Peak use is about 845 KB (2 × 307 KB + 230 KB), all from PSRAM through CLIB
  malloc. The SD write is chunked and feeds the watchdog. 230 KB at 20 MHz SPI takes a few hundred ms. Apart from
  finding 3, the call stays at or below about 1 s.
- **Screenshot while locked or during an SOS alarm.** It captures the lock screen or the alarm (the active screen).
  The key is consumed, so it neither types into the PIN nor acknowledges the alarm. While the screen is off, the first
  event only wakes the device. The alarm sound pauses briefly during the blocking write. That is cosmetic.
- **Lambda lifetimes.** `ChoiceScreen` and `EmojiScreen` copy their callback and `pop()` before calling it. The popped
  screen goes to the graveyard (freed 600 ms later from `App::loop`, never during the callback), and the captured
  `ChatScreen` / `HeardPage` stays below on the stack (pages are persistent). Pop followed by push in one pass is safe
  in LVGL 9.2 (`lv_screen_load_anim` completes the pending load and deletes the previous root). In `HeardPage`,
  `uint8_t k[6]` is captured by value as an array copy.
- **Quick reply indexes.** `kQ` matches `kQuick` exactly, `i == 0` is the emoji entry, `i - 1` is bounds-checked, and
  the picker starts on "Emoji...", so a stray press doesn't send. `shortcodeEmoji` writes at most 5 bytes into
  `char[8]`. Every grid emoji is in `emoji_24`, which is reached through the fallback chain `mod_f24`/`tac_f24` →
  `icons_24` → `emoji_24`.
- **Auto-add mapping** against `MyMesh::shouldAutoAddContactType`, `getAutoAddMaxHops` and
  `shouldOverwriteWhenFull`:
  - Mode 0 → `manual_add_contacts = 0` (add everything).
  - Mode 1 → CHAT.
  - Mode 2 → CHAT|REPEATER|ROOM.
  - Mode 3 → no type bits.
  - Hop index k maps to `max_hops = k`. Since `path count >= max_hops` is rejected, index 2 = "up to 1 hop" is
    right.
  - Prefs are saved through `savePrefs()`, and the fields are in `DataStore` (offsets 63, 87, 88).
  - `onDiscoveredContact` is called on all three not-added paths (type refused, too far, list full), so
    `noteDiscovered` sees every node that was turned away.
  - `addContact` → `allocateContactSlot` honours "replace oldest" and calls `onContactOverwrite` (blob delete plus
    phone push).
- **PSRAM Heard cache.** 16 × `ContactInfo` (about 3 KB) from `ps_calloc`, allocated lazily, null-checked, updated in
  place by full key, and the ring index wraps.
- **Route ping UI.** `_ping_row = _list.count()` before adding the row is correct, because headers aren't counted as
  rows. Clicking by touch, which rebuilds the list inside the row's own click event, is handled by LVGL 9's
  delete-during-event marking. `IC_RADAR`, `IC_SIGNAL` and `IC_REFRESH` are in the icon fonts.
- **Android SELF_INFO offsets** against `MyMesh.cpp:1085-1117`: code 0, type 1, tx 2, max tx 3, pubkey 4..35,
  lat i32 at 36, lon i32 at 40, then 44-47, freq 48, bw 52, sf 56, cr 57, name 58. `f.size >= 58` covers 44 bytes.
- **Android radar edge cases.**
  - A single point (or everyone at the centre) gives `far` = 0, coerced to 50 m. `bearing(p, p)` = `atan2(0, 0)` = 0,
    so there is no NaN.
  - An empty list returns before `maxOf`.
  - `pos` already drops (0,0).
  - Points beyond three rings are clamped to the outer ring.
  - `return@Column` from the inline `Column` lambda is supported by the Kotlin 2.x Compose compiler (groups are closed
    correctly), and the toggle can switch between the list and the map freely.
  - Mutating `placed` during draw is harmless, apart from finding 9: draw and tap both run on the main thread, and the
    list is not snapshot state, so it causes no recomposition loop.

---

## Status (2026-11-20)

All 9 findings fixed in firmware 0.42.0 / app 0.5.1. Gesture change: long-press Back now fires on release (>= 0.8 s), so a key pressed during the hold is a screenshot and holding for SOS no longer pops a screen at 0.8 s.
