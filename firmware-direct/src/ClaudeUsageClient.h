// Talks directly to the same undocumented endpoint claude.ai's own
// Settings > Usage panel calls. Not a public/stable API - Anthropic could
// change or remove it without notice. See ../README.md for how that was
// found and what the response looks like.
#pragma once

#include <Arduino.h>
#include "UsageDashboard.h"

namespace ClaudeUsageClient {

// Blocking HTTPS GET + JSON parse (a few hundred ms typically). Returns
// true and fills `out` on success; on failure returns false and leaves a
// human-readable reason in out.error.
bool fetch(const String &orgId, const String &cookie, UsageDashboard::Snapshot &out);

} // namespace ClaudeUsageClient
