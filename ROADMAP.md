# EasyDeck roadmap

EasyDeck is a clean, modern, open-source MeshCore firmware for the LilyGO T-Deck Plus —
an alternative to Ripple and MeshOS. v1 is tuned for the **MidCoast NSW** mesh
(preset *Australia (Mid)*, scope `au-nsw-tro`), but works anywhere MeshCore does.

Pillars:

1. **Messaging** — fast, beautiful chats; 100% compatible with MeshCore + official apps.
2. **Friends & family tracking** — see where your people are (list, distance/bearing, offline map).
3. **Safety (Garmin inReach replacement)** — SOS with GPS position, "I'm OK" check-ins,
   periodic tracking to trusted contacts.
4. **For everyone** — modern dark/light UI by default; **Easy mode** in Settings
   (huge text, 4 big buttons) for elderly or non-technical users.
5. **Phone sync** — EasyDeck Android app over Bluetooth with identical history both ways.

## Phases

| Version | Content | Status |
|---|---|---|
| 0.1-0.4 | MeshCore core on SD Launcher, message log, sync protocol, first UIs, SOS | done |
| 0.5 | Tactical (default) + Modern styles, Barlow/Ubuntu fonts, Material Design Icons | done |
| 0.6-0.7 | Offline map (SD tiles), GPS track, packet log, auto advert, Discover | done |
| 0.8-0.10 | Free repeater/room admin + CLI, watchdog, private groups, Garmin-style location sharing | done |
| 0.11-0.13 | Emoji, room authors, setup file (fleet provisioning), SOS hold-from-anywhere | done |
| 0.14-0.15 | Hardware self-test checklist, USB/BLE state fix, auto re-join rooms | done |
| 0.20-0.21 | SOS map previews; Offline maps: Wi-Fi region download to SD, Wi-Fi picker, tile sources | done |
| 0.22-0.28 | GPS info + sky view, saved places, emoji shortcodes, heard-by-N echoes, screen lock, group mute; second review fixed (docs/review-firmware-2.md) | done |
| 0.16-0.19 | Per-group region scope (NSW defaults); fixes for every finding of the firmware review; public GitHub repo | done |
| app 0.1-0.5 | Android: BLE + EDS full sync, notifications, foreground service, saved history, People tab + offline radar map, I'm OK, SOS banner, repeat count + hops | done |
| next | Hardware validation on the user's T-Deck Plus; Android people/map/SOS; rooms sync; power saving | in progress |
| 1.0 | Hardening, Launcher catalogue listing, public release | planned |

## Ripple parity checklist

From the Ripple user guide and changelogs (research notes, Nov 2026). Goal: every Ripple
feature, done better. `[x]` = EasyDeck has it.

**Messaging and contacts**
- [x] DMs with delivery state, retries, resend · [x] hashtag + private channels · [x] `$` jump to unread
- [x] Discover nearby repeaters · [x] packet log · [x] region scopes, per-group scope
- [x] repeat count on own channel messages ("heard by N repeaters") + flood-advert echo report (Signal screen, toast)
- [x] per-channel alerts on/off (mute; SOS always alarms) · [ ] channel pause/resume · [ ] channel names up to 31 chars (check)
- [x] message search (/ in Chats: text + names, newest first)
- [x] A–Z jump in lists · [x] contact notes (tags: not planned, notes cover it) · [ ] clipboard copy/paste (coords, keys, messages)
- [x] emoji shortcodes (`:thumbsup:`, 97 codes + ASCII smileys) · [x] double-space inserts ". "
- [x] path details (Route: repeaters by name + hash) · [ ] custom path / find path / direct (zero-hop) per contact
- [x] auto-telemetry on opening a chat (distance + direction in the title; known route only, max once per 10 min)

