#include "Iso8601.h"

#include <time.h>

namespace Iso8601 {

time_t parseUtc(const String &s) {
  int year, month, day, hour, minute, second;
  int matched = sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second);
  if (matched != 6) return 0;

  struct tm t = {};
  t.tm_year = year - 1900;
  t.tm_mon = month - 1;
  t.tm_mday = day;
  t.tm_hour = hour;
  t.tm_min = minute;
  t.tm_sec = second;
  // timegm (not mktime) - the fields above are already UTC, so this must
  // not apply the device's local timezone.
  time_t utc = timegm(&t);

  // Trailing offset, e.g. ".897964+00:00" or "Z". Every response observed
  // from claude.ai uses +00:00, but honor a non-zero offset if one ever
  // shows up rather than silently mis-computing "resets in".
  int offSign = 0, offH = 0, offM = 0;
  int plusPos = s.lastIndexOf('+');
  int minusPos = s.lastIndexOf('-');
  // The date portion also contains '-', so only trust a '-' that appears
  // after the 'T' (i.e. in the time/offset portion).
  int tPos = s.indexOf('T');
  if (minusPos <= tPos) minusPos = -1;

  if (plusPos > tPos) {
    offSign = 1;
    sscanf(s.c_str() + plusPos + 1, "%d:%d", &offH, &offM);
  } else if (minusPos > tPos) {
    offSign = -1;
    sscanf(s.c_str() + minusPos + 1, "%d:%d", &offH, &offM);
  }

  utc -= offSign * (offH * 3600 + offM * 60);
  return utc;
}

} // namespace Iso8601
