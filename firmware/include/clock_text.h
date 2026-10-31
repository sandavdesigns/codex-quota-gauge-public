#pragma once
#include <stddef.h>
#include <stdio.h>
#include <time.h>

inline bool formatClockDate(time_t epoch, char *output, size_t capacity) {
  if (output == nullptr || capacity < 13) return false;
  struct tm local {};
  if (epoch < 1700000000 || localtime_r(&epoch, &local) == nullptr) {
    snprintf(output, capacity, "--:-- --.--.");
    return false;
  }
  snprintf(output, capacity, "%02d:%02d %02d.%02d.", local.tm_hour, local.tm_min,
           local.tm_mday, local.tm_mon + 1);
  return true;
}
