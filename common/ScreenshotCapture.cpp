#include "ScreenshotCapture.h"

#include <WebServer.h>

namespace ScreenshotCapture {
namespace {

WebServer *g_server = nullptr;
uint16_t g_width = 0;
uint16_t g_rowBytes = 0;

// Widest board this project supports is the CrowPanel7 at 800px - a
// fixed buffer avoids a heap allocation on every row.
constexpr uint16_t MAX_ROW_BUF = 800 * 3 + 4;
uint8_t rowBuf[MAX_ROW_BUF];

void put16(uint8_t *p, uint16_t v) {
  p[0] = v & 0xFF;
  p[1] = (v >> 8) & 0xFF;
}

void put32(uint8_t *p, uint32_t v) {
  p[0] = v & 0xFF;
  p[1] = (v >> 8) & 0xFF;
  p[2] = (v >> 16) & 0xFF;
  p[3] = (v >> 24) & 0xFF;
}

} // namespace

void stream(WebServer &server, uint16_t width, uint16_t height) {
  uint16_t rowBytes = ((width * 3 + 3) / 4) * 4;  // BMP rows pad to a 4-byte boundary
  if (rowBytes > MAX_ROW_BUF) {
    server.send(500, "text/plain", "Screen too wide for the screenshot buffer");
    return;
  }
  g_rowBytes = rowBytes;
  g_width = width;

  uint32_t pixelDataSize = (uint32_t)g_rowBytes * height;
  uint32_t fileSize = 54 + pixelDataSize;

  uint8_t header[54] = {0};
  header[0] = 'B';
  header[1] = 'M';
  put32(header + 2, fileSize);
  put32(header + 10, 54);                             // pixel data offset
  put32(header + 14, 40);                              // BITMAPINFOHEADER size
  put32(header + 18, width);
  put32(header + 22, (uint32_t)(-(int32_t)height));    // negative height = top-down rows
  put16(header + 26, 1);                                // colour planes
  put16(header + 28, 24);                               // bits per pixel
  put32(header + 34, pixelDataSize);

  server.setContentLength(fileSize);
  server.send(200, "image/bmp", "");
  server.sendContent((const char *)header, sizeof(header));

  g_server = &server;
  lv_obj_invalidate(lv_scr_act());
  lv_refr_now(nullptr);  // synchronous full redraw - every dirty area flushes before this returns
  g_server = nullptr;
}

void feedArea(const lv_area_t *area, lv_color_t *colorBuf, uint32_t w, uint32_t h) {
  if (!g_server || w != g_width) return;  // not capturing, or a partial-width flush - skip rather than corrupt the stream
  (void)area;

  for (uint32_t row = 0; row < h; row++) {
    lv_color_t *src = colorBuf + row * w;
    for (uint32_t x = 0; x < w; x++) {
      uint16_t px = src[x].full;
      uint8_t r5 = (px >> 11) & 0x1F;
      uint8_t g6 = (px >> 5) & 0x3F;
      uint8_t b5 = px & 0x1F;
      // BMP pixel order is B,G,R - bit-depth-expand each channel to 8 bits.
      rowBuf[x * 3 + 0] = (b5 * 255 + 15) / 31;
      rowBuf[x * 3 + 1] = (g6 * 255 + 31) / 63;
      rowBuf[x * 3 + 2] = (r5 * 255 + 15) / 31;
    }
    for (uint32_t p = w * 3; p < g_rowBytes; p++) rowBuf[p] = 0;  // row padding
    g_server->sendContent((const char *)rowBuf, g_rowBytes);
  }
}

} // namespace ScreenshotCapture
