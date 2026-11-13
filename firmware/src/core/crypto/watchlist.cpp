#include "watchlist.h"

#include <string.h>

#include "crypto/coin_catalog.h"

namespace crypto {

int catalog_index(const char *sym) {
  for (int i = 0; sym && i < COIN_CATALOG_COUNT; i++)
    if (!strcmp(sym, COIN_CATALOG[i].sym)) return i;
  return -1;
}

int parse_watch(const char *csv, int8_t *out, int max) {
  int n = 0;
  char tok[12];
  size_t t = 0;
  for (const char *p = csv ? csv : "";; p++) {
    if (*p == ',' || *p == '\0') {
      tok[t] = '\0';
      const int idx = t ? catalog_index(tok) : -1;
      bool dup = false;
      for (int i = 0; i < n; i++) dup |= out[i] == idx;
      if (idx >= 0 && !dup && n < max) out[n++] = static_cast<int8_t>(idx);
      t = 0;
      if (*p == '\0') break;
    } else if (t + 1 < sizeof(tok) && *p != ' ') {
      tok[t++] = *p;
    }
  }
  return n;
}

void format_watch(const int8_t *idx, int n, char *out, size_t len) {
  if (!len) return;
  out[0] = '\0';
  size_t o = 0;
  for (int i = 0; i < n; i++) {
    if (idx[i] < 0 || idx[i] >= COIN_CATALOG_COUNT) continue;
    const char *s = COIN_CATALOG[idx[i]].sym;
    const size_t need = strlen(s) + (o ? 1 : 0);
    if (o + need + 1 > len) break;
    if (o) out[o++] = ',';
    memcpy(out + o, s, strlen(s));
    o += strlen(s);
    out[o] = '\0';
  }
}

}  // namespace crypto
