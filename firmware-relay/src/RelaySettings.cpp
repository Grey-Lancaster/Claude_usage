#include "RelaySettings.h"

#include <Arduino.h>
#include "RelayConfig.h"

namespace RelaySettings {
namespace {

lv_obj_t *panel = nullptr;
lv_obj_t *textarea = nullptr;
lv_obj_t *keyboard = nullptr;
std::function<void(const String &)> savedCb;

void close() {
  if (panel) {
    lv_obj_del(panel);
    panel = nullptr;
    textarea = nullptr;
    keyboard = nullptr;
  }
}

void doSave() {
  String value = lv_textarea_get_text(textarea);
  value.trim();
  if (value.length() == 0) return;
  RelayConfig::setHostPort(value);
  auto cb = savedCb;
  close();
  if (cb) cb(value);
}

void onSaveClicked(lv_event_t *e) { doSave(); }
void onCancelClicked(lv_event_t *e) { close(); }

// Keyboard's checkmark (LV_EVENT_READY) acts as a second Save trigger, so
// users aren't forced to hunt for the Save button after typing.
void onKeyboardReady(lv_event_t *e) { doSave(); }

} // namespace

void open(lv_obj_t *parent, std::function<void(const String &)> onSaved) {
  if (panel) return;
  savedCb = onSaved;

  panel = lv_obj_create(parent);
  lv_obj_add_flag(panel, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_size(panel, lv_pct(100), lv_pct(100));
  lv_obj_set_pos(panel, 0, 0);
  lv_obj_set_style_bg_color(panel, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(panel, 10, 0);
  lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(panel, 8, 0);

  lv_obj_t *title = lv_label_create(panel);
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_label_set_text(title, "Relay address (host:port)");

  textarea = lv_textarea_create(panel);
  lv_obj_set_size(textarea, lv_pct(100), 40);
  lv_textarea_set_one_line(textarea, true);
  lv_textarea_set_placeholder_text(textarea, "192.168.1.50:8787");
  lv_textarea_set_text(textarea, RelayConfig::hostPort().c_str());

  lv_obj_t *btnRow = lv_obj_create(panel);
  lv_obj_remove_style_all(btnRow);
  lv_obj_set_size(btnRow, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *cancelBtn = lv_btn_create(btnRow);
  lv_obj_add_event_cb(cancelBtn, onCancelClicked, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *cancelLabel = lv_label_create(cancelBtn);
  lv_label_set_text(cancelLabel, "Cancel");

  lv_obj_t *saveBtn = lv_btn_create(btnRow);
  lv_obj_add_event_cb(saveBtn, onSaveClicked, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *saveLabel = lv_label_create(saveBtn);
  lv_label_set_text(saveLabel, "Save");

  keyboard = lv_keyboard_create(panel);
  lv_keyboard_set_textarea(keyboard, textarea);
  lv_obj_add_event_cb(keyboard, onKeyboardReady, LV_EVENT_READY, nullptr);

  lv_obj_add_state(textarea, LV_STATE_FOCUSED);
}

} // namespace RelaySettings
