# First test on a T-Deck Plus

EasyDeck has only been tested in the desktop simulator so far. These steps check the parts that
can only be checked on real hardware, roughly in order. A screenshot or photo of anything odd is
the most useful bug report: **hold the trackball and press any key** to save a screenshot to
`/screenshots/` on the SD card.

## 1. Boot and Device check
1. Install `EasyDeck-<ver>-full.bin` from the SD Launcher.
2. Does the screen come on after a cold power-on? Does the keyboard type, the trackball move,
   touch work?
3. *Settings > Device check*: take a screenshot. It reports radio, keyboard, touch, SD card,
   storage, GPS (and its baud rate), battery, sound, Bluetooth, memory and clock.

## 2. Radio and adverts
1. Check *Settings > Radio preset* matches your mesh (MidCoast: Australia (Mid), 915.075 MHz).
2. Home > *Advertise* > *Whole mesh*. Wait 20 seconds: EasyDeck says whether any repeater
   re-sent your advert. *Signal* shows the same line plus RX/TX counts.
3. If no repeater re-sent it: check the antenna, go higher, check the preset, and turn GPS on so
   the clock gets set (other nodes ignore adverts with an old clock).
4. *Heard*: do repeaters and people appear after a few minutes?

## 3. Messages
1. Send to `#test` (*Signal > Send "test" to #test*); bots usually answer with your path.
   Your message should show a repeater count.
2. DM a friend: ticks should go sending > sent > delivered.
3. Pick a contact > *Route* > *Ping the repeaters*: every hop should show a signal value.

## 4. GPS, map, SOS
1. *GPS*: outdoors you should get a fix in a few minutes; *Satellites* shows the sky plot.
2. *Map*: with tiles on the SD card (or after *Offline maps*) you should see the map and friends.
3. SOS test: tell your favourites first, then start SOS and cancel it from the SOS screen.

## 5. Phone
1. Pair the T-Deck in the EasyDeck Android app (PIN under *Settings > Connect a phone*).
2. Messages should appear on both, in both directions.

## What to send back
- The Device check screenshot.
- Whether the advert echo check found repeaters.
- Screenshots of anything that looks wrong, plus what you pressed just before.
