// Desktop UI simulator: renders every page of the deck UI into a 480x480 framebuffer with
// realistic fake data and dumps each frame (big-endian RGB565) to out/<name>.rgb.
#include <lvgl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app/app_controller.h"
#include "app/cities.h"
#include "app/state_store.h"
#include "crypto/coin_catalog.h"
#include "crypto/crypto_store.h"
#include "network/net_task.h"
#include "storage/net_store.h"
#include "ui/battery_badge.h"
#include "ui/cities_ui.h"
#include "ui/i18n.h"
#include "ui/screen_cover.h"
#include "ui/theme/theme.h"
#include "ui/ui.h"
#include "ui/ui_internal.h"

namespace net {
extern Status g_status;
extern ScanEntry g_scan[4];
extern int g_scan_n;
extern PcEntry g_pcs[2];
extern int g_pc_n;
}  // namespace net
namespace storage {
extern NetConfig g_net;
}

static constexpr int kW = 480, kH = 480;
static constexpr int SS_SCROLL = 870;  // Settings scroll position of the SCREENSAVER section
static uint32_t g_ms = 1000;
extern "C" uint32_t millis(void) { return g_ms; }
extern "C" void delay(uint32_t ms) { g_ms += ms; }

static lv_color_t *g_fb;
static lv_disp_draw_buf_t g_draw;
static lv_disp_drv_t g_drv;
static lv_indev_drv_t g_indev;
static bool g_pressed = false;
static lv_point_t g_pt = {0, 0};
static const char *g_out = "out";

