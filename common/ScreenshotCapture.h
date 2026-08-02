// Streams a synchronous, top-down 24bpp BMP of whatever the current LVGL
// screen looks like, straight out of LVGL's own render pass - no
// persistent full-frame buffer needed, which matters on the classic
// ESP32's ~300KB SRAM (a 320x240 shadow framebuffer alone would be
// ~150KB, uncomfortably close to what WiFi/TLS needs during a usage
// fetch). Used by ClaudeSetupServer's "Screenshot" link/button so the
// on-device dashboard can be grabbed straight from a browser for
// READMEs/social posts, instead of photographing the physical screen.
//
// Relies on one assumption that only holds because stream() forces a
// full-screen invalidate before reading anything: every disp_flush()
// call while a capture is active covers the display's *full width*, so
// BMP rows can be written out in the same order LVGL flushes them, no
// reassembly buffer required. feedArea() checks this and silently skips
// any flush that doesn't match - the result is a short/truncated
// download (an obvious, visible failure) rather than corrupted pixels.
#pragma once

#include <Arduino.h>
#include <lvgl.h>

class WebServer;

namespace ScreenshotCapture {

// Call from a WebServer GET handler. Blocks until the whole frame has
// been sent (well under a second for either board this project
// supports). width/height must match the registered LVGL display
// driver's resolution exactly.
void stream(WebServer &server, uint16_t width, uint16_t height);

// Call from every board's disp_flush(), unconditionally, right after the
// real hardware push - a cheap no-op unless a capture is in progress.
void feedArea(const lv_area_t *area, lv_color_t *colorBuf, uint32_t w, uint32_t h);

} // namespace ScreenshotCapture
