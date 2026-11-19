#include "Model.h"

// days since 1970-01-01 for a civil date (Howard Hinnant's algorithm)
static int32_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}

static int yearOf(uint32_t t) {   // UTC year
  int32_t z = (int32_t)(t / 86400) + 719468;
  const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned m = mp + (mp < 10 ? 3 : -9);
  return (int)yoe + era * 400 + (m <= 2);
}

// first Sunday of a month, as days since 1970 (1970-01-01 was a Thursday)
static int32_t firstSunday(int y, unsigned m) {
  int32_t d = daysFromCivil(y, m, 1);
  int wd = (int)((d % 7 + 7 + 4) % 7);   // 0 = Sunday
  return d + (7 - wd) % 7;
}

int tzOffsetAt(int setting, uint32_t t) {
  if (setting != TZ_AU_EAST) return setting;
  // AEDT starts 2:00 AEST on the 1st Sunday of October (16:00 UTC Saturday) and ends 3:00 AEDT on
  // the 1st Sunday of April (also 16:00 UTC Saturday).
  int y = yearOf(t + 11 * 3600);
  uint32_t end = (uint32_t)firstSunday(y, 4) * 86400 - 8 * 3600;     // Sat 16:00 UTC
  uint32_t start = (uint32_t)firstSunday(y, 10) * 86400 - 8 * 3600;
  return (t < end || t >= start) ? 11 : 10;
}
