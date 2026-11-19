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
| 0.16-0.19 | Per-group region scope (NSW defaults); fixes for every finding of the firmware review; public GitHub repo | done |
| app 0.1-0.3 | Android: BLE + EDS full sync, notifications, foreground service, saved history, People tab, I'm OK, SOS banner | done |
| next | Hardware validation on the user's T-Deck Plus; Android people/map/SOS; rooms sync; power saving | in progress |
| 1.0 | Hardening, Launcher catalogue listing, public release | planned |

## Ripple parity checklist

From the Ripple user guide and changelogs (research notes, Nov 2026). Goal: every Ripple
feature, done better. `[x]` = EasyDeck has it.

**Messaging and contacts**
- [x] DMs with delivery state, retries, resend · [x] hashtag + private channels · [x] `$` jump to unread
- [x] Discover nearby repeaters · [x] packet log · [x] region scopes, per-group scope
- [ ] repeat count on own channel messages ("heard by N repeaters")
- [ ] per-channel alerts on/off, channel pause/resume · [ ] channel names up to 31 chars (check)
- [ ] A–Z jump in lists · [ ] contact tags + notes · [ ] clipboard copy/paste (coords, keys, messages)
- [ ] emoji shortcodes (`:thumbsup:`) · [ ] double-space inserts ". "
- [ ] path details / custom path / find path / direct (zero-hop) per contact
- [ ] auto-telemetry on opening a chat (distance + direction in the title)

**Radio and network**
- [x] presets incl. Australia (Mid), Narrow, Wide · [ ] off-grid client-repeat regions (433.0/869.495/918.0)
- [ ] "Scan Local" regions from repeaters → add region · [ ] Optimise screen (RX delay, interference threshold, AGC reset, double ACKs)
- [ ] telemetry permissions (Deny / Base / Base+GPS)

**Device**
- [x] themes (Tactical, Modern, Modern light) · [x] Easy mode (≈ Kid Mode) · [ ] Easy mode remote on/off from a parent
- [ ] PIN / phrase lock screen · [ ] owner info · [ ] calculator
- [ ] sound / vibration-LED patterns per event · [ ] brightness DAY/NIGHT presets · [ ] font size setting
- [x] repeater/room admin, CLI terminal (free in EasyDeck)

**GPS and maps**
- [x] offline tiles from SD (`/tiles/z/x/y.png`) · [x] GPS track · [x] SOS map previews
- [x] Wi-Fi tile download for your region (configurable source; OSM's own servers forbid bulk download) · [ ] auto-fetch missing tiles while browsing with Wi-Fi on
- [ ] GPS info screen with satellites/HDOP/altitude/speed · [ ] compass / heading arrow · [ ] direction triangle on the map
- [ ] waypoints (save, navigate-to with distance/bearing) · [ ] long-tap map → copy coordinates
