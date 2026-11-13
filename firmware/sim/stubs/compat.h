#pragma once
#include <string.h>
#include <time.h>
#ifdef __cplusplus
extern "C" {
#endif
static inline size_t pcd_strlcpy(char *d, const char *s, size_t n) {
  size_t l = strlen(s);
  if (n) {
    size_t c = l < n - 1 ? l : n - 1;
    memcpy(d, s, c);
    d[c] = 0;
  }
  return l;
}
static inline struct tm *pcd_gmtime_r(const time_t *t, struct tm *r) {
  struct tm *p = gmtime(t);
  if (!p) return 0;
  *r = *p;
  return r;
}
#ifdef __cplusplus
}
#endif
#define strlcpy pcd_strlcpy
#define gmtime_r pcd_gmtime_r

/* PCD_SIM_TIME=<unix seconds> pins the clock (reproducible screenshots); unset = real time. */
#include <stdlib.h>
#ifdef __cplusplus
extern "C" {
#endif
static inline time_t pcd_sim_time(time_t *t) {
  const char *e = getenv("PCD_SIM_TIME");
  time_t v = e && *e ? (time_t)strtoll(e, 0, 10) : (time)(0);
  if (t) *t = v;
  return v;
}
#ifdef __cplusplus
}
#endif
#define time(t) pcd_sim_time(t)