static void flush_cb(lv_disp_drv_t *d, const lv_area_t *, lv_color_t *) { lv_disp_flush_ready(d); }
static void read_cb(lv_indev_drv_t *, lv_indev_data_t *data) {
  data->point = g_pt;
  data->state = g_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void run(int ms) {
  for (int t = 0; t < ms; t += 10) {
    g_ms += 10;
    lv_timer_handler();
  }
}

static void shot(const char *name) {
  run(400);
  lv_obj_invalidate(lv_scr_act());
  lv_refr_now(NULL);
  char path[256];
  snprintf(path, sizeof(path), "%s/%s.rgb", g_out, name);
  FILE *f = fopen(path, "wb");
  if (!f) return;
  fwrite(g_fb, 2, kW * kH, f);
  fclose(f);
  printf("shot %s\n", name);
}

// Delete what a modal added to the top layer (the modules' own pointers dangle afterwards: each
// modal module is opened at most once more, through its own close/replace path).
static uint32_t g_top_base = 0;
static void top_mark() { g_top_base = lv_obj_get_child_cnt(lv_layer_top()); }
static void top_clear() {
  while (lv_obj_get_child_cnt(lv_layer_top()) > g_top_base) lv_obj_del(lv_obj_get_child(lv_layer_top(), -1));
  run(50);
}

// A finger dragged dx pixels sideways (fast enough for LVGL to call it a gesture).
static void swipe(int x, int y, int dx) {
  g_pt.x = x;
  g_pt.y = y;
  g_pressed = true;
  run(40);
  for (int s = 1; s <= 8; s++) {
    g_pt.x = x + dx * s / 8;
    run(30);
  }
  g_pressed = false;
  run(150);
}

static void tap(int x, int y) {
  g_pt.x = x;
  g_pt.y = y;
  g_pressed = true;
  run(120);
  g_pressed = false;
  run(120);
}

static void fill_state() {
  auto &st = app::state();
  st.settings.place_lat = 40.42f;
  st.settings.place_lon = -3.70f;
  strcpy(st.settings.place_name, "Madrid");
  st.settings.utc_offset_min = 60;
  st.link = app::LinkState::Online;
  st.net_link = app::LinkState::Online;
  st.caps = app::Capabilities{true, true, true, false, true};
  telemetry::Snapshot s;
  strcpy(s.host, "DESKTOP-I9");
  strcpy(s.cpu.name, "Intel Core i9-13900K");
  strcpy(s.cpu.temp_sensor_name, "CPU Package");
  s.cpu.temp_c = 68.2f;
  s.cpu.core_max_temp_c = 74.0f;
  s.cpu.load_pct = 42.5f;
  s.cpu.freq_mhz = 5400;
  s.cpu.package_power_w = 154.2f;
  strcpy(s.gpu.name, "NVIDIA GeForce RTX 4080 SUPER");
  s.gpu.temp_c = 54.1f;
  s.gpu.load_pct = 31;
  s.gpu.vram_used_mib = 4200;
  s.gpu.vram_total_mib = 16376;
  s.gpu.board_power_w = 120.3f;
  s.ram.used_mib = 24371;
  s.ram.total_mib = 65536;
  strcpy(s.storage[0].name, "Samsung SSD 990 PRO 2TB");
  s.storage[0].temp_c = 41;
  s.storage_count = 1;
  strcpy(s.temps[0].name, "GPU Memory Junction");
  s.temps[0].temp_c = 62;
  s.temp_count = 1;
  s.volume_pct = 65;
  s.has_media = true;
  s.media_playing = true;
  s.media_can_seek = true;
  strcpy(s.media_title, "Bohemian Rhapsody - Remastered 2011");
  strcpy(s.media_artist, "Queen");
  strcpy(s.media_app, "Spotify");
  s.media_pos_s = 38;
  s.media_dur_s = 354;
  s.media_accent = 0x4A6E8C;
  s.has_battery = true;
  s.bat_pct = 79;
  s.bat_plugged = 0;
  s.bat_charging = 0;
  s.bat_secs_left = 5400;
  s.bat_remaining_wh = 62.7f;
  s.bat_full_wh = 79.4f;
  s.bat_rate_w = -14.2f;
  for (int i = 0; i < 60; i++) {
    telemetry::Snapshot t = s;
    t.cpu.load_pct = 35 + 20 * sinf(i * 0.21f) + (i % 7);
    t.cpu.temp_c = 62 + 8 * sinf(i * 0.13f);
    t.gpu.load_pct = 30 + 10 * cosf(i * 0.17f);
    t.gpu.temp_c = 52 + 4 * sinf(i * 0.09f);
    app::ingest(t, 1000 + i * 1000);
  }
  app::ingest(s, 62000);
  // Weather
  telemetry::Weather &w = st.weather;
  w.ok = true;
  w.temp = 16.0f;
  w.feels = 13.4f;
  w.humidity = 69;
  w.wind_kmh = 19;
  w.code = 2;
  w.day = true;
  w.today_max = 18;
  w.today_min = 11;
  strcpy(w.sunrise, "07:57");
  strcpy(w.sunset, "18:00");
  const int8_t codes[8] = {2, 3, 61, 80, 3, 1, 0, 0};
  for (int i = 0; i < 8; i++) {
    w.hours[i].hour = static_cast<int8_t>((15 + i) % 24);
    w.hours[i].code = codes[i];
    w.hours[i].temp = static_cast<int8_t>(16 - i / 2);
    w.hours[i].pop = static_cast<int8_t>(i == 2 ? 60 : i == 3 ? 35 : 5);
    w.hours[i].day = i < 3;
  }
  w.hour_count = 8;
  const int8_t dcodes[5] = {3, 61, 95, 1, 0};
  for (int i = 0; i < 5; i++) {
    w.days[i].weekday = static_cast<int8_t>((3 + i) % 7);
    w.days[i].code = dcodes[i];
    w.days[i].max = static_cast<int8_t>(17 + (i % 3) * 2);
    w.days[i].min = static_cast<int8_t>(9 + i % 2);
    w.days[i].pop = static_cast<int8_t>(i == 1 ? 70 : i == 2 ? 85 : 10);
  }
  w.day_count = 5;
  // Network
  net::g_status.wifi_connected = true;
  net::g_status.ws_connected = true;
  net::g_status.paired = true;
  net::g_status.rssi = -54;
  strcpy(net::g_status.ip, "192.168.1.57");
  strcpy(net::g_status.agent_name, "DESKTOP-I9");
  strcpy(storage::g_net.ssid, "MiFibra-2A4C");
  strcpy(storage::g_net.host, "192.168.1.20");
  strcpy(storage::g_net.agent_name, "DESKTOP-I9");
  strcpy(storage::g_net.token, "x");
  const char *ssids[4] = {"MiFibra-2A4C", "MOVISTAR_PLUS_8E1F", "Vodafone-6C3D", "Orange-Invitados"};
  for (int i = 0; i < 4; i++) {
    strcpy(net::g_scan[i].ssid, ssids[i]);
    net::g_scan[i].rssi = static_cast<int8_t>(-48 - i * 9);
    net::g_scan[i].secure = i != 3;
  }
  net::g_scan_n = 4;
  strcpy(net::g_pcs[0].name, "DESKTOP-I9");
  strcpy(net::g_pcs[0].host, "192.168.1.20");
  strcpy(net::g_pcs[1].name, "PORTATIL-ANA");
  strcpy(net::g_pcs[1].host, "192.168.1.34");
  net::g_pc_n = 2;
}

static void fill_crypto() {
  crypto::store_init();
  static crypto::Market m;
  crypto::reset(m);
  m.ok = true;
  strcpy(m.cur, "EUR");
  const char *pairs[6] = {"BTCUSDT", "ETHUSDT", "SOLUSDT", "XRPUSDT", "BNBUSDT", "DOGEUSDT"};
  const double prices[6] = {73891.2, 2412.55, 162.31, 0.51234, 584.1, 0.123456};
  const float chg[6] = {-1.23f, 2.71f, 5.42f, -0.48f, 0.12f, -3.9f};
  const int ranks[6] = {1, 2, 5, 4, 3, 8};
  srand(7);
  for (int i = 0; i < 6; i++) {
    crypto::Coin &c = m.coins[i];
    strcpy(c.pair, pairs[i]);
    c.price = prices[i];
    c.chg_pct = chg[i];
    c.chg_abs = prices[i] * chg[i] / 100.0;
    c.hi = prices[i] * 1.021;
    c.lo = prices[i] * 0.978;
    c.vol = 2.91e10 / (i + 1);
    c.mcap = 1.46e12 / (i * i + 1);
    c.ath = prices[i] * 1.45;
    c.rank = static_cast<int16_t>(ranks[i]);
    float v = 50;
    for (int k = 0; k < 48; k++) {
      v += (rand() % 21 - 10) * 0.9f + (chg[i] > 0 ? 0.8f : -0.8f);
      c.spark[k] = static_cast<uint8_t>(v < 0 ? 0 : v > 100 ? 100 : v);
    }
    c.spark_n = 48;
  }
  m.count = 6;
  m.mcap = 2.91e12;
  m.mcap_chg = 1.21f;
  m.btc_dom = 57.3f;
  m.fng = 63;
  strcpy(m.fng_class, "Greed");
  crypto::store_market(m);

  static crypto::Chart ch;
  crypto::reset(ch);
  ch.ok = true;
  strcpy(ch.pair, "BTCUSDT");
  strcpy(ch.cur, "EUR");
  ch.range = 1;
  float p = 74800;
  const uint32_t t0 = static_cast<uint32_t>(time(nullptr)) - 96 * 900;
  for (int i = 0; i < 96; i++) {
    crypto::Candle &k = ch.c[i];
    k.t = t0 + i * 900;
    k.o = p;
    p += (rand() % 401 - 215) * 1.1f;
    k.c = p;
    k.h = (k.o > k.c ? k.o : k.c) + rand() % 120;
    k.l = (k.o < k.c ? k.o : k.c) - rand() % 120;
    k.v = 2e7f + rand() % 30000000;
  }
  ch.n = 96;
  crypto::store_chart(ch);
}

int main(int argc, char **argv) {
  if (argc > 1) g_out = argv[1];
  lv_init();
  g_fb = static_cast<lv_color_t *>(malloc(kW * kH * sizeof(lv_color_t)));
  lv_disp_draw_buf_init(&g_draw, g_fb, nullptr, kW * kH);
  lv_disp_drv_init(&g_drv);
  g_drv.hor_res = kW;
  g_drv.ver_res = kH;
  g_drv.flush_cb = flush_cb;
  g_drv.draw_buf = &g_draw;
  g_drv.direct_mode = 1;
  g_drv.render_start_cb = ui::skip_covered_screen;
  lv_disp_drv_register(&g_drv);
  lv_indev_drv_init(&g_indev);
  g_indev.type = LV_INDEV_TYPE_POINTER;
  g_indev.read_cb = read_cb;
  lv_indev_drv_register(&g_indev);

  const bool en = argc > 2 && !strcmp(argv[2], "en");
  app::state().settings.lang = en ? storage::Lang::En : storage::Lang::Es;
  ui::set_lang(app::state().settings.lang);
  fill_state();
  fill_crypto();
  ui::theme::init();
  ui::build();
  run(500);

  ui::navigate(ui::Page::Home);
  shot("01_monitor");
  ui::navigate(ui::Page::DetailCpu);
  shot("02_cpu");
  ui::navigate(ui::Page::Thermals);
  shot("03_thermals");
  ui::navigate(ui::Page::Controls);
  shot("04_player");
  ui::navigate(ui::Page::Today);
  shot("05_today");
  {
    // Weather backgrounds (Settings > Time & weather): some groups, day / night, cold / warm veil.
    struct Wx {
      int code;
      bool day;
      float temp;
      const char *name;
    };
    const Wx kWx[] = {{0, true, 33.0f, "50_wx_clear_hot"},     {61, false, 9.0f, "51_wx_rain_night"},
                      {71, true, -2.0f, "52_wx_snow_cold"},    {95, false, 14.0f, "53_wx_storm_night"},
                      {45, true, 8.0f, "54_wx_fog"},           {0, false, 12.0f, "55_wx_clear_night"}};
    auto &stw = app::state().weather;
    for (const Wx &x : kWx) {
      stw.code = x.code;
      stw.day = x.day;
      stw.temp = x.temp;
      ui::on_weather();
      shot(x.name);
    }
    stw.code = 2;
    stw.day = true;
    stw.temp = 16.0f;
    ui::on_weather();
  }
  ui::navigate(ui::Page::Crypto);
  shot("06_crypto");
  ui::crypto_detail::show(0);
  shot("07_coin");
  // Finger on the chart (page origin: 12, 46 + 6; chart at y 120): crosshair + price follow.
  g_pt.x = 12 + 150;
  g_pt.y = 46 + 6 + 120 + 60;
  g_pressed = true;
  run(200);
  g_pt.x += 40;
  run(200);
  shot("08_coin_scrub");
  g_pressed = false;
  run(200);
  app::state().settings.crypto_style = 1;
  ui::crypto_detail::show(0);
  shot("09_coin_candles");
  app::state().settings.crypto_style = 0;
  ui::crypto_detail::show(1);  // no chart cached: loading state
  shot("10_coin_loading");
  ui::navigate(ui::Page::Settings);
  for (int i = 0; i < 9; i++) {
    ui::dev_scroll(i * 330);
    char n[32];
    snprintf(n, sizeof(n), "11_settings_%d", i);
    shot(n);
  }
  ui::dev_scroll(0);
  ui::navigate(ui::Page::Wifi);
  ui::wifi::on_scan();
  shot("12_wifi");
  ui::navigate(ui::Page::Pair);
  ui::pair::on_discovery();
  shot("13_pair");
  top_mark();
  ui::wifi::dev_open_connect("MiFibra-2A4C");
  shot("14_wifi_pass");
  top_clear();
  ui::navigate(ui::Page::Home);
  run(200);
  ui::pair::dev_open_code();
  shot("15_pair_code");
  ui::pair::dev_open_manual();
  shot("16_pair_manual");
  top_clear();
  // ---- More cities for Today (app/cities.h): the main place with its dots, another city (its clock,
  // weather and picture), a swipe to the next one, and the Cities sheet (list, edit mode, add a city).
  {
    auto &st = app::state();
    struct More {
      const char *name;
      float lat, lon;
      int tz, code;
      bool day;
      float temp;
    };
    const More kMore[] = {{"Tokio", 35.69f, 139.69f, 540, 61, false, 9.0f},
                          {"Nueva York", 40.71f, -74.01f, -300, 0, true, 4.0f}};
    for (const More &m : kMore) {
      app::add_city(m.name, m.lat, m.lon);
      telemetry::Weather w = st.weather;  // the same forecast shape, its own conditions
      w.lat = m.lat;
      w.lon = m.lon;
      w.tz_min = static_cast<int16_t>(m.tz);
      w.code = static_cast<int8_t>(m.code);
      w.day = m.day;
      w.temp = m.temp;
      w.feels = m.temp - 2.0f;
      w.today_max = static_cast<int8_t>(m.temp + 3);
      w.today_min = static_cast<int8_t>(m.temp - 4);
      app::ingest_weather(w, g_ms);
    }
    ui::navigate(ui::Page::Today);
    ui::on_weather();
    shot("30_today_cities");
    app::show_city(1);
    shot("31_today_city");
    // A swipe to the left goes to the next city, one to the right comes back; neither taps anything.
    const int sx = ui::theme::kScreenW * 3 / 5, sy = ui::theme::kTopBarH + 84, sd = ui::theme::kScreenW / 4;
    bool bad = false;
    swipe(sx, sy, -sd);
    if (app::today_city() != 2) {
      printf("FAIL cities: a swipe to the left did not show the next city (city %d)\n", app::today_city());
      bad = true;
    }
    shot("32_today_swiped");
    swipe(sx, sy, -sd);  // already the last one: it stays
    swipe(sx - sd, sy, sd);
    if (app::today_city() != 1) {
      printf("FAIL cities: a swipe to the right did not come back (city %d)\n", app::today_city());
      bad = true;
    }
    if (ui::cities::active()) {
      printf("FAIL cities: a swipe opened the Cities sheet\n");
      bad = true;
      ui::cities::close();
      run(50);
    }
    printf("cities swipe: %s\n", bad ? "FAIL" : "ok");
    ui::cities::open();
    shot("33_cities");
    ui::cities::dev_edit(true);
    shot("34_cities_edit");
    // "Make main": Tokio trades places with Madrid, Today keeps showing Tokio (now the main place).
    app::make_main_city(1);
    if (strcmp(app::city_name(0), "Tokio") != 0 || strcmp(app::city_name(1), "Madrid") != 0 || app::today_city() != 0)
      printf("FAIL cities: make main -> '%s', '%s', shown %d\n", app::city_name(0), app::city_name(1), app::today_city());
    app::make_main_city(1);
    app::show_city(0);
    ui::cities::dev_edit(false);
    // The full list with long names and the 12 h clock: nothing may run into its neighbour.
    {
      const More kLong[] = {{"San Francisco", 37.77f, -122.42f, -480, 45, true, 14.0f},
                            {"Ciudad de México", 19.43f, -99.13f, -360, 95, true, 22.0f},
                            {"Buenos Aires", -34.61f, -58.38f, -180, 3, true, -12.0f}};
      for (const More &m : kLong) {
        app::add_city(m.name, m.lat, m.lon);
        telemetry::Weather w = st.weather;
        w.lat = m.lat;
        w.lon = m.lon;
        w.tz_min = static_cast<int16_t>(m.tz);
        w.code = static_cast<int8_t>(m.code);
        w.day = m.day;
        w.temp = m.temp;
        app::ingest_weather(w, g_ms);
      }
      if (app::add_city("Sídney", -33.87f, 151.21f) != -1) printf("FAIL cities: a seventh city was accepted\n");
      st.settings.clock_12h = true;
      ui::on_weather();
      shot("36_cities_full");
      ui::cities::dev_edit(true);
      shot("37_cities_full_edit");
      ui::cities::close();
      run(50);
      app::show_city(4);
      shot("38_today_full");
      st.settings.clock_12h = false;
      app::remove_city(5);
      app::remove_city(4);
      app::remove_city(3);
      if (app::city_count() != 3 || app::today_city() != 0)
        printf("FAIL cities: after removing the shown city, %d left, shown %d\n", app::city_count(), app::today_city());
      ui::cities::open();
    }
    ui::place::open(ui::place::Mode::Add);
    shot("35_city_add");
    ui::cities::close();
    run(50);
    // Back to one city for the rest of the scenes.
    app::remove_city(2);
    app::remove_city(1);
    if (app::city_count() != 1 || strcmp(app::city_name(0), "Madrid") != 0)
      printf("FAIL cities: after removing two, %d left, main '%s'\n", app::city_count(), app::city_name(0));
  }
  ui::navigate(ui::Page::Settings);
  run(100);
  ui::place::dev_search("");
  shot("17_place");
  top_clear();
  ui::coins::open();
  shot("18_coins");
  top_clear();
  ui::navigate(ui::Page::Home);
  ui::confirm(ui::tr(ui::Str::ConfirmDeepSleep), ui::tr(ui::Str::DeepSleepOk), ui::theme::kGpu, nullptr, nullptr);
  shot("19_confirm");
  top_clear();
  ui::setup::open(0);
  shot("20_setup");
  ui::setup::close();
  run(100);
  // Crypto states: no coins chosen, PC offline (no prices yet).
  ui::navigate(ui::Page::Crypto);
  app::set_crypto_coins("");
  shot("21_crypto_empty");
  app::set_crypto_coins("BTC,ETH,SOL,XRP,BNB,DOGE");
  {
    // A synthetic album cover (150 px, like the PC app sends): the screensaver's music card shows it.
    app::ArtFrame art;
    art.art_id = 1;
    art.width = art.height = 150;
    art.data = static_cast<uint8_t *>(malloc(150 * 150 * 2));
    for (int y = 0; y < 150; y++)
      for (int x = 0; x < 150; x++) {
        const int r = 40 + x * 160 / 150, g = 30 + (x + y) * 40 / 300, b = 120 + y * 120 / 150;
        const uint16_t v = static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        art.data[(y * 150 + x) * 2] = static_cast<uint8_t>(v >> 8);  // big-endian (LV_COLOR_16_SWAP)
        art.data[(y * 150 + x) * 2 + 1] = static_cast<uint8_t>(v & 0xFF);
      }
    ui::on_art(art);
  }
  // Screensaver: its Settings section (whole, in two frames), the wallpaper picker, then the
  // screensaver on a few images (the last one with the 12 h clock).
  {
    storage::Screensaver ss = app::state().settings.ss;
    ss.enabled = true;
    ss.bg_mode = 0;  // fixed
    ss.bg = 0;
    app::set_screensaver(ss);
  }
  ui::navigate(ui::Page::Settings);
  ui::settings::on_screensaver();
  ui::dev_scroll(330);  // auto sleep: greyed out, replaced by the screensaver
  shot("22_settings_autosleep");
  ui::dev_scroll(SS_SCROLL);
  shot("22_settings_screensaver_0");
  ui::dev_scroll(SS_SCROLL + 340);
  shot("22_settings_screensaver_1");
  ui::dev_scroll(0);
  ui::screensaver::open_picker();
  shot("23_ss_picker");
  ui::screensaver::on_rebuild();  // closes the picker through its own path
  run(100);
  const int pics[4] = {0, 4, 6, 5};  // aurora, city, network, dunes (12 h)
  for (int i = 0; i < 4; i++) {
    app::state().settings.ss.bg = static_cast<uint8_t>(pics[i]);
    app::state().settings.clock_12h = i == 3;
    ui::screensaver::start();
    run(1600);
    char n[32];
    snprintf(n, sizeof(n), "24_screensaver_%d", i);
    shot(n);
    ui::screensaver::stop();
    run(100);
  }
  app::state().settings.clock_12h = false;
  // Deck battery in the corner (as if on battery; the real decks show it only then): icon and %,
  // icon only, and a nearly empty battery.
  {
    auto &ss = app::state().settings.ss;
    const uint8_t was = ss.battery;
    const struct {
      int pct;
      uint8_t mode;
      const char *name;
    } kBat[] = {{78, 2, "27_ss_battery"}, {64, 1, "27_ss_battery_icon"}, {8, 2, "27_ss_battery_low"}};
    for (const auto &b : kBat) {
      ss.battery = b.mode;
      ui::battery_badge_dev(b.pct);
      ui::screensaver::start();
      run(1600);
      shot(b.name);
      ui::screensaver::stop();
      run(100);
    }
    ui::battery_badge_dev(-1);
    ss.battery = was;
  }
  // Shortcuts: a finger held on the music card (pressed state) then sliding off (just closes); a tap
  // elsewhere (closes, Settings stays); a tap on each card / the weather line opens its page.
  // Positions with the content box at (16, 16): cards stacked up from y 464 (64 px + 8 px gaps),
  // weather line around y 188.
  app::state().settings.ss.bg = 0;
  ui::screensaver::start();
  run(1600);
  g_pt.x = 160;
  g_pt.y = 360;
  g_pressed = true;
  run(200);
  shot("25_ss_pressed");
  g_pt.y = 240;  // slides off the card: PRESS_LOST
  run(200);
  g_pressed = false;
  run(200);
  shot("25_ss_slid_off");
  struct Tap {
    int x, y;
    const char *name;
  };
  const Tap taps[5] = {{160, 100, "26_ss_tap_clock"},
                       {160, 432, "26_ss_tap_coin"},
                       {160, 360, "26_ss_tap_music"},
                       {160, 288, "26_ss_tap_pc"},
                       {160, 188, "26_ss_tap_weather"}};
  for (const Tap &t : taps) {
    ui::screensaver::start();
    run(1600);
    tap(t.x, t.y);
    printf("%s: screensaver %s, page %d\n", t.name, ui::screensaver::active() ? "on" : "off",
           static_cast<int>(ui::current_page()));
    shot(t.name);
  }
  // ---- No PC, only Wi-Fi: the PC tabs leave the bar, the PC page that was open gives way to the first
  // tab that works, weather and crypto stay, and the screensaver shows only what it has.
  {
    if (ui::screensaver::active()) ui::screensaver::stop();
    auto &st = app::state();
    const auto link = st.link, net_link = st.net_link;
    st.link = st.net_link = app::LinkState::Discovering;
    st.pc_present = false;
    ui::navigate(ui::Page::Home);
    ui::on_link();
    ui::on_pc_presence();
    printf("nopc: page %d (Home = 0)\n", static_cast<int>(ui::current_page()));
    if (ui::current_page() == ui::Page::Home) printf("FAIL nopc: still on the Monitor page\n");
    shot("40_nopc_first_tab");
    ui::navigate(ui::Page::Crypto);
    shot("41_nopc_crypto");
    ui::screensaver::start();
    run(1600);  // fade-in
    shot("42_nopc_saver");
    ui::screensaver::stop();
    st.link = link;
    st.net_link = net_link;
    st.pc_present = true;
    ui::on_link();
    ui::on_pc_presence();
    ui::navigate(ui::Page::Home);
  }
  // ---- Status area (top right): a double tap starts the screensaver when it is enabled; a single tap
  // does not, and with the LED grey it still leads to the fix once the double-tap window has passed.
  {
    auto &st = app::state();
    const auto ss = st.settings.ss;
    const auto link = st.link;
    const int x = 457, y = 22;  // the status LED
    bool bad = false;
    ui::navigate(ui::Page::Home);
    run(100);
    st.settings.ss.enabled = true;
    tap(x, y);
    tap(x, y);
    run(100);
    if (!ui::screensaver::active()) {
      printf("FAIL status: a double tap did not start the screensaver\n");
      bad = true;
    }
    ui::screensaver::stop();
    run(600);
    tap(x, y);
    run(600);
    if (ui::screensaver::active()) {
      printf("FAIL status: a single tap started the screensaver\n");
      bad = true;
      ui::screensaver::stop();
    }
    st.settings.ss.enabled = false;
    tap(x, y);
    tap(x, y);
    run(600);
    if (ui::screensaver::active()) {
      printf("FAIL status: the screensaver is off but a double tap started it\n");
      bad = true;
      ui::screensaver::stop();
    }
    st.settings.ss.enabled = true;
    st.link = app::LinkState::NoWifi;
    ui::on_link();
    run(100);
    tap(x, y);
    run(600);
    if (ui::current_page() != ui::Page::Wifi) {
      printf("FAIL status: a tap on the grey LED did not open Wi-Fi\n");
      bad = true;
    }
    st.link = link;
    st.settings.ss = ss;
    ui::on_link();
    ui::navigate(ui::Page::Home);
    run(100);
    printf("status double tap: %s\n", bad ? "FAIL" : "ok");
  }
  return 0;
}
