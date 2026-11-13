#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif
uint32_t millis(void);
void delay(uint32_t ms);
#ifdef __cplusplus
}
class IPAddress {
 public:
  bool fromString(const char *s) {
    int a, b, c, d;
    char x;
    return sscanf(s, "%d.%d.%d.%d%c", &a, &b, &c, &d, &x) == 4 && a >= 0 && a < 256 && b >= 0 && b < 256 && c >= 0 &&
           c < 256 && d >= 0 && d < 256;
  }
};
#endif
