// On-target tests of the protocol v1 parser against the shared fixtures (PC-Control-Deck-Agent shared/fixtures).
#include <Arduino.h>
#include <math.h>
#include <string.h>
#include <unity.h>

#include "../common/wait_host.h"

#include "fixtures.h"
#include "network/protocol_parser.h"

using proto::Message;
using proto::MsgType;
using proto::ParseResult;

static Message s_msg;  // large: keep off the stack

void test_snapshot_full() {
  TEST_ASSERT_EQUAL(ParseResult::Ok, proto::parse(FX_SNAPSHOT_FULL, strlen(FX_SNAPSHOT_FULL), s_msg));
  TEST_ASSERT_EQUAL(MsgType::Snapshot, s_msg.type);
  const auto &s = s_msg.snapshot;
  TEST_ASSERT_FALSE(s.synthetic);
  TEST_ASSERT_EQUAL_STRING("desktop-i9", s.host);
  TEST_ASSERT_EQUAL_STRING("Intel Core i9-14900K", s.cpu.name);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 68.2f, s.cpu.temp_c);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 5400.0f, s.cpu.freq_mhz);  // integer JSON number
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 154.2f, s.cpu.package_power_w);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 16384.0f, s.gpu.vram_total_mib);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 65536.0f, s.ram.total_mib);
  TEST_ASSERT_EQUAL(1, s.storage_count);
  TEST_ASSERT_EQUAL(2, s.fan_count);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 1200.0f, s.fans[0].rpm);
  TEST_ASSERT_TRUE(isnan(s.fans[1].rpm));  // null rpm stays missing, not 0
  TEST_ASSERT_EQUAL(2, s.temp_count);
  TEST_ASSERT_EQUAL_STRING("GPU Memory Junction", s.temps[0].name);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 62.0f, s.temps[0].temp_c);
}

void test_snapshot_nulls_become_nan() {
  TEST_ASSERT_EQUAL(ParseResult::Ok, proto::parse(FX_SNAPSHOT_UNPRIVILEGED, strlen(FX_SNAPSHOT_UNPRIVILEGED), s_msg));
  const auto &s = s_msg.snapshot;
  TEST_ASSERT_TRUE(isnan(s.cpu.temp_c));
  TEST_ASSERT_TRUE(isnan(s.cpu.freq_mhz));
  TEST_ASSERT_TRUE(isnan(s.cpu.package_power_w));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.8f, s.cpu.load_pct);
  TEST_ASSERT_EQUAL_STRING("NVIDIA GeForce RTX 5070 Ti Laptop GPU", s.gpu.name);
  TEST_ASSERT_EQUAL(0, s.storage_count);
  TEST_ASSERT_EQUAL(0, s.fan_count);
}

void test_rejections() {
  TEST_ASSERT_EQUAL(ParseResult::UnsupportedVersion, proto::parse(FX_INVALID_VERSION, strlen(FX_INVALID_VERSION), s_msg));
  TEST_ASSERT_EQUAL(ParseResult::UnknownType, proto::parse(FX_INVALID_TYPE, strlen(FX_INVALID_TYPE), s_msg));
  TEST_ASSERT_EQUAL(ParseResult::Malformed, proto::parse(FX_INVALID_MALFORMED, strlen(FX_INVALID_MALFORMED), s_msg));
  const char *not_obj = "[1,2,3]";
  TEST_ASSERT_EQUAL(ParseResult::Malformed, proto::parse(not_obj, strlen(not_obj), s_msg));
  const char *str_v = R"({"v":"1","type":"heartbeat","payload":{}})";
  TEST_ASSERT_EQUAL(ParseResult::Malformed, proto::parse(str_v, strlen(str_v), s_msg));
  TEST_ASSERT_EQUAL(ParseResult::TooLarge, proto::parse("{}", proto::kMaxFrame + 1, s_msg));
}

