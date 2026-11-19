# EasyDeck for Android

Companion app for EasyDeck (and stock MeshCore) devices over Bluetooth LE.

- **Full two-way sync** with EasyDeck firmware via the EasyDeck Sync Protocol: the T-Deck keeps
  the one shared history; the app mirrors it ("everything since #N" + live pushes), so messages
  typed on the T-Deck show here and messages sent here show on the T-Deck — with delivery ticks
  and read state shared both ways.
- Works with stock MeshCore companions too (falls back to the standard message queue).
- Tactical style matching the device.

Build (Linux, no Android Studio needed):

    ./gradlew testDebugUnitTest assembleRelease   # -> app/build/outputs/apk/release/app-release.apk

Install: `adb install app-release.apk`, or copy the APK to the phone and open it
(allow "install unknown apps"). Android 12+.
