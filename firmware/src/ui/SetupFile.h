#pragma once
// Plain-text setup file for preparing devices ("fleet mode"): drop easydeck-setup.txt on the
// SD card root and the device applies it on next boot (then renames it to .applied).
#include "core/Model.h"
#include "Theme.h"

#define SETUP_IMPORT_PATH  "/easydeck-setup.txt"
#define SETUP_DONE_PATH    "/easydeck-setup.applied"
#define SETUP_EXPORT_PATH  "/easydeck-setup-export.txt"

#include <string>
#include <vector>

namespace SetupFile {
  // returns number of settings applied (0 = no file)
  int importIfPresent(Model* m);

  // Settings packs (packs/*.txt in the GitHub repo, some built in): same keys as the setup file,
  // plus pack_name=, pack_about=, note= and group=<name>|<scope> (scope: region, "none", or empty).
  struct PackInfo {
    std::string name, about, preset, scope;
    std::vector<std::string> groups, notes;
  };
  bool describe(const char* text, PackInfo& out);   // false = not a pack
  int applyText(Model* m, const char* text);        // settings applied
  int builtinPackCount();
  const char* builtinPack(int i);
  bool exportTo(Model* m, const char* path = SETUP_EXPORT_PATH);
}
