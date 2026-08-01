// Plain HTTP GET to the local relay server's /usage endpoint. No TLS, no
// cookie on this device at all - see relay-server/ for what actually
// talks to claude.ai.
#pragma once

#include <Arduino.h>
#include "UsageDashboard.h"

namespace RelayClient {

// `hostPort` like "192.168.1.50:8787". Returns true and fills `out` on
// success; false with out.error set otherwise (including the relay's own
// "haven't fetched successfully yet" response).
bool fetch(const String &hostPort, UsageDashboard::Snapshot &out);

} // namespace RelayClient
