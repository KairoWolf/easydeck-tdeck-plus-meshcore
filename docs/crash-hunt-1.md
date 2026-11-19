# Crash hunt 1: "Restarted after crash (panic)" after 0.53.1 / 0.54.0

Scope: `git diff d13eaa3..HEAD -- firmware/src firmware/include` (0.52.0, 0.53.1, 0.54.0), the code part of
`firmware/src/ui/fonts/pixemoji.c`, `tools/gen_emoji.py`, and the LVGL 9.2.2 / MeshCore 1.18 paths they reach.
Frame sizes come from `firmware/.elf-archive/EasyDeck-0.54.0.elf` (`entry a1, N`). That ELF is from a later
working-tree build (see 0.2), but the functions measured are the same as in the release.

## Bottom line

No line in the range panics every time, at boot or in normal use. These were traced and are memory-safe on the
device: the pixel-emoji image glyphs, the LogRecord carve-out, repeater capture, the Updater read-back, battery
smoothing, the header change and the Easy menu. The cleared items are listed at the end so nobody re-checks them.
The findings below are real defects. Two of them (F1, F4) can turn into a panic depending on build layout or data,
but none is proven to be *the* crash. The crash's own core dump is almost certainly still on the user's device.
Step 0 is the fastest way to the real answer.

## 0. Get the actual dump first (no code change needed)

0.1 **The dump is still in flash.** Arduino 2.0.17's sdkconfig has `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=1` and
`CONFIG_ESP_TASK_WDT_PANIC=1`, and the partition table has `coredump` at 0x680000. ESP-IDF writes a dump on
every panic. The released 0.53.1/0.54.0 never read it and never erase it: `Crash.cpp` and `crashCaptureAtBoot()`
are only in the uncommitted working tree. So the last panic's dump stays there until the next panic overwrites it.

- Over USB: `esptool.py --chip esp32s3 read_flash 0x680000 0x10000 core.bin`, then
  `espcoredump.py info_corefile --core core.bin --core-format raw <matching firmware.elf>`. First confirm the
  offset with `read_flash 0x8000 0xc00 pt.bin` + `gen_esp32part.py pt.bin`, because the Launcher writes its own
  table.
- Or flash a build that has `Crash.cpp`. It writes the summary and backtrace to `SD:/easydeck/crash.txt` on first
  boot. Caveat: that file says "EasyDeck <new version> crash" but the PCs belong to the **old** build. Match them
  by the `elf sha256` line.

0.2 **Use the right ELF.** `firmware/.elf-archive/EasyDeck-0.54.0.elf` (sha256 `09fabf53…`) does **not** match
`dist/EasyDeck-0.54.0-app.bin`. That bin embeds app_elf_sha256 `7629ab03aed2b327…`, and its `.flash.text`
differs from offset 8 on. It was built from the working tree (with `Crash.cpp`), not from tag f66b138. Rebuild
f66b138 (0.54.0) and 1c438a2 (0.53.1) with the same toolchain. Check the ELF sha against the GitHub release bin
and against the dump's sha before decoding.

0.3 **Reading the reset reason:**
- ESP_RST_PANIC means an exception or `abort()`: a failed assert, CORRUPT HEAP, the FreeRTOS stack-overflow
  canary, or a failed `new`.
- The task watchdog sets ESP_RST_TASK_WDT (verified: `task_wdt_isr` references `esp_reset_reason_set_hint`), so a
  plain freeze is *not* reported as "panic".
- The hint lives in an RTC register and survives a software restart. So a panic inside the bmorcelli Launcher
  right after it flashed EasyDeck would also show up as "Restarted after crash (panic)" on EasyDeck's first boot.
- If the dump's ELF sha is not EasyDeck's (or there is no dump), look at the Launcher, not this diff.

## Findings, most likely first

