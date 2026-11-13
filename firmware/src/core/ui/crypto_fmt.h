#pragma once
// Money / number formatting for the Crypto tab (ported from the JC1060P470C CryptoTracker).
// Spanish uses "." for thousands and "," for decimals ("80.229,12 €"), English the reverse
// ("$80,229.12"). Values are already in the display currency (the PC app converts them).

#include <stddef.h>
#include <stdint.h>

namespace ui {
namespace cfmt {

struct CurrencyInfo {
  const char *code;    // ISO code, as sent to / received from the PC app
  const char *symbol;  // display symbol
};
const CurrencyInfo &currency(uint8_t idx);  // storage::Currency order
int currency_index(const char *code);       // -1 if unknown

int price_decimals(double v);  // decimals that suit a price of this magnitude
void number(double v, int decimals, char *out, size_t n);           // locale separators, "-" for negatives
void money(double v, int decimals, const char *cur, char *out, size_t n);  // decimals < 0: automatic
void compact_money(double v, const char *cur, char *out, size_t n);  // "$1.34T", "12,4 M €"
void pct(double v, char *out, size_t n);                             // "+2.34%" / "-0.51%"

}  // namespace cfmt
}  // namespace ui
