# cookie-refresh

Whenever `firmware-direct`'s dashboard shows "Cookie expired -
reconfigure", run this instead of retyping everything into the setup web
page by hand - it still needs the DevTools dig for the two values, but
handles sending them to the device for you.

## Why this can't be fully automated

An earlier version of this script drove Chrome via Playwright to grab both
values automatically, with no DevTools step at all. That doesn't work:
claude.ai sits behind Cloudflare Turnstile, which detects an
automation-controlled Chrome session (the CDP debugger attachment itself
is part of what gets fingerprinted, not just launch flags) and
re-challenges it in an endless loop - no amount of retrying gets past it.
A completely normal, human-driven browser is invisible to that check,
which is why the DevTools step below still has to be done by hand.

## Use (Windows)

Double-click **`refresh_cookie.bat`**. It installs dependencies the first
time it's run (needs [Python](https://python.org/downloads/) - check "Add
python.exe to PATH" in the installer) and just runs the script every time
after.

## Use (manual / other platforms)

```bash
cd cookie-refresh
pip install -r requirements.txt
python refresh_cookie.py
```

## What it asks for

1. **Org ID** and **Cookie** - grab both from a browser logged into
   claude.ai. See [`../docs/cookie-guide.md`](../docs/cookie-guide.md) for
   the illustrated, click-by-click version; short version:
   - Open DevTools (F12) > Network tab.
   - Reload `https://claude.ai/settings/usage`
   - Find the request to `/api/organizations/<ORG_ID>/usage` - the org ID
     is right there in the URL; click the request > Headers > Request
     Headers > copy the whole `cookie` value.
2. **Device passphrase** (hidden input, never written to disk by this
   script or the device - same "not stored anywhere" principle as the
   device itself).

It then POSTs all of that straight to `http://claudeusage.local/provision`
- same effect as filling out the setup page by hand, minus the hand part
for that last step.

Options:
- `--device <host-or-ip>` - if mDNS resolution is being flaky (a known
  Windows quirk - see the root README), pass the device's IP directly.
