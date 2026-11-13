#pragma once
// Watched coins as stored in the settings ("BTC,ETH,SOL") <-> COIN_CATALOG indices.

#include <stddef.h>
#include <stdint.h>

namespace crypto {

int catalog_index(const char *sym);                         // -1 if unknown
int parse_watch(const char *csv, int8_t *out, int max);     // unknown / duplicate symbols skipped
void format_watch(const int8_t *idx, int n, char *out, size_t len);

}  // namespace crypto