void test_wrong_types_are_missing_not_zero() {
  const char *j = R"({"v":1,"type":"telemetry.snapshot","payload":{"cpu":{"tempC":"hot","loadPct":true},"ram":{}}})";
  TEST_ASSERT_EQUAL(ParseResult::Ok, proto::parse(j, strlen(j), s_msg));
  TEST_ASSERT_TRUE(isnan(s_msg.snapshot.cpu.temp_c));
  TEST_ASSERT_TRUE(isnan(s_msg.snapshot.cpu.load_pct));
  TEST_ASSERT_TRUE(isnan(s_msg.snapshot.ram.total_mib));
}

void test_utf8_truncation_is_clean() {
  // 30 x "ñ" = 60 bytes; host buffer is 24 bytes -> must not end on half a character.
  String j = "{\"v\":1,\"type\":\"telemetry.snapshot\",\"deviceId\":\"";
  for (int i = 0; i < 30; i++) j += "\xC3\xB1";
  j += "\",\"payload\":{}}";
  TEST_ASSERT_EQUAL(ParseResult::Ok, proto::parse(j.c_str(), j.length(), s_msg));
  const size_t n = strlen(s_msg.snapshot.host);
  TEST_ASSERT_TRUE(n % 2 == 0);
  TEST_ASSERT_TRUE(n <= sizeof(s_msg.snapshot.host) - 1);
}

void test_capabilities() {
  const char *j =
      R"({"v":1,"type":"telemetry.capabilities","payload":{"agent":"a","provider":"LibreHardwareMonitor 0.9.6","actions":["volume.set","media.next"],"notes":["needs admin","x"],"authRequired":false}})";
  TEST_ASSERT_EQUAL(ParseResult::Ok, proto::parse(j, strlen(j), s_msg));
  TEST_ASSERT_EQUAL(MsgType::Capabilities, s_msg.type);
  TEST_ASSERT_TRUE(s_msg.caps.volume);
  TEST_ASSERT_TRUE(s_msg.caps.media);
  TEST_ASSERT_FALSE(s_msg.caps.lock);
  TEST_ASSERT_FALSE(s_msg.caps.power);
  TEST_ASSERT_EQUAL(2, s_msg.caps.note_count);
  TEST_ASSERT_EQUAL_STRING("needs admin", s_msg.caps.first_note);
}

void test_crypto_data() {
  static crypto::Market m;
  s_msg.market = &m;
  TEST_ASSERT_EQUAL(ParseResult::Ok, proto::parse(FX_CRYPTO_DATA, strlen(FX_CRYPTO_DATA), s_msg));
  TEST_ASSERT_EQUAL(MsgType::CryptoData, s_msg.type);
  TEST_ASSERT_TRUE(m.ok);
  TEST_ASSERT_EQUAL_STRING("EUR", m.cur);
  TEST_ASSERT_EQUAL(2, m.count);
  TEST_ASSERT_EQUAL_STRING("BTCUSDT", m.coins[0].pair);
  TEST_ASSERT_TRUE(fabs(m.coins[0].price - 73891.2) < 0.01);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.23f, m.coins[0].chg_pct);
  TEST_ASSERT_TRUE(fabs(m.coins[0].vol - 2.91e10) < 1.0);  // integer above 2^32
  TEST_ASSERT_TRUE(fabs(m.coins[0].mcap - 1.46e12) < 1.0);
  TEST_ASSERT_EQUAL(1, m.coins[0].rank);
  TEST_ASSERT_EQUAL(48, m.coins[0].spark_n);
  TEST_ASSERT_EQUAL(100, m.coins[0].spark[6]);
  // An unlisted pair: every value null -> missing, never 0.
  TEST_ASSERT_EQUAL_STRING("XYZUSDT", m.coins[1].pair);
  TEST_ASSERT_TRUE(isnan(m.coins[1].price));
  TEST_ASSERT_TRUE(isnan(m.coins[1].chg_pct));
  TEST_ASSERT_EQUAL(-1, m.coins[1].rank);
  TEST_ASSERT_EQUAL(0, m.coins[1].spark_n);
  TEST_ASSERT_EQUAL(63, m.fng);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 57.31f, m.btc_dom);
  TEST_ASSERT_EQUAL_UINT32(1794479100u, m.ts);
  // Error payload from the PC app.
  const char *err = R"({"v":1,"type":"crypto.data","payload":{"cur":"EUR","error":"unavailable"}})";
  TEST_ASSERT_EQUAL(ParseResult::Ok, proto::parse(err, strlen(err), s_msg));
  TEST_ASSERT_FALSE(m.ok);
  TEST_ASSERT_EQUAL_STRING("unavailable", m.error);
  s_msg.market = nullptr;
}

