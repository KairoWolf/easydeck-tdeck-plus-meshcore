#!/bin/bash
# Decode a crash.txt line from the T-Deck:  tools/decode_crash.sh 0.54.0 0x42012345 0x42023456 ...
# Needs firmware/.elf-archive/EasyDeck-<ver>.elf (kept locally for every release; not in git).
ver=$1; shift
elf=$(dirname "$0")/../firmware/.elf-archive/EasyDeck-$ver.elf
a2l=$(ls ~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-addr2line)
[ -f "$elf" ] || { echo "no ELF for $ver"; exit 1; }
"$a2l" -pfiaC -e "$elf" "$@"
