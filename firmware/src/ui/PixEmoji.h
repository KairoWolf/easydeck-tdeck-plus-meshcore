#pragma once
// Pixel-art emoji (SerenityOS set): font glyphs, sequence packing and picker data.
// Data: ui/fonts/pixemoji.c (tools/gen_emoji.py). Code: ui/PixEmoji.c.
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define PIX_PUA 0xF0000u   // multi-codepoint emoji are drawn as one private-use code from here

// Text that needs packing before LVGL draws it (has flags / ZWJ sequences / keycaps / tags)?
int pixemoji_needs_pack(const char* s);
// Replace every known emoji sequence with its single private-use code. out may not alias in;
// the result is never longer than the input. Returns the output length.
size_t pixemoji_pack(const char* in, char* out, size_t outsz);
// The reverse, for sending: private-use codes back to the real (fully-qualified) sequences.
size_t pixemoji_unpack(const char* in, char* out, size_t outsz);
// Bytes `s` takes once unpacked (what goes over the air).
size_t pixemoji_unpacked_len(const char* s);

// Picker: emoji in Unicode order, in groups (Smileys, People, ... Flags, Other).
int pixemoji_group_count(void);
const char* pixemoji_group_name(int g);
int pixemoji_group_first(int g);
int pixemoji_group_size(int g);
int pixemoji_total(void);
uint32_t pixemoji_at(int i);           // code to draw (a real code point or PUA)
const char* pixemoji_name(int i);      // lower-case Unicode name, e.g. "flag: australia"
int pixemoji_utf8(uint32_t cp, char* buf5);   // encode one code point, returns bytes
int pixemoji_has(uint32_t cp);

#ifdef __cplusplus
}
#endif