void test_crypto_candles() {
  static crypto::Chart c;
  s_msg.chart = &c;
  TEST_ASSERT_EQUAL(ParseResult::Ok, proto::parse(FX_CRYPTO_CANDLES, strlen(FX_CRYPTO_CANDLES), s_msg));
  TEST_ASSERT_EQUAL(MsgType::CryptoCandles, s_msg.type);
  TEST_ASSERT_TRUE(c.ok);
  TEST_ASSERT_EQUAL_STRING("BTCUSDT", c.pair);
  TEST_ASSERT_EQUAL(static_cast<int>(crypto::Range::H24), c.range);
  TEST_ASSERT_EQUAL(5, c.n);
  TEST_ASSERT_EQUAL_UINT32(1794474900u, c.c[0].t);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 73891.2f, c.c[4].c);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 21460000.0f, c.c[4].v);
  s_msg.chart = nullptr;
}

void test_build_crypto() {
  char buf[512];
  const char *pairs[2] = {"BTCUSDT", "ETHUSDT"};
  const char *ids[2] = {"bitcoin", "ethereum"};
  const size_t n = proto::build_crypto_request(buf, sizeof(buf), pairs, ids, 2, "EUR");
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_STRING(
      R"({"v":1,"type":"crypto.request","payload":{"cur":"EUR","coins":[{"p":"BTCUSDT","g":"bitcoin"},{"p":"ETHUSDT","g":"ethereum"}]}})",
      buf);
  TEST_ASSERT_TRUE(proto::build_crypto_chart(buf, sizeof(buf), "SOLUSDT", 6, "USD") > 0);
  TEST_ASSERT_EQUAL_STRING(R"({"v":1,"type":"crypto.chart","payload":{"p":"SOLUSDT","r":"all","cur":"USD"}})", buf);
  TEST_ASSERT_EQUAL(0, proto::build_crypto_request(buf, 20, pairs, ids, 2, "EUR"));  // overflow -> 0
}

void test_build_hello() {
  char buf[160];
  const size_t n = proto::build_hello(buf, sizeof(buf), "deck-001122334455", "0.2.0");
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"type\":\"hello\""));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"clientId\":\"deck-001122334455\""));
  TEST_ASSERT_EQUAL(0, proto::build_hello(buf, 20, "deck-001122334455", "0.2.0"));  // overflow -> 0
}

void setup() {
  wait_for_test_host();
  UNITY_BEGIN();
  RUN_TEST(test_snapshot_full);
  RUN_TEST(test_snapshot_nulls_become_nan);
  RUN_TEST(test_rejections);
  RUN_TEST(test_wrong_types_are_missing_not_zero);
  RUN_TEST(test_utf8_truncation_is_clean);
  RUN_TEST(test_capabilities);
  RUN_TEST(test_build_hello);
  RUN_TEST(test_crypto_data);
  RUN_TEST(test_crypto_candles);
  RUN_TEST(test_build_crypto);
  UNITY_END();
}

void loop() { delay(1000); }  // never spin: an empty loop() starved the core and correlated with USB drops
