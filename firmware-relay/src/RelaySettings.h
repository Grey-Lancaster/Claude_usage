// Full-screen "type the relay's host:port" overlay - on-screen keyboard
// entry is fine here since, unlike a session cookie, this value is short.
#pragma once

#include <lvgl.h>
#include <functional>

namespace RelaySettings {

// Builds the overlay as a child of `parent` and shows it. Deletes itself
// on Save/Cancel. `onSaved` fires with the new value after it's been
// persisted via RelayConfig.
void open(lv_obj_t *parent, std::function<void(const String &)> onSaved);

} // namespace RelaySettings
