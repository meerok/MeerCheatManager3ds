#!/usr/bin/env python3
"""
Updates McM3.unistore for a new release so Universal-Updater shows "update available".

    python3 tools/update_unistore.py <tag> [release notes file]

Run by .github/workflows/unistore.yml whenever a release is published. The store's
download script always fetches the latest release's McM3.3gx, so only the version,
date, release notes and store revision need to change.
"""
import datetime
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STORE = os.path.join(ROOT, "McM3.unistore")


def main(tag, notes_path=None):
    store = json.load(open(STORE, encoding="utf-8"))
    info = store["storeContent"][0]["info"]

    info["version"] = tag
    info["last_updated"] = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d at %H:%M (UTC)")
    if notes_path and os.path.exists(notes_path):
        notes = open(notes_path, encoding="utf-8").read().replace("\r", "").strip()
        if notes:
            info["releasenotes"] = notes[:2000]
    store["storeInfo"]["revision"] = int(store["storeInfo"].get("revision", 0)) + 1

    with open(STORE, "w", encoding="utf-8") as f:
        f.write(json.dumps(store, indent=4, ensure_ascii=False) + "\n")
    print("McM3.unistore -> %s (revision %d)" % (tag, store["storeInfo"]["revision"]))


if __name__ == "__main__":
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    main(*sys.argv[1:])
