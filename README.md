# EasyDeck — MeshCore firmware for the LilyGO T-Deck Plus

> **Beta — not yet tested on real hardware.** Builds cleanly and every screen is tested in the
> desktop simulator, but please treat it as experimental until the first hardware reports are in.
> Bug reports and photos of *Settings → Device check* are very welcome.

![EasyDeck screens](docs/screenshots/overview.png)

Open-source MeshCore firmware for the **LilyGO T-Deck Plus** — a free alternative to
Ripple and MeshOS. Off-grid messaging, friends & family tracking, offline maps and a
proper SOS system, in a fast keyboard-first UI. Tuned for Australian meshes (default
preset **Australia (Mid)**), works anywhere MeshCore does.

## Features

**Messaging**
- Direct chats with delivery ticks, automatic retries and resend; public `#hashtag` groups and
  private groups with secret keys; room servers (re-joined automatically after a reboot).
- "Heard by N repeaters" on your group messages; **message search** (`/`), unread jump (`$`).
- **Quick replies** and an **emoji picker** (roll the trackball right in a chat), emoji
  shortcodes like `:thumbsup:`, two spaces for a full stop.
- Per-group **mute** and **region scope** (NSW Mesh defaults when joining suggested groups),
  message tones with a quieter option for groups.

**People & location**
- Contacts with distance/direction, favourites, **private notes**, locate-a-friend, share contact.
- **Route view** (which repeaters reach someone) and **route ping** (signal of every hop, both ways).
- Choose who is **added automatically** (everyone / people / people + repeaters / only when you
  add them, plus a hop limit); add any node from **Heard**.
- **Full GPS info**: sky plot, satellites, accuracy, speed, heading, altitude.
- **Saved places** and **navigate-to** with arrow, distance and ETA; share your position with
  favourites (only when moving + hourly), breadcrumb track.

**Maps**
- Offline map from SD tiles (`/tiles/z/x/y.png`, same layout as Ripple), with friends, saved
  places and your track; **download your region over Wi-Fi** right on the device.

**SOS & safety**
- Guarded 10 s SOS countdown, position + battery to your favourites every 2 → 10 → 30 min until
  cancelled, optional `#emergency`, survives reboots, works from the lock screen (hold the
  trackball 3 s). Incoming SOS: full-screen siren alarm with a **map** and distance.
- "I'm OK" check-ins; **screen lock** (PIN or passphrase) with **owner & medical info** readable
  without the PIN (roll the trackball up on the lock screen).
- *EasyDeck does not contact emergency services.*

**Device**
- Two styles - *Tactical* (black & amber) and *Modern* (MeshOS-like) - plus **Easy mode**
  with six big tiles for older users and kids.
- Optional **battery saver** (sleeps between radio packets with Bluetooth off), screenshots to
  SD, **Device check** self-test, repeater/room admin with CLI (free), packet log, Discover,
  advert echo check ("your advert was repeated by N repeaters").
- **Phone**: the official MeshCore app works over Bluetooth; the EasyDeck Android app adds full
  two-way history sync, notifications, SOS alerts and an offline people radar.
- SD Launcher friendly, watchdog + crash reporting, two-copy crash-safe settings.

See [TESTING.md](TESTING.md) for a first-test checklist.

## Install

Copy `EasyDeck-<ver>-full.bin` to the SD card and install it from the bmorcelli Launcher,
or flash it over USB at `0x0`. Data lives in its own partition or in `/easydeck` on the SD.

## Offline maps

The map uses standard 256 px PNG tiles in `/tiles/<zoom>/<x>/<y>.png` on the SD card (the same
layout Ripple uses), so you can either:

- **Download on the device:** *Settings → Offline maps* (or press **D** on the map). Pick a
  Wi-Fi network, a map source and an area (5–100 km, up to street detail), then *Download now*.
  Tiles you already have are skipped. Wi-Fi is only switched on while scanning or downloading.
- **Copy from a computer:** generate tiles for your region with any tile downloader and copy the
  `tiles` folder to the SD card.

Map sources: OpenStreetMap's own tile servers **don't allow** downloading areas for offline use,
so EasyDeck doesn't use them. Built-in choices are Thunderforest (free Hobby API key from
thunderforest.com) or any `{z}/{x}/{y}.png` URL, e.g. your own tile server. You can also set
`map_url=`, `wifi_ssid=` and `wifi_pass=` in `easydeck-setup.txt`. Check your provider's terms.

## Controls

Trackball: move · press: open · **hold ~1 s and release: back** · hold 3 s: SOS. Home keys: **C** chats, **O** contacts,
**G** groups, **M** map, **H** heard, **R** signal, **A** advertise, **P** GPS, **X** SOS,
**K** I'm OK, **S** settings, **W** saved places. In chats Enter sends; Backspace on an empty box goes back; **/** searches messages.

**Screenshots:** hold the trackball and press any key - saved as `/screenshots/easydeck-NNNN.bmp` on the SD card. Please attach one to bug reports.

## Build

    git submodule update --init
    cd firmware && pio run            # -> dist/EasyDeck-<ver>-full.bin / -app.bin
    cmake -S sim -B sim/build && cmake --build sim/build && \
      sim/build/easydeck_sim sim/scenarios/tour.txt sim/shots   # desktop UI simulator

Fonts are generated by `tools/gen_fonts.py` (`npm install` in `tools/` first).

## Credits & licences

EasyDeck code: MIT. Built on [MeshCore](https://github.com/meshcore-dev/MeshCore) (MIT),
LVGL (MIT), LovyanGFX (FreeBSD), RadioLib (MIT). Fonts: Barlow Semi Condensed (SIL OFL),
Ubuntu (Ubuntu Font Licence). Icons: Material Design Icons (Apache 2.0).
GPS parsing uses MicroNMEA (LGPL-2.1).


## Licence

EasyDeck is MIT licensed (see `LICENSE`). Third-party parts and fonts are listed in
`THIRD_PARTY.md`. EasyDeck does not contact emergency services; the SOS feature only reaches
people on the mesh.

<sub>Keywords: MeshCore firmware, T-Deck Plus firmware, LilyGO T-Deck, LoRa mesh messenger,
off-grid messaging, MeshCore companion, standalone MeshCore client, Ripple alternative,
MeshOS alternative, SOS, GPS tracking, offline maps, Australia mesh, NSW Mesh.</sub>
