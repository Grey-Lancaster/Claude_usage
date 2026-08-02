#include "UsageDashboard.h"

namespace UsageDashboard {
namespace {

lv_obj_t *mascot = nullptr;                // header badge - see wiggleMascot() below
lv_obj_t *statusLabel = nullptr;           // left slot - "Updated" or a single-message status
lv_obj_t *statusCountdownLabel = nullptr;  // middle slot on wide displays, right slot on narrow ones
lv_obj_t *statusUptimeLabel = nullptr;     // right slot, wide displays only - stays nullptr on narrow ones
lv_obj_t *lockOverlay = nullptr;
lv_obj_t *lockDetailLabel = nullptr;
bool everValid = false;
std::function<void()> onSettingsClicked;
std::function<void()> onBackgroundClicked;

struct Meter {
  lv_obj_t *nameLabel = nullptr;
  lv_obj_t *valueLabel = nullptr;
  lv_obj_t *bar = nullptr;
  lv_obj_t *subLabel = nullptr;
};

Meter sessionMeter;
Meter weeklyMeter;
Meter creditsMeter;

// Same three-tier severity break every meter uses, rather than a fixed
// bar color - a 92%-used session bar should read as urgent at a glance,
// not require reading the number next to it.
lv_color_t severityColor(int percent) {
  if (percent >= 90) return lv_palette_main(LV_PALETTE_RED);
  if (percent >= 70) return lv_palette_main(LV_PALETTE_ORANGE);
  return lv_palette_main(LV_PALETTE_BLUE);
}

// UsageDashboard is deliberately board-agnostic (this file never touches
// hardware directly - see the header comment), so board-specific font
// scaling can't be a compile-time #ifdef here without breaking that. The
// screen width of the actual `parent` object handed to build() already
// reflects whichever display got registered though, so that's the signal
// used instead - true only for the CrowPanel Advance 7" (800px wide) vs
// the CYD (320px). 480 is a clean cutover point between them.
bool largeDisplay = false;

void styleCard(lv_obj_t *card) {
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  // lv_obj_create() defaults every plain object to clickable (base
  // constructor sets LV_OBJ_FLAG_CLICKABLE unconditionally - remove_style_all
  // above only strips styles, not flags). Left on, this card - not root -
  // becomes the hit-test target for taps landing on it, so tap-to-refresh
  // silently did nothing over most of the screen. Cleared here so the tap
  // falls through to root's own clickable flag/handler instead.
  lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x1c1c1c), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, largeDisplay ? 16 : 10, 0);
  lv_obj_set_style_pad_all(card, largeDisplay ? 14 : 8, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
}

Meter makeMeter(lv_obj_t *parent, const char *name) {
  Meter m;

  lv_obj_t *card = lv_obj_create(parent);
  styleCard(card);

  lv_obj_t *headerRow = lv_obj_create(card);
  lv_obj_remove_style_all(headerRow);
  lv_obj_set_size(headerRow, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_clear_flag(headerRow, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(headerRow, LV_OBJ_FLAG_CLICKABLE);  // see styleCard() comment
  lv_obj_set_flex_flow(headerRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(headerRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  m.nameLabel = lv_label_create(headerRow);
  lv_label_set_text(m.nameLabel, name);
  lv_obj_set_style_text_color(m.nameLabel, lv_color_white(), 0);
  if (largeDisplay) lv_obj_set_style_text_font(m.nameLabel, &lv_font_montserrat_24, 0);

  m.valueLabel = lv_label_create(headerRow);
  lv_label_set_text(m.valueLabel, "--");
  lv_obj_set_style_text_color(m.valueLabel, lv_color_white(), 0);
  if (largeDisplay) lv_obj_set_style_text_font(m.valueLabel, &lv_font_montserrat_24, 0);

  m.bar = lv_bar_create(card);
  lv_obj_clear_flag(m.bar, LV_OBJ_FLAG_CLICKABLE);  // lv_bar doesn't clear this itself either; see styleCard() comment
  lv_obj_set_size(m.bar, lv_pct(100), largeDisplay ? 18 : 10);
  lv_obj_set_style_pad_top(m.bar, largeDisplay ? 8 : 6, 0);
  lv_obj_set_style_bg_color(m.bar, lv_color_hex(0x3a3a3a), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(m.bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_bar_set_range(m.bar, 0, 100);
  lv_bar_set_value(m.bar, 0, LV_ANIM_OFF);

  m.subLabel = lv_label_create(card);
  lv_label_set_text(m.subLabel, "");
  lv_obj_set_style_text_color(m.subLabel, lv_color_hex(0x999999), 0);
  lv_obj_set_style_text_font(m.subLabel, largeDisplay ? &lv_font_montserrat_16 : &lv_font_montserrat_14, 0);
  lv_obj_set_style_pad_top(m.subLabel, largeDisplay ? 6 : 2, 0);

  return m;
}

void updateMeter(Meter &m, int percent, const String &subText) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", percent);
  lv_label_set_text(m.valueLabel, buf);
  lv_bar_set_value(m.bar, percent, LV_ANIM_ON);
  lv_obj_set_style_bg_color(m.bar, severityColor(percent), LV_PART_INDICATOR);
  lv_label_set_text(m.subLabel, subText.c_str());
}

void onGearClicked(lv_event_t *e) {
  if (onSettingsClicked) onSettingsClicked();
}

void onRootClicked(lv_event_t *e) {
  if (onBackgroundClicked) onBackgroundClicked();
}

// Ripple grows from RIPPLE_START_SIZE to RIPPLE_END_SIZE (px) while fading
// out, like a Material Design tap ripple - a size-only or opacity-only
// animation read as too static to notice; growth is what makes it read as
// "you tapped THERE" rather than just a flash.
constexpr int RIPPLE_START_SIZE = 10;
constexpr int RIPPLE_END_SIZE = 70;

struct RippleState {
  lv_coord_t centerX;
  lv_coord_t centerY;
};

void onRippleAnimReady(lv_anim_t *a) {
  lv_obj_t *obj = (lv_obj_t *)a->var;
  delete (RippleState *)lv_obj_get_user_data(obj);
  lv_obj_del(obj);
}

// v runs 0..255 as a single progress value driving both size growth and
// opacity fade together, rather than two separate lv_anim_t's on the same
// object - avoids any ordering risk between an opacity anim's ready_cb
// deleting the object out from under a still-running size anim's exec_cb.
void rippleProgressCb(void *var, int32_t v) {
  lv_obj_t *obj = (lv_obj_t *)var;
  RippleState *state = (RippleState *)lv_obj_get_user_data(obj);
  int size = RIPPLE_START_SIZE + (RIPPLE_END_SIZE - RIPPLE_START_SIZE) * v / 255;
  lv_obj_set_size(obj, size, size);
  lv_obj_set_pos(obj, state->centerX - size / 2, state->centerY - size / 2);
  lv_opa_t opa = (lv_opa_t)(255 - v);
  lv_obj_set_style_bg_opa(obj, opa, 0);
  lv_obj_set_style_border_opa(obj, opa, 0);
}

// Draws a growing, fading ring centered on the touch point, so a tap is
// visibly acknowledged even when it doesn't change anything on screen
// (e.g. a refresh that finds nothing new) - without this the touchscreen
// gave no feedback at all for "did that register?", inviting repeat taps.
void onRootPressed(lv_event_t *e) {
  lv_obj_t *root = (lv_obj_t *)lv_event_get_target(e);
  lv_indev_t *indev = lv_indev_get_act();
  if (!indev) return;
  lv_point_t p;
  lv_indev_get_point(indev, &p);

  lv_area_t rootArea;
  lv_obj_get_coords(root, &rootArea);

  RippleState *state = new RippleState{(lv_coord_t)(p.x - rootArea.x1), (lv_coord_t)(p.y - rootArea.y1)};

  lv_obj_t *ripple = lv_obj_create(root);
  lv_obj_remove_style_all(ripple);
  lv_obj_add_flag(ripple, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_clear_flag(ripple, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(ripple, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_user_data(ripple, state);
  lv_obj_set_style_radius(ripple, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(ripple, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(ripple, LV_OPA_40, 0);
  lv_obj_set_style_border_width(ripple, 2, 0);
  lv_obj_set_style_border_color(ripple, lv_color_hex(0xE0795A), 0);  // matches the header mascot's orange
  lv_obj_set_style_border_opa(ripple, LV_OPA_COVER, 0);
  lv_obj_set_size(ripple, RIPPLE_START_SIZE, RIPPLE_START_SIZE);
  lv_obj_set_pos(ripple, state->centerX - RIPPLE_START_SIZE / 2, state->centerY - RIPPLE_START_SIZE / 2);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, ripple);
  lv_anim_set_values(&a, 0, 255);
  lv_anim_set_time(&a, 400);
  lv_anim_set_exec_cb(&a, rippleProgressCb);
  lv_anim_set_ready_cb(&a, onRippleAnimReady);
  lv_anim_start(&a);
}

} // namespace

void build(lv_obj_t *parent) {
  largeDisplay = lv_obj_get_width(parent) >= 480;

  lv_obj_t *root = lv_obj_create(parent);
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, lv_pct(100), lv_pct(100));
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(root, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(root, largeDisplay ? 16 : 8, 0);
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(root, largeDisplay ? 10 : 6, 0);
  // Tap-to-refresh: none of root's children (title, meter cards) are
  // themselves clickable except the gear button, so LVGL's hit-testing
  // falls through to root - the nearest clickable ancestor - for a tap
  // anywhere else on the dashboard. The gear keeps opening settings only.
  lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(root, onRootClicked, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(root, onRootPressed, LV_EVENT_PRESSED, nullptr);

  lv_obj_t *headerRow = lv_obj_create(root);
  lv_obj_remove_style_all(headerRow);
  lv_obj_set_size(headerRow, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_clear_flag(headerRow, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(headerRow, LV_OBJ_FLAG_CLICKABLE);  // see styleCard() comment; gearBtn (a real lv_btn) still catches its own taps
  lv_obj_set_flex_flow(headerRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(headerRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *title = lv_label_create(headerRow);
  lv_label_set_text(title, "Claude Usage");
  lv_obj_set_style_text_font(title, largeDisplay ? &lv_font_montserrat_48 : &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, lv_color_white(), 0);

  // A little mascot badge, drawn from plain LVGL shapes rather than a
  // bundled bitmap - no image asset/converter pipeline exists in this
  // project yet, and a blocky body + two eyes is simple enough to just
  // build directly. Scaled ~1.7x on the big display to stay proportional
  // next to the larger title text next to it.
  int mascotW = largeDisplay ? 48 : 28;
  int mascotH = largeDisplay ? 40 : 24;
  mascot = lv_obj_create(headerRow);
  lv_obj_remove_style_all(mascot);
  lv_obj_clear_flag(mascot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(mascot, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(mascot, mascotW, mascotH);
  lv_obj_set_style_radius(mascot, largeDisplay ? 8 : 5, 0);
  lv_obj_set_style_bg_color(mascot, lv_color_hex(0xE0795A), 0);
  lv_obj_set_style_bg_opa(mascot, LV_OPA_COVER, 0);
  // Rotate around its own center (default pivot is the top-left corner)
  // - see wiggleMascot() below, which spins this a few degrees each
  // update.
  lv_obj_set_style_transform_pivot_x(mascot, mascotW / 2, 0);
  lv_obj_set_style_transform_pivot_y(mascot, mascotH / 2, 0);

  int eyeW = largeDisplay ? 7 : 4;
  int eyeH = largeDisplay ? 13 : 8;

  lv_obj_t *eyeL = lv_obj_create(mascot);
  lv_obj_remove_style_all(eyeL);
  lv_obj_clear_flag(eyeL, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(eyeL, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(eyeL, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(eyeL, LV_OPA_COVER, 0);
  lv_obj_set_size(eyeL, eyeW, eyeH);
  lv_obj_set_pos(eyeL, largeDisplay ? 10 : 6, largeDisplay ? 12 : 7);

  lv_obj_t *eyeR = lv_obj_create(mascot);
  lv_obj_remove_style_all(eyeR);
  lv_obj_clear_flag(eyeR, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(eyeR, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(eyeR, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(eyeR, LV_OPA_COVER, 0);
  lv_obj_set_size(eyeR, eyeW, eyeH);
  lv_obj_set_pos(eyeR, largeDisplay ? 31 : 18, largeDisplay ? 12 : 7);

  int gearSize = largeDisplay ? 46 : 34;
  lv_obj_t *gearBtn = lv_btn_create(headerRow);
  lv_obj_set_size(gearBtn, gearSize, gearSize);
  lv_obj_set_style_radius(gearBtn, gearSize / 2, 0);
  lv_obj_set_style_bg_color(gearBtn, lv_color_hex(0x2a2a2a), 0);
  lv_obj_set_style_shadow_width(gearBtn, 0, 0);
  lv_obj_add_event_cb(gearBtn, onGearClicked, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *gearLabel = lv_label_create(gearBtn);
  lv_label_set_text(gearLabel, LV_SYMBOL_SETTINGS);
  if (largeDisplay) lv_obj_set_style_text_font(gearLabel, &lv_font_montserrat_24, 0);
  lv_obj_center(gearLabel);

  sessionMeter = makeMeter(root, "Session (5hr)");
  weeklyMeter = makeMeter(root, "Weekly");
  creditsMeter = makeMeter(root, "Credits");

  lv_obj_t *statusRow = lv_obj_create(root);
  lv_obj_remove_style_all(statusRow);
  lv_obj_set_size(statusRow, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_clear_flag(statusRow, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(statusRow, LV_OBJ_FLAG_CLICKABLE);  // let a tap here still bubble to root's background-tap-to-refresh
  lv_obj_set_flex_flow(statusRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(statusRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  statusLabel = lv_label_create(statusRow);
  lv_label_set_text(statusLabel, "Connecting...");
  lv_obj_set_style_text_color(statusLabel, lv_color_hex(0x777777), 0);
  lv_obj_set_style_text_font(statusLabel, largeDisplay ? &lv_font_montserrat_16 : &lv_font_montserrat_14, 0);
  // On wide displays, match statusUptimeLabel's fixed width (190, below)
  // so the row is symmetric left/right - SPACE_BETWEEN only equalizes
  // the *gaps* between children, not the middle child's position
  // relative to true center, so an unequal left/right footprint (a
  // short "Updated" against a wide fixed uptime box) visibly pushed the
  // countdown off-center. Left-aligned text keeps "Updated" reading
  // flush-left within the wider box rather than drifting to its middle.
  if (largeDisplay) {
    lv_obj_set_width(statusLabel, 190);
    lv_obj_set_style_text_align(statusLabel, LV_TEXT_ALIGN_LEFT, 0);
  }

  // Fixed width + alignment (rather than auto-sizing to content) on both
  // ticking labels below - SPACE_BETWEEN recomputes every child's gap
  // from its *current* rendered width, so as uptime's text grows from
  // "12s" to "5m 9s" to "1h 5m" (and the countdown's digits change),
  // auto-sized labels made the whole row visibly reflow and the middle
  // countdown jump around every second. A fixed box means the row's
  // layout stays constant regardless of what the digits are.
  statusCountdownLabel = lv_label_create(statusRow);
  lv_label_set_text(statusCountdownLabel, "");
  lv_obj_set_style_text_color(statusCountdownLabel, lv_color_hex(0x777777), 0);
  lv_obj_set_style_text_font(statusCountdownLabel, largeDisplay ? &lv_font_montserrat_16 : &lv_font_montserrat_14, 0);
  lv_obj_set_width(statusCountdownLabel, largeDisplay ? 170 : 130);
  lv_obj_set_style_text_align(statusCountdownLabel, largeDisplay ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_RIGHT, 0);

  // Third slot only on wide displays - SPACE_BETWEEN naturally centers
  // the countdown between this and statusLabel when it exists, and
  // collapses back to a clean 2-way left/right split when it doesn't,
  // so no separate positioning logic is needed either way.
  if (largeDisplay) {
    statusUptimeLabel = lv_label_create(statusRow);
    lv_label_set_text(statusUptimeLabel, "");
    lv_obj_set_style_text_color(statusUptimeLabel, lv_color_hex(0x777777), 0);
    lv_obj_set_style_text_font(statusUptimeLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_width(statusUptimeLabel, 190);
    lv_obj_set_style_text_align(statusUptimeLabel, LV_TEXT_ALIGN_RIGHT, 0);
  }

  // Covers the (otherwise indistinguishable-from-"just no data yet") empty
  // meters until the first real fetch succeeds - a locked/not-configured
  // device and a briefly-loading one looked identical without this, which
  // cost real debugging time working out "is it broken or just not synced
  // yet?" the hard way.
  lockOverlay = lv_obj_create(root);
  lv_obj_add_flag(lockOverlay, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_size(lockOverlay, lv_pct(100), lv_pct(100));
  lv_obj_set_pos(lockOverlay, 0, 0);
  lv_obj_set_style_bg_color(lockOverlay, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(lockOverlay, LV_OPA_90, 0);
  lv_obj_set_style_border_width(lockOverlay, 0, 0);
  lv_obj_set_style_radius(lockOverlay, 0, 0);
  lv_obj_clear_flag(lockOverlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(lockOverlay, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(lockOverlay, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(lockOverlay, 6, 0);

  lv_obj_t *lockTitle = lv_label_create(lockOverlay);
  lv_label_set_text(lockTitle, "LOCKED");
  lv_obj_set_style_text_font(lockTitle, largeDisplay ? &lv_font_montserrat_48 : &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(lockTitle, lv_palette_main(LV_PALETTE_ORANGE), 0);

  lockDetailLabel = lv_label_create(lockOverlay);
  lv_label_set_text(lockDetailLabel, "Connecting...");
  lv_obj_set_style_text_color(lockDetailLabel, lv_color_white(), 0);
  lv_obj_set_style_text_font(lockDetailLabel, largeDisplay ? &lv_font_montserrat_24 : &lv_font_montserrat_14, 0);
  lv_label_set_long_mode(lockDetailLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lockDetailLabel, lv_pct(85));
  lv_obj_set_style_text_align(lockDetailLabel, LV_TEXT_ALIGN_CENTER, 0);
}

void update(const Snapshot &snap) {
  if (!snap.valid) {
    String text = snap.error.length() ? snap.error : "Waiting for first update...";
    setStatusLine(text);
    if (!everValid && lockDetailLabel) lv_label_set_text(lockDetailLabel, text.c_str());
    return;
  }

  if (!everValid) {
    everValid = true;
    if (lockOverlay) {
      lv_obj_del(lockOverlay);
      lockOverlay = nullptr;
      lockDetailLabel = nullptr;
    }
  }

  updateMeter(sessionMeter, snap.sessionPercent, snap.sessionResetsIn);
  updateMeter(weeklyMeter, snap.weeklyPercent, snap.weeklyResetsIn);

  if (snap.creditsEnabled && snap.creditsLimitMinor > 0) {
    int percent = (int)((snap.creditsUsedMinor * 100) / snap.creditsLimitMinor);
    char sub[48];
    snprintf(sub, sizeof(sub), "$%.2f of $%.2f", snap.creditsUsedMinor / 100.0, snap.creditsLimitMinor / 100.0);
    updateMeter(creditsMeter, percent, sub);
  } else {
    updateMeter(creditsMeter, 0, "Not enabled");
  }
}

// A little celebratory wiggle on the header mascot each time an update
// lands - three chained short animations (rather than one anim with
// playback, which just reverses back to its own start value) so the
// motion actually goes center -> counter-clockwise -> clockwise ->
// center instead of a single there-and-back swing.
void mascotWiggleLeg3(lv_anim_t *) {
  static lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, mascot);
  lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) { lv_obj_set_style_transform_angle((lv_obj_t *)obj, v, 0); });
  lv_anim_set_values(&a, 120, 0);
  lv_anim_set_time(&a, 150);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_start(&a);
}

void mascotWiggleLeg2(lv_anim_t *) {
  static lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, mascot);
  lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) { lv_obj_set_style_transform_angle((lv_obj_t *)obj, v, 0); });
  lv_anim_set_values(&a, -120, 120);
  lv_anim_set_time(&a, 250);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_ready_cb(&a, mascotWiggleLeg3);
  lv_anim_start(&a);
}

void wiggleMascot() {
  if (!mascot) return;
  static lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, mascot);
  lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) { lv_obj_set_style_transform_angle((lv_obj_t *)obj, v, 0); });
  lv_anim_set_values(&a, 0, -120);  // angle units are 0.1deg, so -120/120 = 12 degrees
  lv_anim_set_time(&a, 150);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_ready_cb(&a, mascotWiggleLeg2);
  lv_anim_start(&a);
}

void setStatusLine(const String &text) {
  if (statusLabel) lv_label_set_text(statusLabel, text.c_str());
  if (statusCountdownLabel) lv_label_set_text(statusCountdownLabel, "");
  if (statusUptimeLabel) lv_label_set_text(statusUptimeLabel, "");
  // The LOCKED overlay sits fully opaque on top of statusLabel while
  // !everValid, so callers like pollUsage()'s "not configured"/"locked"
  // messages would otherwise be computed correctly but never actually seen -
  // mirror them onto the overlay's own label too.
  if (lockDetailLabel) lv_label_set_text(lockDetailLabel, text.c_str());
}

void setUpdatedLabel() {
  if (statusLabel) lv_label_set_text(statusLabel, "Updated");
  // lockOverlay is always hidden by the time this is called (only reached
  // after a successful fetch, which requires unlocked+configured), so
  // there's no lockDetailLabel echo needed here the way setStatusLine() does.
  wiggleMascot();
}

void setLiveStatus(const String &countdownText, const String &uptimeText) {
  if (statusCountdownLabel) lv_label_set_text(statusCountdownLabel, countdownText.c_str());
  if (statusUptimeLabel) lv_label_set_text(statusUptimeLabel, uptimeText.c_str());
}

void setOnSettingsClicked(std::function<void()> cb) {
  onSettingsClicked = cb;
}

void setOnBackgroundClicked(std::function<void()> cb) {
  onBackgroundClicked = cb;
}

} // namespace UsageDashboard
