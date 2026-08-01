"""Polls the same undocumented endpoint claude.ai's own Settings > Usage
panel calls, and re-serves a small, simplified JSON summary on the local
network for firmware-relay to read. This is the only place your session
cookie lives - it's read from .env (gitignored) and never sent to the CYD.

Run: python claude_usage_relay.py
"""

import json
import os
import threading
import time
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import requests

HERE = os.path.dirname(os.path.abspath(__file__))


def load_env(path):
    """Minimal .env reader - avoids adding python-dotenv as a dependency
    for three key=value lines."""
    values = {}
    if not os.path.exists(path):
        return values
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, _, value = line.partition("=")
            values[key.strip()] = value.strip().strip('"').strip("'")
    return values


env = {**load_env(os.path.join(HERE, ".env")), **os.environ}

ORG_ID = env.get("CLAUDE_ORG_ID", "")
COOKIE = env.get("CLAUDE_SESSION_COOKIE", "")
PORT = int(env.get("RELAY_PORT", "8787"))
BIND_HOST = env.get("RELAY_BIND_HOST", "0.0.0.0")
POLL_INTERVAL_SECONDS = int(env.get("RELAY_POLL_INTERVAL_SECONDS", "300"))

if not ORG_ID or not COOKIE:
    raise SystemExit(
        "Missing CLAUDE_ORG_ID / CLAUDE_SESSION_COOKIE - copy .env.example to "
        ".env and fill them in (see README.md for how to find these)."
    )

USAGE_URL = f"https://claude.ai/api/organizations/{ORG_ID}/usage"

_lock = threading.Lock()
_cache = {"ok": False, "error": "Haven't polled claude.ai yet"}


def format_duration(resets_at_iso):
    try:
        target = datetime.fromisoformat(resets_at_iso)
    except ValueError:
        return "?"
    now = datetime.now(timezone.utc)
    diff = (target - now).total_seconds()
    if diff <= 0:
        return "now"
    hours, remainder = divmod(int(diff), 3600)
    minutes = remainder // 60
    return f"{hours}h {minutes}m" if hours else f"{minutes}m"


def fetch_once():
    resp = requests.get(
        USAGE_URL,
        headers={
            "Cookie": COOKIE,
            "User-Agent": "Claude_usage-relay/1.0 (+https://github.com/Grey-Lancaster/Claude_usage)",
        },
        timeout=10,
    )
    if resp.status_code in (401, 403):
        raise RuntimeError("Cookie expired or invalid - update .env")
    resp.raise_for_status()
    data = resp.json()

    result = {
        "ok": True,
        "session_percent": 0,
        "session_resets_in": "?",
        "weekly_percent": 0,
        "weekly_resets_in": "?",
        "credits_enabled": False,
        "credits_used_minor": 0,
        "credits_limit_minor": 0,
        "currency": "USD",
        "fetched_at": datetime.now(timezone.utc).isoformat(),
    }

    for limit in data.get("limits", []):
        kind = limit.get("kind", "")
        if kind == "session":
            result["session_percent"] = limit.get("percent", 0)
            result["session_resets_in"] = format_duration(limit.get("resets_at", ""))
        elif kind.startswith("weekly"):
            result["weekly_percent"] = limit.get("percent", 0)
            result["weekly_resets_in"] = format_duration(limit.get("resets_at", ""))

    spend = data.get("spend", {})
    result["credits_enabled"] = spend.get("enabled", False)
    result["credits_used_minor"] = spend.get("used", {}).get("amount_minor", 0)
    result["credits_limit_minor"] = spend.get("limit", {}).get("amount_minor", 0)
    result["currency"] = spend.get("limit", {}).get("currency", "USD")

    return result


def poll_loop():
    while True:
        try:
            result = fetch_once()
            with _lock:
                _cache.clear()
                _cache.update(result)
            print(f"[relay] updated: session={result['session_percent']}% weekly={result['weekly_percent']}%")
        except Exception as exc:  # noqa: BLE001 - one bad poll shouldn't kill the loop
            with _lock:
                _cache.clear()
                _cache.update({"ok": False, "error": str(exc)})
            print(f"[relay] fetch failed: {exc}")
        time.sleep(POLL_INTERVAL_SECONDS)


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass  # keep console output to the poll loop's own prints

    def do_GET(self):
        if self.path != "/usage":
            self.send_response(404)
            self.end_headers()
            return

        with _lock:
            body = json.dumps(_cache).encode("utf-8")
            ok = _cache.get("ok", False)

        self.send_response(200 if ok else 503)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main():
    threading.Thread(target=poll_loop, daemon=True).start()
    server = ThreadingHTTPServer((BIND_HOST, PORT), Handler)
    print(f"[relay] serving http://{BIND_HOST}:{PORT}/usage (polling every {POLL_INTERVAL_SECONDS}s)")
    server.serve_forever()


if __name__ == "__main__":
    main()
