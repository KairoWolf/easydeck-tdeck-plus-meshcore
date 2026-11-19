#include "NetTime.h"
#include <time.h>
#include <sys/time.h>

static volatile uint32_t s_utc = 0, s_ms = 0;

void netTimeGrab() {
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
  for (int i = 0; i < 40; i++) {
    time_t t = time(nullptr);
    if (t > 1767225600) { s_ms = millis(); s_utc = (uint32_t)t; return; }   // after 2026-01-01
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

bool netTimeTake(uint32_t& now) {
  uint32_t u = s_utc;
  if (!u) return false;
  now = u + (millis() - s_ms) / 1000;
  s_utc = 0;
  return true;
}
