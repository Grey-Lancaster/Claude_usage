#pragma once

// Bump alongside docs/firmware/*/manifest.json's "version" field and
// CHANGELOG.md's newest entry - kept here too so the on-device System Info
// screen (AppLogic.cpp, opened via a short press of the physical BOOT
// button) shows what's actually running on the device, not just what the
// OTA manifest claims it should be.
#define FW_VERSION "0.9.44"
