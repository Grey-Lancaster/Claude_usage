#include "UsageDashboard.h"

namespace UsageDashboard {
namespace {

lv_obj_t *statusLabel = nullptr;
std::function<void()> onSettingsClicked;

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

void styleCard(lv_obj_t *card) {
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x1c1c1c), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, 10, 0);
  lv_obj_set_style_pad_all(card, 8, 0);
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
  lv_obj_set_flex_flow(headerRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(headerRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  m.nameLabel = lv_label_create(headerRow);
  lv_label_set_text(m.nameLabel, name);
  lv_obj_set_style_text_color(m.nameLabel, lv_color_white(), 0);

  m.valueLabel = lv_label_create(headerRow);
  lv_label_set_text(m.valueLabel, "--");
  lv_obj_set_style_text_color(m.valueLabel, lv_color_white(), 0);

  m.bar = lv_bar_create(card);
  lv_obj_set_size(m.bar, lv_pct(100), 10);
  lv_obj_set_style_pad_top(m.bar, 6, 0);
  lv_obj_set_style_bg_color(m.bar, lv_color_hex(0x3a3a3a), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(m.bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_bar_set_range(m.bar, 0, 100);
  lv_bar_set_value(m.bar, 0, LV_ANIM_OFF);

  m.subLabel = lv_label_create(card);
  lv_label_set_text(m.subLabel, "");
  lv_obj_set_style_text_color(m.subLabel, lv_color_hex(0x999999), 0);
  lv_obj_set_style_text_font(m.subLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_pad_top(m.subLabel, 2, 0);

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

} // namespace

void build(lv_obj_t *parent) {
  lv_obj_t *root = lv_obj_create(parent);
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, lv_pct(100), lv_pct(100));
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(root, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(root, 8, 0);
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(root, 6, 0);

  lv_obj_t *headerRow = lv_obj_create(root);
  lv_obj_remove_style_all(headerRow);
  lv_obj_set_size(headerRow, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_clear_flag(headerRow, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(headerRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(headerRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *title = lv_label_create(headerRow);
  lv_label_set_text(title, "Claude Usage");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, lv_color_white(), 0);

  lv_obj_t *gearBtn = lv_btn_create(headerRow);
  lv_obj_set_size(gearBtn, 34, 34);
  lv_obj_set_style_radius(gearBtn, 17, 0);
  lv_obj_set_style_bg_color(gearBtn, lv_color_hex(0x2a2a2a), 0);
  lv_obj_set_style_shadow_width(gearBtn, 0, 0);
  lv_obj_add_event_cb(gearBtn, onGearClicked, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *gearLabel = lv_label_create(gearBtn);
  lv_label_set_text(gearLabel, LV_SYMBOL_SETTINGS);
  lv_obj_center(gearLabel);

  sessionMeter = makeMeter(root, "Session (5hr)");
  weeklyMeter = makeMeter(root, "Weekly");
  creditsMeter = makeMeter(root, "Credits");

  statusLabel = lv_label_create(root);
  lv_label_set_text(statusLabel, "Connecting...");
  lv_obj_set_style_text_color(statusLabel, lv_color_hex(0x777777), 0);
  lv_obj_set_style_text_font(statusLabel, &lv_font_montserrat_14, 0);
}

void update(const Snapshot &snap) {
  if (!snap.valid) {
    setStatusLine(snap.error.length() ? snap.error : "Waiting for first update...");
    return;
  }

  updateMeter(sessionMeter, snap.sessionPercent, "Resets in " + snap.sessionResetsIn);
  updateMeter(weeklyMeter, snap.weeklyPercent, "Resets in " + snap.weeklyResetsIn);

  if (snap.creditsEnabled && snap.creditsLimitMinor > 0) {
    int percent = (int)((snap.creditsUsedMinor * 100) / snap.creditsLimitMinor);
    char sub[48];
    snprintf(sub, sizeof(sub), "$%.2f of $%.2f", snap.creditsUsedMinor / 100.0, snap.creditsLimitMinor / 100.0);
    updateMeter(creditsMeter, percent, sub);
  } else {
    updateMeter(creditsMeter, 0, "Not enabled");
  }
}

void setStatusLine(const String &text) {
  if (statusLabel) lv_label_set_text(statusLabel, text.c_str());
}

void setOnSettingsClicked(std::function<void()> cb) {
  onSettingsClicked = cb;
}

} // namespace UsageDashboard
