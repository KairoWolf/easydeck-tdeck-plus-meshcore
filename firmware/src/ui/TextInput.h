#pragma once
// Typing helpers for the message box (pure string logic, no LVGL, unit-testable):
//  - emoji shortcodes:  ":thumbsup:" -> 👍 when the closing ':' is typed
//  - ASCII smileys:     ":) " -> 🙂 when followed by a space
//  - double space:      "word  " -> "word. "
// Only emoji the device font can draw are included.

struct TextEdit {
  int del_bytes = 0;    // delete this many bytes before the cursor (all ASCII here) ...
  char insert[12] = ""; // ... then insert this UTF-8 text
};

// `text` is the box content *after* `typed` was appended (cursor at the end).
// Returns true and fills `out` if an edit should be applied.
bool textEditFor(const char* text, char typed, TextEdit& out);

// Number of shortcodes and the n-th name/emoji (for a help list).
int shortcodeCount();
const char* shortcodeName(int i);
const char* shortcodeEmoji(int i, char* buf5);