### F1. `ChatScreen::details()` reads past the end of `sts[]`. UB, can panic depending on layout (0.52+)
`firmware/src/ui/screens/ChatScreen.cpp:343-344`
```cpp
static const char* sts[] = { "Sending...", "Sent", "Delivered", "Not delivered" };   // 4 entries
... r.status <= ST_FAILED ? sts[r.status] : "?" ...                                   // ST_FAILED == 4
```
`LogStatus` is `NONE=0, PENDING=1, SENT=2, DELIVERED=3, FAILED=4`.
- **Mechanism:** Message, then Details, on an outgoing message that was **not delivered** reads `sts[4]`, one
  word past the array. That word is passed to `%s` in `snprintf`.
- **In 0.54.0:** `sts` is in `.dram0.data` at 0x3fc9adcc, and the next word happens to be `quickReply`'s `kQ[0]`,
  a valid string. So it prints "Status: OK 👍" and doesn't crash.
- **In any other build:** if the linker places something else there, it's a wild pointer, which means
  LoadProhibited (EXCCAUSE 28) inside `_svfprintf_r`.
- **Also wrong without the overflow:** every label is shifted by one. Pending shows "Sent", Sent shows
  "Delivered", Delivered shows "Not delivered".
- **ASan:** catches it only if a scenario opens Details on a failed message.
- **Fix:**
```cpp
static const char* sts[] = { "", "Sending...", "Sent", "Delivered", "Not delivered" };
const char* s = r.status < sizeof(sts) / sizeof(sts[0]) ? sts[r.status] : "?";
```

### F2. Little room left on the loop-task stack while drawing emoji (0.53+; the simulator can't see this)
The Arduino `loopTask` stack is 8192 B (`CONFIG_ARDUINO_LOOP_STACK_SIZE`). With `LV_USE_OS_NONE`, every draw task
runs synchronously inside `lv_draw_finalize_task_creation()`. That is at the bottom of the
`refr_obj`/`lv_obj_redraw` recursion, which costs 240 + 320 = **560 B per level of object nesting**.

Worst common case: an emoji in a chat bubble. The label is 6 levels deep (root, content, `_msgs`, wrap, bubble,
label).

| part | bytes |
|---|---|
| loopTask, loop, DeviceUI::loop, lv_timer_handler, lv_display_refr_timer, refr_area_part, refr_obj_and_children | ~400 |
| 6 levels × (refr_obj 240 + lv_obj_redraw 320) | 3360 |
| send_event 64, event_send_core 32, lv_label_event 224, lv_draw_label 32, finalize 32, dispatch 32+48+32, sw dispatch 32, lv_draw_sw_label 32, iterate_characters 288 | ~880 |
| **pixel emoji:** draw_letter_cb 160, lv_draw_sw_image 32, normal_helper 160, img_decode_and_draw 128, img_draw_core 288, sw_blend 128, blend_image_to_rgb565 80 | ~976 |
| **total** | **~5.6–5.8 KB** (about 6.4 KB for a 7-deep tree) |

The 0.51 Noto bitmap glyph path was draw_letter_cb, sw_blend and blend_color: about 370 B. The image-glyph path
adds roughly **+600 B on every emoji draw**. Add the about 256 B exception frame that FreeRTOS/Xtensa pushes onto
the task stack, and the headroom is around 1.5–2 KB. That is not a proven overflow, but it is thin. An overflow
would show up as `***ERROR*** A stack overflow in task loopTask` (abort, which is ESP_RST_PANIC) or as random
corruption. On the host the simulator has an 8 MB stack.
- **Fix:** in `main.cpp` (global scope), add `SET_LOOP_TASK_STACK_SIZE(12 * 1024);`. Internal RAM is tight for
  Wi-Fi, so prefer 12 KB over 16 KB.
- Also log `uxTaskGetStackHighWaterMark(NULL)` from `loop()` into the diagnostics screen, so the real margin can
  be measured on the device.

### F3. Six tile workers (0.54 only; panics only during map downloads)
`firmware/src/device/TileDownloader.cpp:26, 37-38, 267, 329-333, 412`

