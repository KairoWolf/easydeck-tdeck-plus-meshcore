#pragma once
// Plain-text setup file for preparing devices ("fleet mode"): drop easydeck-setup.txt on the
// SD card root and the device applies it on next boot (then renames it to .applied).
#include "core/Model.h"
#include "Theme.h"

#define SETUP_IMPORT_PATH  "/easydeck-setup.txt"
#define SETUP_DONE_PATH    "/easydeck-setup.applied"
#define SETUP_EXPORT_PATH  "/easydeck-setup-export.txt"

namespace SetupFile {
  // returns number of settings applied (0 = no file)
  int importIfPresent(Model* m);
  bool exportTo(Model* m, const char* path = SETUP_EXPORT_PATH);
}
