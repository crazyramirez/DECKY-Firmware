// Desktop UI simulator: renders every page of the deck UI into a 1024x600 framebuffer with
// realistic fake data and dumps each frame (little-endian RGB565, as on the panel) to out/<name>.rgb.
#include <lvgl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app/app_controller.h"
#include "app/state_store.h"
#include "crypto/coin_catalog.h"
#include "crypto/crypto_store.h"
#include "network/net_task.h"
#include "storage/net_store.h"
#include "ui/i18n.h"
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

static uint32_t g_ms = 1000;
extern "C" uint32_t millis(void) { return g_ms; }
extern "C" void delay(uint32_t ms) { g_ms += ms; }

static constexpr int kW = ui::theme::kScreenW, kH = ui::theme::kScreenH;
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
  // Finger on the chart: crosshair + header follow.
  g_pt.x = ui::theme::kNavW + ui::theme::kMargin + 300;
  g_pt.y = ui::theme::kTopBarH + 15 + 114 + 150;
  g_pressed = true;
  run(200);
  g_pt.x += 80;
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
  for (int i = 0; i < 8; i++) {
    ui::dev_scroll(i * 440);
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
  ui::navigate(ui::Page::Settings);
  run(100);
  ui::place::dev_search("");
  shot("17_place");
  top_clear();
  ui::coins::open();
  shot("18_coins");
  top_clear();
  ui::navigate(ui::Page::Home);
  ui::confirm(ui::tr(ui::Str::ConfirmRestart), ui::tr(ui::Str::Restart), ui::theme::kWarning, nullptr, nullptr);
  shot("19_confirm");
  top_clear();
  // Screensaver: its Settings section, the wallpaper picker, then the screensaver on a few images.
  {
    storage::Screensaver ss = app::state().settings.ss;
    ss.enabled = true;
    ss.bg_mode = 0;  // fixed
    ss.bg = 0;
    app::set_screensaver(ss);
  }
  ui::navigate(ui::Page::Settings);
  ui::settings::on_screensaver();
  ui::dev_scroll(1010);
  shot("22_settings_screensaver");
  // "Show" chips: every tap must store the mask the chips display (it once lagged one tap behind).
  {
    struct Step { int x; uint8_t want; };
    const uint8_t start = app::state().settings.ss.show;
    const Step steps[] = {{471, uint8_t(start ^ 2)}, {650, uint8_t(start ^ 6)}, {471, uint8_t(start ^ 4)},
                          {245, uint8_t(start ^ 5)}, {245, uint8_t(start ^ 4)}, {650, start}};
    int bad = 0;
    for (const Step &s : steps) {
      tap(s.x, 328);
      const uint8_t got = app::state().settings.ss.show;
      if (got != s.want) {
        printf("FAIL ss.show after tap x=%d: got 0x%02X want 0x%02X\n", s.x, got, s.want);
        bad++;
      }
    }
    printf("ss.show chips: %s\n", bad ? "FAIL" : "ok");
    tap(471, 328);  // PC off, for the shot
    shot("22b_settings_ss_show");
    tap(471, 328);
  }
  ui::dev_scroll(0);
  ui::screensaver::open_picker();
  shot("23_ss_picker");
  ui::screensaver::on_rebuild();  // closes the picker through its own path
  run(100);
  const int pics[3] = {0, 4, 6};
  for (int i = 0; i < 3; i++) {
    app::state().settings.ss.bg = static_cast<uint8_t>(pics[i]);
    app::state().settings.clock_12h = i == 2;
    ui::screensaver::start();
    run(1600);
    char n[32];
    snprintf(n, sizeof(n), "24_screensaver_%d", i);
    shot(n);
    ui::screensaver::stop();
    run(100);
  }
  app::state().settings.clock_12h = false;
  // Tapping a card opens its page: music card -> Player, crypto card -> that coin's chart.
  ui::screensaver::start();
  run(1600);
  tap(512, 502);
  run(300);
  shot("25_ss_tap_music");
  ui::screensaver::start();
  run(1600);
  tap(830, 502);
  run(300);
  shot("26_ss_tap_coin");
  ui::navigate(ui::Page::Home);
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
  ui::setup::open();  // Wi-Fi + PC already connected: starts at the city step
  shot("21_setup_city");
  net::g_status.wifi_connected = false;
  ui::setup::open();  // first power-on: language step
  shot("20_setup");
  return 0;
}
