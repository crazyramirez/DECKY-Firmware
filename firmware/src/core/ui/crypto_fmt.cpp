#include "crypto_fmt.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui/i18n.h"

namespace ui {
namespace cfmt {
namespace {

const CurrencyInfo kCurrencies[] = {
    {"USD", "$"}, {"EUR", "\xE2\x82\xAC"}, {"MXN", "MX$"}, {"ARS", "AR$"}, {"COP", "COL$"}, {"BRL", "R$"},
};
constexpr int kCount = sizeof(kCurrencies) / sizeof(kCurrencies[0]);

bool es() { return lang() != storage::Lang::En; }

const CurrencyInfo &by_code(const char *code) {
  const int i = currency_index(code);
  return kCurrencies[i < 0 ? 0 : i];
}

// Euro after the amount in Spanish ("1.234,56 €"), every other symbol before it.
bool suffix(const char *cur) { return es() && cur && !strcmp(cur, "EUR"); }

}  // namespace

const CurrencyInfo &currency(uint8_t idx) { return kCurrencies[idx < kCount ? idx : 0]; }

int currency_index(const char *code) {
  for (int i = 0; code && i < kCount; i++)
    if (!strcmp(code, kCurrencies[i].code)) return i;
  return -1;
}

int price_decimals(double v) {
  v = fabs(v);
  if (v >= 100000) return 0;
  if (v >= 1) return 2;
  if (v <= 0) return 2;
  int d = static_cast<int>(-floor(log10(v))) + 3;
  if (d < 2) d = 2;
  if (d > 10) d = 10;
  return d;
}

void number(double v, int decimals, char *out, size_t n) {
  if (!n) return;
  if (isnan(v) || isinf(v)) {
    snprintf(out, n, "--");
    return;
  }
  char tmp[48];
  const bool neg = v < 0;
  if (neg) v = -v;
  snprintf(tmp, sizeof(tmp), "%.*f", decimals, v);
  const char *dot = strchr(tmp, '.');
  const int int_len = dot ? static_cast<int>(dot - tmp) : static_cast<int>(strlen(tmp));
  const char th = es() ? '.' : ',';
  const char dec = es() ? ',' : '.';
  char res[64];
  int o = 0;
  if (neg) res[o++] = '-';
  for (int i = 0; i < int_len && o < 60; i++) {
    res[o++] = tmp[i];
    const int rem = int_len - 1 - i;
    // Spanish style: no separator for 4-digit integers (1234,56), like the RAE recommends.
    if (rem > 0 && rem % 3 == 0 && !(es() && int_len == 4)) res[o++] = th;
  }
  if (dot) {
    res[o++] = dec;
    for (const char *p = dot + 1; *p && o < static_cast<int>(sizeof(res)) - 1; p++) res[o++] = *p;
  }
  res[o] = '\0';
  snprintf(out, n, "%s", res);
}

void money(double v, int decimals, const char *cur, char *out, size_t n) {
  if (isnan(v) || isinf(v)) {
    snprintf(out, n, "--");
    return;
  }
  if (decimals < 0) decimals = price_decimals(v);
  char num[64];
  number(v, decimals, num, sizeof(num));
  const char *sym = by_code(cur).symbol;
  if (suffix(cur))
    snprintf(out, n, "%s %s", num, sym);
  else if (num[0] == '-')
    snprintf(out, n, "-%s%s", sym, num + 1);
  else
    snprintf(out, n, "%s%s", sym, num);
}

void compact_money(double v, const char *cur, char *out, size_t n) {
  if (isnan(v) || isinf(v)) {
    snprintf(out, n, "--");
    return;
  }
  const char *suf = "";
  const double a = fabs(v);
  if (a >= 1e12) {
    v /= 1e12;
    suf = "T";
  } else if (a >= 1e9) {
    v /= 1e9;
    suf = "B";
  } else if (a >= 1e6) {
    v /= 1e6;
    suf = "M";
  } else if (a >= 1e3) {
    v /= 1e3;
    suf = "K";
  }
  char num[40];
  number(v, fabs(v) >= 100 ? 1 : 2, num, sizeof(num));
  const char *sym = by_code(cur).symbol;
  if (suffix(cur))
    snprintf(out, n, "%s %s %s", num, suf, sym);
  else
    snprintf(out, n, "%s%s%s", sym, num, suf);
}

void pct(double v, char *out, size_t n) {
  if (isnan(v) || isinf(v)) {
    snprintf(out, n, "--");
    return;
  }
  char num[32];
  number(fabs(v), fabs(v) >= 1000 ? 0 : 2, num, sizeof(num));
  snprintf(out, n, "%s%s%%", v < 0 ? "-" : "+", num);
}

}  // namespace cfmt
}  // namespace ui