- **Memory (possible OOM panic):** the spawn loop checks free internal RAM once per spawn. That happens *before*
  any worker has connected, so all 6 pass the check. 6 × (10 KB stack + TCB) ≈ 62 KB internal. Then six parallel
  TLS handshakes and TCP windows (lwIP pbufs and Wi-Fi RX buffers are internal) all run at once. Internal RAM can
  hit zero mid-download. Allocation failures in Wi-Fi/lwIP/HW-crypto bounce buffers, or a failed `new`, end in
  `abort()`, which is a panic. 0.51.1 already had "watchdog restart during fast map downloads" in this area.
- **Queues (hangs, not panics):** `_reqq` and `_resq` are created with depth 4, but `IN_FLIGHT` is 8 and there
  are 6 workers.
  - (a) Line 267: `xQueueSend(_reqq, …, 0)` fails as soon as 4 requests are queued and workers haven't picked
    them up yet. The cursor has already moved on, so that tile is silently counted as "failed".
  - (b) Line 412: if `_resq` stays full for 2 s (the loop writes at most 2 tiles to SD per pass), the result is
    freed and dropped. `_in_flight` is never decremented, so `_cursor_done && _in_flight == 0` never becomes true
    and the download never reaches Done.
- **Fix:**
  - Go back to `MAX_WORKERS 4`, or re-check `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` inside each worker
    after its first successful GET, and exit the extra worker if it's low.
  - Create both queues with depth `IN_FLIGHT + MAX_WORKERS`.
  - On a failed result send, retry with `portMAX_DELAY` while `!_cancel` instead of dropping.

### F4. `messageRepeaters()` trusts `rpt_hsize`/`rpt_n` from storage (hardening; turns bad data into a stack smash)
`firmware/src/device/DeviceModel.cpp:~988-994`. Caller: `ChatScreen::hopNames`, `PathHop hops[8]` on the stack
(ChatScreen.cpp:279).
- `memcpy(p.hash, &r.rpt[i * r.rpt_hsize], r.rpt_hsize)` copies into `PathHop::hash[4]` with no check.
- All writers in this tree `memset` the record and `addRepeater()` keeps the fields within bounds, so normal data
  is fine. But a record with a bad byte there (SD corruption, a torn write, a future layout change) gives
  `rpt_hsize` up to 255. That overflows `hops[]` and the stack frame, and the return goes to a wild PC
  (InstrFetchProhibited / IllegalInstruction).
- `nameHop` then also `memcmp`s `hash_len` bytes.
- **Fix:** at the top of the function add
  `if (r.rpt_hsize < 1 || r.rpt_hsize > 3 || r.rpt_n * r.rpt_hsize > (int)sizeof(r.rpt)) return 0;`

### F5. The Easy menu rebuilds itself synchronously, including while one of its own actions is running (0.54 only, low)
`firmware/src/ui/screens/Easy.cpp:45, 72-75, 129-130`
- `_list.onActivate` calls `_acts[i]()` **by reference**. `fill()` does `_acts.clear()` (and `lv_obj_clean` on
  the rows). Today none of the actions reaches `fill()` synchronously: `openChat` leads to `markRead`, which only
  sends `appended=false`. But if one ever does, the `std::function` being executed is destroyed mid-call
  (use-after-free).
- `fill()` also runs on **every received advert** (`onContacts`, from `DeviceUI::onDiscoveredContact`) and every
  appended message, even while EasyHome is buried under other screens. Every other page sets `_dirty` and
  rebuilds in `tick()`.
- **Fix:**
  - `onActivate = [this](int i){ if (i >= 0 && i < (int)_acts.size()) { auto f = _acts[i]; f(); } };`
  - Make `onMessage`/`onContacts` set `_dirty = true`, and call `fill()` from `tick()`/`onShow()`.

### F6. Smaller items found on the way (not panics)
- `ChatScreen::messageMenu` sub-label: for outgoing channel messages, `path_len` holds the echo count, which is
  correct. Nothing to fix.
- The `Updater::check()` path can be started while a read-back verify is still running; `_verify` lives on.
  `promote()` then names the file `/EasyDeck--full.bin` (empty `_st.version`). Cosmetic. Guard `check()` with
  `if (_verify) return false;`.

## Checked and cleared

