// Player: album art, title/artist/source, progress, transport and volume — a standalone-music-app
// layout driven by the Windows media session (Spotify, browser players, VLC...).
//
// Feedback rules: the UI reflects the PC state from the next snapshot, so successful commands are
// silent; only failures raise a toast. Album art arrives as RGB565 big-endian (LVGL native format).

#include <stdio.h>
#include <string.h>

#include "app/state_store.h"
#include "commands/command_client.h"
#include "esp_heap_caps.h"
#include "ui/ui.h"
#include "ui/ui_internal.h"

namespace ui {
namespace controls {
namespace {

using namespace theme;
using commands::Action;

constexpr int kArt = 150;  // the agent sends 150 px covers: shown 1:1, clipped by the rounded box

// Landscape (content 416 x 242): cover on the left, everything else in a right column.
constexpr int kXArt = 14;
constexpr int kYArt = (theme::kContentH - kArt) / 2;  // 46
constexpr int kXCol = kXArt + kArt + 16;              // 180
constexpr int kColW = theme::kContentW - kXCol - 14;  // 222
constexpr int kColMid = kXCol + kColW / 2;            // centre of the right column
constexpr int kYTitle = 14;
constexpr int kYArtist = kYTitle + 28;  // 42
constexpr int kYSource = kYArtist + 24; // 66
constexpr int kYBar = kYSource + 30;    // 96
constexpr int kYTimes = kYBar + 8;      // 104
constexpr int kPlay = 54;
constexpr int kSkip = 44;
constexpr int kYPlay = kYTimes + 24;    // 128
constexpr int kYVol = kYPlay + kPlay + 18;  // 200

lv_obj_t *s_page = nullptr;
lv_obj_t *s_art_box = nullptr;
lv_obj_t *s_art_img = nullptr;
lv_obj_t *s_art_ph = nullptr;
lv_obj_t *s_title = nullptr;
lv_obj_t *s_artist = nullptr;
lv_obj_t *s_source = nullptr;
lv_obj_t *s_bar = nullptr;
lv_obj_t *s_t_pos = nullptr;
lv_obj_t *s_t_dur = nullptr;
lv_obj_t *s_prev = nullptr;
lv_obj_t *s_play = nullptr;
lv_obj_t *s_play_lbl = nullptr;
lv_obj_t *s_next = nullptr;
lv_obj_t *s_vol = nullptr;
lv_obj_t *s_vol_icon = nullptr;
lv_obj_t *s_vol_val = nullptr;

lv_img_dsc_t s_dsc;
uint8_t *s_art_data = nullptr;
uint32_t s_art_id = 0;
uint32_t s_accent = 0xFFFFFFFF;
uint32_t s_vol_pending_until = 0;
uint32_t s_seek_pending_until = 0;

void fmt_time(char *buf, size_t n, int32_t s) {
  if (s < 0) {
    snprintf(buf, n, "--:--");
    return;
  }
  snprintf(buf, n, "%d:%02d", static_cast<int>(s / 60), static_cast<int>(s % 60));
}

void dispatch(Action a, int32_t value = -1) {
  if (!app::controls_available()) return;
  if (commands::send(a, value) == 0) ui::toast(tr(Str::CmdFailed), kCritical);
}

void action_cb(lv_event_t *e) { dispatch(static_cast<Action>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)))); }

// Volume follows the finger: while dragging, changes go to the PC at most every kVolLiveMs (and
// only with a free command slot, silently); releasing always sends the final value.
constexpr uint32_t kVolLiveMs = 150;  // ~6/s: under the PC app's 8 commands/s limit, room for the final one
uint32_t s_vol_sent_at = 0;
int32_t s_vol_sent = -1;

void vol_icon(int32_t v) {
  set_text(s_vol_icon, v == 0 ? LV_SYMBOL_MUTE : v < 50 ? LV_SYMBOL_VOLUME_MID : LV_SYMBOL_VOLUME_MAX);
}

