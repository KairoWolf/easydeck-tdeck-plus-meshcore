#include "TextInput.h"
#include <ctype.h>
#include <stdint.h>
#include <string.h>

static const struct { const char* name; uint32_t cp; } kCodes[] = {
  // faces
  { "smile", 0x1F604 }, { "grin", 0x1F600 }, { "joy", 0x1F602 }, { "rofl", 0x1F923 }, { "wink", 0x1F609 },
  { "blush", 0x1F60A }, { "heart_eyes", 0x1F60D }, { "kiss", 0x1F618 }, { "thinking", 0x1F914 },
  { "neutral", 0x1F610 }, { "cry", 0x1F622 }, { "sob", 0x1F62D }, { "angry", 0x1F620 }, { "rage", 0x1F621 },
  { "scream", 0x1F631 }, { "sweat", 0x1F613 }, { "sleeping", 0x1F634 }, { "cool", 0x1F60E },
  { "sunglasses", 0x1F60E }, { "upside_down", 0x1F643 }, { "party", 0x1F973 }, { "shrug", 0x1F937 },
  { "facepalm", 0x1F926 }, { "eyes", 0x1F440 }, { "skull", 0x1F480 }, { "ghost", 0x1F47B }, { "robot", 0x1F916 },
  { "poop", 0x1F4A9 }, { "zzz", 0x1F4A4 },
  // hands & hearts
  { "wave", 0x1F44B }, { "ok", 0x1F44C }, { "clap", 0x1F44F }, { "thumbsup", 0x1F44D }, { "+1", 0x1F44D },
  { "thumbsdown", 0x1F44E }, { "-1", 0x1F44E }, { "pray", 0x1F64F }, { "muscle", 0x1F4AA },
  { "handshake", 0x1F91D }, { "heart", 0x2764 }, { "broken_heart", 0x1F494 },
  // status & safety
  { "fire", 0x1F525 }, { "tada", 0x1F389 }, { "check", 0x2705 }, { "x", 0x274C }, { "star", 0x2B50 },
  { "100", 0x1F4AF }, { "pin", 0x1F4CD }, { "sos", 0x1F198 }, { "warning", 0x26A0 }, { "siren", 0x1F6A8 },
  { "stop", 0x1F6D1 }, { "boom", 0x1F4A5 },
  // places & outdoors
  { "home", 0x1F3E0 }, { "car", 0x1F697 }, { "walk", 0x1F6B6 }, { "camping", 0x1F3D5 }, { "tent", 0x26FA },
  { "mountain", 0x26F0 }, { "ocean", 0x1F30A }, { "map", 0x1F5FA }, { "compass", 0x1F9ED },
  { "boat", 0x1F6E5 }, { "surf", 0x1F3C4 }, { "bike", 0x1F6B4 }, { "run", 0x1F3C3 }, { "fishing", 0x1F3A3 },
  // weather
  { "sun", 0x2600 }, { "cloud", 0x2601 }, { "rain", 0x1F327 }, { "storm", 0x26C8 }, { "lightning", 0x1F329 },
  { "snow", 0x2744 }, { "moon", 0x1F319 }, { "hot", 0x1F321 }, { "drop", 0x1F4A7 },
  // things
  { "coffee", 0x2615 }, { "beer", 0x1F37A }, { "pizza", 0x1F355 }, { "burger", 0x1F354 }, { "cake", 0x1F382 },
  { "gift", 0x1F381 }, { "camera", 0x1F4F7 }, { "phone", 0x1F4F1 }, { "battery", 0x1F50B },
  { "antenna", 0x1F4E1 }, { "bell", 0x1F514 }, { "speech", 0x1F4AC }, { "alarm", 0x23F0 },
  { "hourglass", 0x231B }, { "calendar", 0x1F4C5 }, { "key", 0x1F511 }, { "lock", 0x1F512 },
  // animals
  { "dog", 0x1F415 }, { "cat", 0x1F408 }, { "bird", 0x1F426 }, { "fish", 0x1F41F },
};
static const int kNumCodes = sizeof(kCodes) / sizeof(kCodes[0]);

static const struct { const char* ascii; uint32_t cp; } kSmileys[] = {
  { ":)", 0x1F642 }, { ":-)", 0x1F642 }, { ":(", 0x1F641 }, { ":-(", 0x1F641 }, { ":D", 0x1F600 },
  { ";)", 0x1F609 }, { ":P", 0x1F61B }, { ":p", 0x1F61B }, { ":O", 0x1F62E }, { ":o", 0x1F62E },
  { "<3", 0x2764 }, { ":'(", 0x1F622 },
};

static int utf8(uint32_t cp, char* o) {
  if (cp < 0x80) { o[0] = (char)cp; o[1] = 0; return 1; }
  if (cp < 0x800) { o[0] = (char)(0xC0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 0x3F)); o[2] = 0; return 2; }
  if (cp < 0x10000) {
    o[0] = (char)(0xE0 | (cp >> 12)); o[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[2] = (char)(0x80 | (cp & 0x3F)); o[3] = 0;
    return 3;
  }
  o[0] = (char)(0xF0 | (cp >> 18)); o[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
  o[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[3] = (char)(0x80 | (cp & 0x3F)); o[4] = 0;
  return 4;
}

bool textEditFor(const char* text, char typed, TextEdit& out) {
  int n = (int)strlen(text);
  if (n == 0 || text[n - 1] != typed) return false;

  if (typed == ':' && n >= 3) {   // ":name:" just closed?
    int i = n - 2;
    while (i >= 0 && n - 2 - i < 16 && text[i] != ':' && text[i] != ' ') i--;
    if (i >= 0 && text[i] == ':' && i < n - 2 && (i == 0 || text[i - 1] == ' ' || (unsigned char)text[i - 1] >= 0x80)) {
      int len = n - 2 - i;
      for (int k = 0; k < kNumCodes; k++) {
        if ((int)strlen(kCodes[k].name) == len && strncmp(kCodes[k].name, text + i + 1, len) == 0) {
          out.del_bytes = len + 2;
          utf8(kCodes[k].cp, out.insert);
          return true;
        }
      }
    }
    return false;
  }

  if (typed == ' ' && n >= 2) {
    // "word  " -> "word. " (after a letter or digit; not at the start)
    if (n >= 3 && text[n - 2] == ' ' && isalnum((unsigned char)text[n - 3])) {
      out.del_bytes = 2;
      strcpy(out.insert, ". ");
      return true;
    }
    // " :) " -> 🙂 (smiley must stand alone)
    for (const auto& s : kSmileys) {
      int l = (int)strlen(s.ascii);
      int start = n - 1 - l;
      if (start < 0 || strncmp(text + start, s.ascii, l) != 0) continue;
      if (start > 0 && text[start - 1] != ' ') continue;
      out.del_bytes = l + 1;
      int k = utf8(s.cp, out.insert);
      out.insert[k] = ' '; out.insert[k + 1] = 0;
      return true;
    }
  }
  return false;
}

int shortcodeCount() { return kNumCodes; }
const char* shortcodeName(int i) { return (i >= 0 && i < kNumCodes) ? kCodes[i].name : ""; }
const char* shortcodeEmoji(int i, char* buf5) {
  buf5[0] = 0;
  if (i >= 0 && i < kNumCodes) utf8(kCodes[i].cp, buf5);
  return buf5;
}

bool textMatches(const char* hay, const char* needle) {
  if (!hay || !needle || !*needle) return false;
  for (const char* h = hay; *h; h++) {
    const char* a = h;
    const char* b = needle;
    while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
    if (!*b) return true;
  }
  return false;
}
