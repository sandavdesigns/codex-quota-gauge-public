#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "hardware_config.h"
#include "clock_text.h"
void check(const char *utc, const char *expected) {
  struct tm value {};
  assert(strptime(utc, "%Y-%m-%d %H:%M", &value));
  const time_t epoch = timegm(&value);
  char text[13];
  assert(formatClockDate(epoch, text, sizeof(text)));
  assert(strcmp(text, expected) == 0);
}
int main() {
  setenv("TZ", GAUGE_TIMEZONE, 1); tzset();
  check("2026-10-30 12:35", "13:35 30.10.");
  check("2026-07-01 12:00", "14:00 01.07.");
  check("2026-03-29 00:59", "01:59 29.03.");
  check("2026-03-29 01:00", "03:00 29.03.");
  check("2026-10-25 00:59", "02:59 25.10.");
  check("2026-10-25 01:00", "02:00 25.10.");
  check("2026-12-31 23:30", "00:30 01.01.");
  char text[13];
  assert(!formatClockDate(0, text, sizeof(text)));
  assert(strcmp(text, "--:-- --.--.") == 0);
  assert(!formatClockDate(0, text, 4));
  puts("PASS: German time/date, both DST transitions, midnight/year rollover, unsynced clock, bounds");
}