void vol_changed_cb(lv_event_t *e) {
  const int32_t v = lv_slider_get_value(lv_event_get_target(e));
  char buf[12];
  snprintf(buf, sizeof(buf), "%d", static_cast<int>(v));
  set_text(s_vol_val, buf);
  vol_icon(v);
  s_vol_pending_until = lv_tick_get() + 3000;  // don't let a snapshot snap the knob back
  const uint32_t now = lv_tick_get();
  if (v != s_vol_sent && now - s_vol_sent_at >= kVolLiveMs && app::controls_available() &&
      commands::pending_count() < 3) {
    if (commands::send(Action::VolumeSet, v) != 0) {
      s_vol_sent = v;
      s_vol_sent_at = now;
    }
  }
}

void vol_release_cb(lv_event_t *e) {
  const int32_t v = lv_slider_get_value(lv_event_get_target(e));
  s_vol_pending_until = lv_tick_get() + 3000;
  if (v != s_vol_sent) dispatch(Action::VolumeSet, v);
  s_vol_sent = -1;  // next drag starts fresh (the PC may have changed the volume meanwhile)
}

// Position slider: dragging previews the time, releasing asks the PC to seek there.
void seek_changed_cb(lv_event_t *e) {
  char buf[12];
  fmt_time(buf, sizeof(buf), lv_slider_get_value(lv_event_get_target(e)));
  set_text(s_t_pos, buf);
}

void seek_release_cb(lv_event_t *e) {
  // Players report the new position with some delay: keep the knob where the user left it.
  s_seek_pending_until = lv_tick_get() + 3000;
  dispatch(Action::MediaSeek, lv_slider_get_value(lv_event_get_target(e)));
}

lv_obj_t *round_button(lv_obj_t *parent, int size, const char *sym, const lv_font_t *font, bool primary, Action a) {
  lv_obj_t *b = lv_btn_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, size, size);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(b, primary ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_color(b, c(kText), 0);
  lv_obj_set_style_bg_color(b, c(primary ? 0xC9D3E3 : kSurfaceHi), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_opa(b, LV_OPA_40, LV_STATE_DISABLED);
  lv_obj_set_ext_click_area(b, 8);
  lv_obj_t *l = make_label(b, font, primary ? kBg : kText, sym);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, action_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(a)));
  if (primary) s_play_lbl = l;
  return b;
}

void set_enabled(lv_obj_t *o, bool en) {
  if (en)
    lv_obj_clear_state(o, LV_STATE_DISABLED);
  else
    lv_obj_add_state(o, LV_STATE_DISABLED);
}

void apply_accent(uint32_t rgb) {
  if (rgb == s_accent) return;
  s_accent = rgb;
  const lv_color_t top = rgb ? lv_color_mix(c(rgb), c(kBg), 150) : c(kSurface);
  lv_obj_set_style_bg_color(s_page, top, 0);
  lv_obj_set_style_bg_grad_color(s_page, c(kBg), 0);
}

void show_art(bool on) {
  if (on) {
    lv_obj_clear_flag(s_art_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_art_ph, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(s_art_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_art_ph, LV_OBJ_FLAG_HIDDEN);
  }
}

}  // namespace

