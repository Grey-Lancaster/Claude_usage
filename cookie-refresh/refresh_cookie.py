"""Sends your Claude.ai org ID + session cookie to the Claude_usage device.

Used to drive Chrome via Playwright to grab these automatically, but
claude.ai sits behind Cloudflare Turnstile, which detects an
automation-controlled Chrome session (the CDP debugger attachment itself
is part of what's fingerprinted, not just launch flags) and re-challenges
it indefinitely - no amount of retrying got past it. A completely normal,
human-driven browser is invisible to that check, so this instead just asks
you to paste the two values from one and does the "send it to the device"
part for you, skipping the setup web page.

Usage:
    pip install -r requirements.txt
    python refresh_cookie.py
"""

import argparse
import getpass
import sys

import requests

DEFAULT_DEVICE = "claudeusage.local"

HOWTO = """\
Grab these from a browser logged into claude.ai:
  1. Open DevTools (F12) > Network tab.
  2. Reload https://claude.ai/settings/usage
  3. Find the request to /api/organizations/<ORG_ID>/usage
       - the ORG_ID is right there in that URL
       - click the request > Headers > Request Headers > copy the whole
         'cookie' value
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("Usage:")[0], formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--device", default=DEFAULT_DEVICE, help=f"Device hostname or IP (default: {DEFAULT_DEVICE})")
    args = parser.parse_args()

    print(HOWTO)
    org_id = input("Org ID: ").strip()
    cookie = input("Cookie: ").strip()

    if not org_id or not cookie:
        print("Both Org ID and Cookie are required.")
        sys.exit(1)

    passphrase = getpass.getpass(
        "Device passphrase (typed fresh, never saved anywhere by this script or the device): "
    )

    resp = requests.post(
        f"http://{args.device}/provision",
        data={"orgid": org_id, "cookie": cookie, "pass": passphrase, "pass2": passphrase},
        timeout=15,
    )

    if resp.status_code == 200 and "Setup failed" not in resp.text:
        print("Sent to device - check the screen, it should be showing live data now.")
    else:
        print(f"Device didn't confirm success (HTTP {resp.status_code}). Response snippet:")
        print(resp.text[:300])
        sys.exit(1)


if __name__ == "__main__":
    main()
