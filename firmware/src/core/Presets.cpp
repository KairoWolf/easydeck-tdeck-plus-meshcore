#include "Model.h"
#include <math.h>

// Sources: nswmesh.au/meshcore/frequency (Mid/Narrow/Wide, May 2026),
// mesh.nah.rocks (MidCoast uses Australia (Mid), Nov 2026),
// meshcorewestaus.pages.dev (WA), MeshCore defaults (EU/UK, USA/Canada).
const RadioPreset kRadioPresets[] = {
  { "Australia (Mid)",    "NSW incl. MidCoast",     915.075f, 125.0f,  9, 5 },
  { "Australia (Narrow)", "Victoria, parts of NSW", 916.575f,  62.5f,  7, 8 },
  { "Australia (Wide)",   "NSW long range",         915.800f, 250.0f, 11, 5 },
  { "Australia",          "MeshCore default AU",    915.800f, 250.0f, 10, 5 },
  { "Western Australia",  "Perth / WA mesh",        923.125f,  62.5f,  8, 8 },
  { "Europe & UK",        "EU/UK narrow",           869.618f,  62.5f,  8, 8 },
  { "USA & Canada",       "US recommended",         910.525f,  62.5f,  7, 5 },
};
const int kNumRadioPresets = sizeof(kRadioPresets) / sizeof(kRadioPresets[0]);

int findPreset(const RadioSettings& r) {
  for (int i = 0; i < kNumRadioPresets; i++) {
    const RadioPreset& p = kRadioPresets[i];
    if (fabsf(r.freq - p.freq) < 0.01f && fabsf(r.bw - p.bw) < 0.01f && r.sf == p.sf) return i;
  }
  return -1;
}
