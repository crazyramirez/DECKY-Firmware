// On-target unit tests for pure logic modules (no display needed).
// Run:  pio test -e test   (board connected; no host compiler is required)

#include <Arduino.h>
#include <math.h>
#include <unity.h>

#include "../common/wait_host.h"

#include "alerts/thermal_alerts.h"
#include "telemetry/history.h"

using alerts::Level;
using alerts::ThermalAlert;
using alerts::Thresholds;

static Thresholds cpu_t() {
  Thresholds t{85.0f, 95.0f};
  t.hysteresis_c = 3.0f;
  t.min_duration_ms = 3000;
  t.cooldown_ms = 60000;
  return t;
}

void test_history_ring_and_max() {
  telemetry::History<4> h;
  TEST_ASSERT_EQUAL(0, h.size());
  TEST_ASSERT_TRUE(isnan(h.max()));
  h.push(10);
  h.push(NAN);
  h.push(30);
  TEST_ASSERT_EQUAL(3, h.size());
  TEST_ASSERT_EQUAL_FLOAT(30, h.max());
  TEST_ASSERT_TRUE(isnan(h.at(1)));
  h.push(20);
  h.push(5);  // overwrites the oldest (10)
  TEST_ASSERT_EQUAL(4, h.size());
  TEST_ASSERT_TRUE(isnan(h.at(0)));
  TEST_ASSERT_EQUAL_FLOAT(5, h.latest());
  TEST_ASSERT_EQUAL_FLOAT(30, h.max());
  h.clear();
  TEST_ASSERT_EQUAL(0, h.size());
}

void test_alert_requires_min_duration() {
  ThermalAlert a(cpu_t());
  TEST_ASSERT_FALSE(a.update(90, 0));
  TEST_ASSERT_EQUAL(Level::Normal, a.level());
  TEST_ASSERT_FALSE(a.update(90, 2000));
  TEST_ASSERT_TRUE(a.update(90, 3000));
  TEST_ASSERT_EQUAL(Level::Warning, a.level());
}

void test_alert_short_spike_ignored() {
  ThermalAlert a(cpu_t());
  a.update(97, 0);
  a.update(70, 1000);  // back to normal before min duration
  a.update(97, 2000);
  TEST_ASSERT_FALSE(a.update(97, 4000));  // timer restarted at 2000
  TEST_ASSERT_EQUAL(Level::Normal, a.level());
  TEST_ASSERT_TRUE(a.update(97, 5000));
  TEST_ASSERT_EQUAL(Level::Critical, a.level());
}

void test_alert_hysteresis() {
  ThermalAlert a(cpu_t());
  a.update(88, 0);
  a.update(88, 3000);
  TEST_ASSERT_EQUAL(Level::Warning, a.level());
  a.update(83, 4000);  // above 85-3: stays warning
  TEST_ASSERT_EQUAL(Level::Warning, a.level());
  a.update(81.9f, 5000);  // below 82: normal
  TEST_ASSERT_EQUAL(Level::Normal, a.level());
}

void test_alert_cooldown() {
  ThermalAlert a(cpu_t());
  a.update(88, 0);
  TEST_ASSERT_TRUE(a.update(88, 3000));
  a.update(70, 4000);
  a.update(88, 5000);
  TEST_ASSERT_FALSE(a.update(88, 8000));  // re-raised inside cooldown: no new notification
  TEST_ASSERT_EQUAL(Level::Warning, a.level());
  a.update(70, 9000);
  a.update(88, 70000);
  TEST_ASSERT_TRUE(a.update(88, 73000));  // cooldown elapsed
}

void test_alert_escalation_notifies_inside_cooldown() {
  ThermalAlert a(cpu_t());
  a.update(88, 0);
  TEST_ASSERT_TRUE(a.update(88, 3000));
  a.update(96, 4000);
  TEST_ASSERT_TRUE(a.update(96, 7000));  // warning -> critical always notifies
  TEST_ASSERT_EQUAL(Level::Critical, a.level());
}

void test_alert_missing_samples_are_neutral() {
  ThermalAlert a(cpu_t());
  a.update(88, 0);
  a.update(88, 3000);
  TEST_ASSERT_FALSE(a.update(NAN, 4000));
  TEST_ASSERT_FALSE(a.update(INFINITY, 4500));
  TEST_ASSERT_EQUAL(Level::Warning, a.level());
}

void setup() {
  wait_for_test_host();
  UNITY_BEGIN();
  RUN_TEST(test_history_ring_and_max);
  RUN_TEST(test_alert_requires_min_duration);
  RUN_TEST(test_alert_short_spike_ignored);
  RUN_TEST(test_alert_hysteresis);
  RUN_TEST(test_alert_cooldown);
  RUN_TEST(test_alert_escalation_notifies_inside_cooldown);
  RUN_TEST(test_alert_missing_samples_are_neutral);
  UNITY_END();
}

void loop() { delay(1000); }  // never spin: an empty loop() starved the core and correlated with USB drops
