# relay-server

Small local script that holds your `claude.ai` session cookie and
re-serves a simplified usage summary on your LAN for
[`../firmware-relay`](../firmware-relay) to poll. This is the only place
the cookie lives in this variant - it's never sent to the ESP32.

## Setup

```bash
cd relay-server
python -m venv .venv
.venv\Scripts\activate        # Windows; use `source .venv/bin/activate` on macOS/Linux
pip install -r requirements.txt
copy .env.example .env        # macOS/Linux: cp .env.example .env
```

Fill in `.env`:

1. In a browser logged into `claude.ai`, open DevTools > Network, go to
   Settings > Usage, and reload/click the refresh icon next to "Last
   updated". Find the request to:
   ```
   GET https://claude.ai/api/organizations/<ORG_ID>/usage
   ```
2. `CLAUDE_ORG_ID` = the UUID from that URL.
3. `CLAUDE_SESSION_COOKIE` = the full `cookie` request header value from
   that same request.

## Run

```bash
python claude_usage_relay.py
```

Leave it running. It polls claude.ai every 5 minutes
(`RELAY_POLL_INTERVAL_SECONDS`) and serves the latest result at
`http://<this-machine's-LAN-IP>:8787/usage`. Point the CYD at that
`host:port` in its Settings > Change Relay Address screen.

## Notes

- The cookie will eventually expire or rotate (e.g. if you log out
  elsewhere). When that happens the console prints `Cookie expired or
  invalid - update .env` and the CYD's dashboard shows the same. Repeat
  the DevTools steps above and update `.env`, then restart the script.
- `/usage` has no auth - anything on your LAN can read your usage
  percentages (not the cookie itself, which stays server-side). Don't
  port-forward this to the internet.
- To run this unattended (e.g. start on login), wrap it in your OS's
  usual mechanism (Task Scheduler, a `.service` file, `pm2`, etc.) - not
  included here since that's environment-specific.