- **Pixel emoji (`pixemoji.c`, LVGL 9.2.2):**
  - The table is consistent: `cps[]` is sorted and unique, has 173 entries, and `tbl1`/`tbl2`/`tbl3` follow the
    same order. All 519 images have `stride == 2*w` and `data_size == stride*h + (stride/2)*h` (checked by script).
  - `draw_letter()` returns on `box_w==0 || box_h==0` *before* `lv_font_get_glyph_bitmap`, so VS16/ZWJ
    (format NONE, `gid.src` NULL) are never dereferenced.
  - The only caller of `get_glyph_bitmap` is `draw_letter()`. It allocates a draw buffer only for
    `NONE < format < IMAGE`, and passes the IMAGE pointer to `lv_draw_sw_image()`.
  - Nothing in the label, textarea or text-size code reads glyph bitmaps; they only use `adv_w`. The only
    writer to an `lv_font_t` is `lv_font_set_kerning`, which is never called, so `const` fonts in flash are fine.
  - `LV_DRAW_SW_SUPPORT_RGB565A8` defaults to 1. In any case the untransformed RGB565A8 branch of
    `img_draw_core` blends as RGB565 + A8 mask with `mask_stride = stride/2`, which matches the generator.
    Clipping against partial-render bands is handled in `lv_draw_sw_blend`.
  - The bin decoder uses the variable image in place: no copy, no cache entry, and no premultiply (the SW unit
    doesn't ask for it). lodepng's `info_cb` rejects it (bad PNG magic).
  - All `e*_d` arrays are 4-byte aligned in the ELF, and the ESP32-S3 handles unaligned loads in hardware
    anyway (`XCHAL_UNALIGNED_LOAD_HW 1`).
  - The fallback chain mod/tac → icons_N → pixemoji_N → emoji_N → NULL has no cycle.
- **LogRecord:**
  - `static_assert(sizeof == 256)` holds; `_pad` is now 9 bytes.
  - Every creator `memset`s the record (EasyMesh.cpp:31, DeviceUI.cpp:109/145, SyncInterface.cpp:90/114) and the
    MessageLog cache zeroes bad slots, so pre-0.52 records have `rpt_n == 0`.
  - `addRepeater` checks hsize 1..3, a consistent hsize, and capacity.
  - `logRx` and DeviceUI guard `n*hs <= MAX_PATH_SIZE`; hash size 4 is rejected.
  - `nameHop` iterates `getContactByIdx`, which is bounds-checked.
- **Message menu:**
  - `ChoiceScreen` copies `_cb` before `pop()`. The ChatScreen stays on the stack under the ChoiceScreen, and
    `replaceAll` removes both together.
  - `InfoScreen` copies the `details()` string.
  - `_bubble[]` entries are only read for indexes below `_n`, and bubbles are never deleted individually.
  - `PathHop::name` stays NUL-terminated.
- **Header change:** `setSubtitle` checks `_sub` for null, and nothing else touches `HeaderScreen::_sub` without
  a check.
- **Updater read-back:**
  - No double close, delete or free on any path: success, short file, mismatch, fail, or cancel during verify.
  - `_vsha` is a zero-initialised global; its block buffer is in internal RAM (DMA-capable). PSRAM input is
    handled by the IDF 4.4 SHA-DMA port, the same as the download hash that already worked.
  - `ps_malloc(16 KB)` per pass is freed every time.
- **Battery:** `percentFromMv` is bounded and integer math only.
- **Boot path:** nothing new runs at boot. `DeviceModel::begin`, `Updater::begin` and backup restore are
  unchanged in the range. The only new thing on screen at boot is EasyHome in easy mode, which uses a heap
  `vector<ConvSummary>(40)`, not the stack.
- **New stack frames are all modest:** `messageMenu` 464 B, its lambda 464 B, `hopNames` 336 B (PathHop[8] =
  240 B), `details` 288 B, `EasyHome::fill` 304 B, `DeviceUI::onChannelMessageRecv` 336 B, `EasyMesh::logRx`
  320 B.
- **Radio-receive path:** the worst case is a channel message, then a new bubble, then `lv_obj_update_layout`.
  That comes to about 5 KB.