lv_obj_t *create(lv_obj_t *parent) {
  s_page = lv_obj_create(parent);
  lv_obj_remove_style_all(s_page);
  lv_obj_set_size(s_page, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(s_page, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_grad_dir(s_page, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_bg_main_stop(s_page, 0, 0);
  lv_obj_set_style_bg_grad_stop(s_page, 220, 0);
  lv_obj_clear_flag(s_page, LV_OBJ_FLAG_SCROLLABLE);
  apply_accent(0);

  // Album art (rounded, clipped) with a placeholder note.
  s_art_box = lv_obj_create(s_page);
  lv_obj_remove_style_all(s_art_box);
  lv_obj_set_size(s_art_box, kArt, kArt);
  lv_obj_set_pos(s_art_box, kXArt, kYArt);
  lv_obj_set_style_radius(s_art_box, 14, 0);
  lv_obj_set_style_clip_corner(s_art_box, true, 0);
  lv_obj_set_style_bg_color(s_art_box, c(kSurfaceHi), 0);
  lv_obj_set_style_bg_opa(s_art_box, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(s_art_box, c(kBorder), 0);
  lv_obj_set_style_border_width(s_art_box, 1, 0);
  lv_obj_clear_flag(s_art_box, LV_OBJ_FLAG_SCROLLABLE);
  s_art_ph = make_label(s_art_box, font_value(), kBorder, LV_SYMBOL_AUDIO);  // 28 px carries the icons
  lv_obj_center(s_art_ph);
  s_art_img = lv_img_create(s_art_box);
  lv_obj_center(s_art_img);
  lv_obj_add_flag(s_art_img, LV_OBJ_FLAG_HIDDEN);
  if (s_art_data) lv_img_set_src(s_art_img, &s_dsc);  // UI rebuilt (language): keep the cover

  // Title / artist / source
  // Single-line labels: fixed height = one line; text that does not fit scrolls slowly in a loop
  // (LVGL only animates when it overflows, and set_text() skips identical updates so it never restarts).
  s_title = make_label(s_page, font_title(), kText, "");
  lv_obj_set_size(s_title, kColW, lv_font_get_line_height(font_title()));
  lv_label_set_long_mode(s_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_anim_speed(s_title, 28, 0);  // px/s
  lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(s_title, kXCol, kYTitle);
  s_artist = make_label(s_page, font_body(), kMuted, "");
  lv_obj_set_size(s_artist, kColW, lv_font_get_line_height(font_body()));
  lv_label_set_long_mode(s_artist, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_anim_speed(s_artist, 28, 0);  // px/s
  lv_obj_set_style_text_align(s_artist, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(s_artist, kXCol, kYArtist);
  s_source = make_label(s_page, font_small(), kMuted, "");
  lv_obj_set_width(s_source, kColW);
  lv_label_set_long_mode(s_source, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(s_source, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(s_source, kXCol, kYSource);

  // Progress
  // Progress / seek: thin track, the knob only appears while the finger is on it.
  s_bar = lv_slider_create(s_page);
  style_slider(s_bar, kText);
  lv_obj_set_size(s_bar, kColW - 8, 4);
  lv_obj_set_style_bg_color(s_bar, c(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_bar, LV_OPA_20, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_bar, 0, LV_PART_KNOB);
  lv_obj_set_style_bg_opa(s_bar, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_set_style_pad_all(s_bar, 7, LV_PART_KNOB | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_KNOB | LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(s_bar, c(kOk), LV_PART_INDICATOR | LV_STATE_PRESSED);
  lv_obj_set_ext_click_area(s_bar, 16);
  lv_slider_set_range(s_bar, 0, 1);
  lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(s_bar, kXCol + 4, kYBar);
  lv_obj_add_event_cb(s_bar, seek_changed_cb, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(s_bar, seek_release_cb, LV_EVENT_RELEASED, nullptr);
  s_t_pos = make_label(s_page, font_tiny(), kMuted, "");
  lv_obj_set_pos(s_t_pos, kXCol + 2, kYTimes);
  s_t_dur = make_label(s_page, font_tiny(), kMuted, "");
  lv_obj_set_width(s_t_dur, 60);
  lv_obj_set_style_text_align(s_t_dur, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(s_t_dur, kXCol + kColW - 62, kYTimes);

  // Transport
  s_prev = round_button(s_page, kSkip, LV_SYMBOL_PREV, font_title(), false, Action::MediaPrevious);
  lv_obj_set_pos(s_prev, kColMid - 76 - kSkip / 2, kYPlay + (kPlay - kSkip) / 2);
  s_play = round_button(s_page, kPlay, LV_SYMBOL_PLAY, font_value(), true, Action::MediaPlayPause);
  lv_obj_set_pos(s_play, kColMid - kPlay / 2, kYPlay);
  s_next = round_button(s_page, kSkip, LV_SYMBOL_NEXT, font_title(), false, Action::MediaNext);
  lv_obj_set_pos(s_next, kColMid + 76 - kSkip / 2, kYPlay + (kPlay - kSkip) / 2);

  // Volume
  s_vol_icon = make_label(s_page, font_body(), kMuted, LV_SYMBOL_VOLUME_MAX);
  lv_obj_set_pos(s_vol_icon, kXCol, kYVol);
  s_vol = lv_slider_create(s_page);
  style_slider(s_vol, kText);
  lv_obj_set_style_bg_color(s_vol, c(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_vol, LV_OPA_20, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_vol, 6, LV_PART_KNOB);
  lv_obj_set_size(s_vol, kColW - 72, 6);
  lv_obj_set_pos(s_vol, kXCol + 32, kYVol + 9);
  lv_slider_set_range(s_vol, 0, 100);
  lv_obj_set_style_opa(s_vol, LV_OPA_40, LV_STATE_DISABLED);
  lv_obj_add_event_cb(s_vol, vol_changed_cb, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(s_vol, vol_release_cb, LV_EVENT_RELEASED, nullptr);
  s_vol_val = make_label(s_page, font_small(), kMuted, "");
  lv_obj_set_width(s_vol_val, 30);
  lv_obj_set_style_text_align(s_vol_val, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(s_vol_val, kXCol + kColW - 30, kYVol + 1);

  update();
  on_telemetry();
  return s_page;
}

void update() {
  if (!s_page) return;
  const auto &st = app::state();
  const bool online = app::controls_available();
  set_enabled(s_prev, online && st.caps.media);
  set_enabled(s_play, online && st.caps.media);
  set_enabled(s_next, online && st.caps.media);
  set_enabled(s_vol, online && st.caps.volume);
}

void on_telemetry() {
  if (!s_page) return;
  const auto &st = app::state();
  const auto &s = st.snap;
  const bool online = app::controls_available();
  const bool real = st.has_snapshot && !st.settings.demo_mode;
  const bool media = real && s.has_media && (s.media_title[0] || s.media_app[0]);

  if (!online && !st.settings.demo_mode) {
    set_text(s_title, tr(Str::ControlsOffline));
    set_text(s_artist, "");
    set_text(s_source, "");
  } else if (media) {
    set_text(s_title, s.media_title[0] ? s.media_title : s.media_app);
    set_text(s_artist, s.media_artist);
    char src[40];
    snprintf(src, sizeof(src), "%s %s", s.media_playing ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE, s.media_app);
    set_text(s_source, s.media_app[0] ? src : "");
  } else {
    set_text(s_title, tr(Str::NothingPlaying));
    set_text(s_artist, tr(Str::PlayerHint));
    set_text(s_source, "");
  }

  // Artwork + background accent follow the current track.
  const bool art_ok = media && s.media_art_id != 0 && s_art_data && s.media_art_id == s_art_id;
  show_art(art_ok);
  apply_accent(media && s.media_accent ? s.media_accent : 0);

  // Progress
  char a[12], b[12];
  const bool timed = media && s.media_dur_s > 0 && s.media_pos_s >= 0;
  fmt_time(a, sizeof(a), timed ? s.media_pos_s : -1);
  fmt_time(b, sizeof(b), timed ? s.media_dur_s : -1);
  const bool seekable = timed && online && st.caps.seek && s.media_can_seek;
  if (seekable)
    lv_obj_add_flag(s_bar, LV_OBJ_FLAG_CLICKABLE);
  else
    lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_CLICKABLE);
  const bool touching = lv_obj_has_state(s_bar, LV_STATE_PRESSED);
  const bool holding = static_cast<int32_t>(lv_tick_get() - s_seek_pending_until) < 0;
  if (!touching && !holding) {
    set_text(s_t_pos, timed ? a : "");
    if (timed) {
      if (lv_slider_get_max_value(s_bar) != s.media_dur_s) lv_slider_set_range(s_bar, 0, s.media_dur_s);
      if (lv_slider_get_value(s_bar) != s.media_pos_s) lv_slider_set_value(s_bar, s.media_pos_s, LV_ANIM_OFF);
    } else if (lv_slider_get_value(s_bar) != 0) {
      lv_slider_set_value(s_bar, 0, LV_ANIM_OFF);
    }
  }
  set_text(s_t_dur, timed ? b : "");

  // Play/pause shows the action it will perform.
  const bool playing = media && s.media_playing;
  set_text(s_play_lbl, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
  // The play triangle's visual centre sits right of its glyph box centre: nudge it (optical centring).
  lv_obj_align(s_play_lbl, LV_ALIGN_CENTER, playing ? 0 : 3, 0);

  // Volume follows the PC unless the user is touching it or just changed it.
  if (real && s.volume_pct >= 0 && !lv_obj_has_state(s_vol, LV_STATE_PRESSED) &&
      static_cast<int32_t>(lv_tick_get() - s_vol_pending_until) >= 0) {
    if (lv_slider_get_value(s_vol) != s.volume_pct) lv_slider_set_value(s_vol, s.volume_pct, LV_ANIM_OFF);
    char v[8];
    snprintf(v, sizeof(v), "%d", s.volume_pct);
    set_text(s_vol_val, v);
    set_text(s_vol_icon, s.muted || s.volume_pct == 0 ? LV_SYMBOL_MUTE
                         : s.volume_pct < 50          ? LV_SYMBOL_VOLUME_MID
                                                      : LV_SYMBOL_VOLUME_MAX);
  }
}

void on_art(const app::ArtFrame &art) {
  if (!s_art_img) {
    heap_caps_free(art.data);
    return;
  }
  uint8_t *old = s_art_data;
  memset(&s_dsc, 0, sizeof(s_dsc));
  s_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  s_dsc.header.w = art.width;
  s_dsc.header.h = art.height;
  s_dsc.data_size = static_cast<uint32_t>(art.width) * art.height * 2;
  s_dsc.data = art.data;
  s_art_data = art.data;
  s_art_id = art.art_id;
  lv_img_set_src(s_art_img, &s_dsc);  // LVGL image cache is disabled: nothing else references `old`
  lv_obj_invalidate(s_art_img);
  if (old) heap_caps_free(old);
  on_telemetry();
}

// The cover of the track that is playing now (the screensaver's music card); nullptr when the art
// held here belongs to another track or there is none.
const lv_img_dsc_t *cover() {
  const auto &s = app::state().snap;
  return s_art_data && s_art_id != 0 && s.media_art_id == s_art_id ? &s_dsc : nullptr;
}

void on_result(const app::CommandResult &r) {
  const bool known = commands::resolve(r.request_id);
  if (!known && r.status != app::CmdStatus::Timeout) return;  // unknown / duplicate / late
  if (r.status == app::CmdStatus::Ack) {
    // Media/volume are silent (the screen reflects the PC). System actions get a confirmation.
    if (!strcmp(r.action, "session.lock")) ui::toast(tr(Str::Locking), kOk);
    if (!strcmp(r.action, "system.shutdown")) ui::toast(tr(Str::ShuttingDown), kWarning);
    if (!strcmp(r.action, "system.restart")) ui::toast(tr(Str::Restarting), kWarning);
    return;
  }
  char buf[96];
  snprintf(buf, sizeof(buf), "%s %s", LV_SYMBOL_WARNING,
           r.status == app::CmdStatus::Timeout ? tr(Str::CmdTimeout)
           : !strcmp(r.error, "offline")       ? tr(Str::ControlsOffline)
                                               : tr(Str::CmdFailed));
  ui::toast(buf, kCritical);
}

}  // namespace controls
}  // namespace ui