**Radio and network**
- [x] contact auto-add settings (everyone / people / people + repeaters / manual; hop limit; replace oldest) + add from Heard
- [x] presets incl. Australia (Mid), Narrow, Wide · [ ] off-grid client-repeat regions (433.0/869.495/918.0)
- [ ] "Scan Local" regions from repeaters → add region · [ ] Optimise screen (RX delay, interference threshold, AGC reset, double ACKs)
- [x] settings packs (packs/ on GitHub + built in; setup More options; MidCoast NSW first) · per-channel region scopes only (global scope removed, migrated) · groups renamed channels · new contacts default: only when I add
- [x] power: loop idles between frames, keyboard light only while typing, Power use (Normal / Battery saver / Maximum), Desk mode (charger / not moving, GPS travel detection) · [ ] GPS standby commands (L76K / u-blox) - no GPS power pin on the T-Deck Plus
- [x] maps: 2 parallel tile connections, no artificial delay, SD-space check instead of a 40k-tile cap, 200/300 km areas, tools/get_tiles.py (PC), docs/tile-proxy.md · [x] nah.rocks mirror for tiles + updates (mesh.nah.rocks/easydeck; device sends its own key; health check, falls back to direct)
- [x] Full pixel emoji set (1976 incl. flags/ZWJ/skin tones) - names and messages show them (sequences packed to private-use codes for display, unpacked on send); picker with groups + name search
- [ ] Visible emoji button/hint in the compose bar (user didn't find the picker)
- [x] Logo: a simple handheld radio, amber line art on black (matches the Tactical UI; replaced the mesh-E at the user's request) - boot splash, setup, README, GitHub social preview (docs/logo, tools/gen_logo.py)
- [x] Colours: 8 accent presets for any style + custom theme file (/easydeck/theme.txt, written with the current colours as a starting point)
- [x] Settings clean-up: 10 sections (You & safety, Radio, Contacts & channels, Privacy, Display, Power, Sound & typing, Phone & GPS, Maps & Wi-Fi, System) with a summary each, instead of one 43-row list
- [x] Easy menu redesign (user: Easy mode was harder): one vertical list, favourites first (1 press to their chat, unread counts), Messages, I'm OK, Map, SOS, Full menu; same letter keys; big title-only headers; named 'Easy menu' (setup: 'Your home screen')
- [x] map downloads: 6 workers via the mirror (now 8 conns/IP, upstream keep-alive) · [ ] server prefetch (waiting for owner's OK)
- [x] pixel-art emoji everywhere (SerenityOS set, BSD-2, LVGL image glyphs 1x/2x/3x, Noto fallback) + 3x emoji picker
- [x] update safety: the file is read back from the SD card and hashed before it's offered (0.52 lost its last 32 bytes on the card -> Launcher boot loop)
- [x] chat: select a message (roll up / touch) -> reply @[name], quote, details (time, status, route, which repeaters re-sent it), show on map, send again
- [x] RADIO section: preset or custom freq/BW/SF/CR, transmit power, path hash size 1/2/3 bytes (MidCoast pack: 2) · [ ] remaining MeshCore companion prefs (multi-acks, rx delay, airtime factor, telemetry env...)
- [x] time: internet time whenever Wi-Fi is on, mesh time from >= 3 agreeing repeaters, NSW/VIC/ACT/TAS time zone with automatic daylight saving, clock source shown
- [x] HTTPS memory (mbedTLS) in PSRAM (fix for 'no secure connection'); map mirror: up to 4 connections + 429 back-off
- [x] Wi-Fi mode: restart without Bluetooth when Wi-Fi needs the RAM; LVGL objects moved to PSRAM
- [ ] push-to-talk voice messages (Codec2 700-1200 bps, known-route DMs only, length cap + cool-down) + PC encode/decode tool
- [x] software update over Wi-Fi from GitHub releases (check → download to SD, SHA-256 checked → SD Launcher installs)
- [x] automatic SD backup of identity/contacts/groups/settings + auto-restore after a reinstall (Settings > Back up to SD card)
- [x] telemetry permissions: "Who can locate me" + "Battery & status" (People I choose / Everyone / Nobody), per contact "Can see my location"

**Device**
- [x] themes (Tactical, Modern, Modern light) · [x] Easy mode (≈ Kid Mode) · [x] Easy mode: 6 tiles (Messages, People, I'm OK, Map, SOS, Settings behind a confirm) · [ ] Easy mode remote on/off from a parent
- [x] PIN / phrase lock screen (SOS still reachable, no previews while locked) · [x] owner + medical info (readable from the lock screen) · [ ] calculator
- [x] battery saver (light sleep between packets, opt-in; MeshCore ESP32Board::sleep + trackball wake)
- [x] message tones (chime / double beep / soft ping), quieter group option, SOS siren always audible · [ ] vibration (no motor on T-Deck) · [ ] brightness DAY/NIGHT presets · [ ] font size setting
- [x] repeater/room admin, CLI terminal (free in EasyDeck)

**Support**
- [x] screenshots to SD (hold trackball + any key -> /screenshots/easydeck-NNNN.bmp)

**GPS and maps**
- [x] offline tiles from SD (`/tiles/z/x/y.png`) · [x] GPS track · [x] SOS map previews
- [x] Wi-Fi tile download for your region (configurable source; OSM's own servers forbid bulk download) · [ ] auto-fetch missing tiles while browsing with Wi-Fi on
- [x] GPS info screen: satellites (sky plot + signal bars), accuracy, altitude, speed, heading · [x] direction-of-travel pointer on the map (GPS course; no magnetometer)
- [x] waypoints (save here / save map spot, navigate-to with arrow, distance, ETA; find a contact) · [x] crosshair coordinates + distance from you; S sends the spot to a favourite/group (no clipboard on the T-Deck) · [x] open coordinates from a chat message on the map (quick reply 'Show on map' or touch the bubble); 'I'm here' quick reply

## Other T-Deck firmwares (checked 2026-11-20)

- **CAIRN** (cvhviz/CAIRN, Cairn 137, 19 Nov 2026; T-Deck/Plus and many boards): in-message cursor editing
  (EasyDeck has it: roll left/right), emoji-reaction chips from the phone app, wallpapers, Wi-Fi OTA updates
  with automatic rollback (needs two app slots - our partition table has one; the SD Launcher handles updates).
- **Meck** (pelgraine/Meck, v1.12.6-1.15; T-Deck Pro/Max): tile home, canned messages (EasyDeck: quick replies),
  device-side DM retries (EasyDeck has them), trace route up to 16 hops, emoji picker, e-book reader, games.
- Neither has an SOS system, saved places/navigation, Wi-Fi region map download or a screen lock with
  emergency info like EasyDeck.
- [x] quick replies (roll right on an empty message box) · [x] emoji picker (Emoji... in quick replies; shows the shortcode) · [x] route ping (TRACE out and back, SNR per hop, round-trip time) · [ ] reactions
