#!/usr/bin/env python3
"""Writes build/<platform>/latest.json, the file each app reads to check for updates.

    python3 tools/update_manifest.py <platform> <key>=<file> [<key>=<file> ...]

Example:
    python3 tools/update_manifest.py windows-11 win-x64=build/windows-11/Pulse-win-x64.zip

The version comes from VERSION at the repo root. Paths are stored relative to
the repo root, so the apps fetch them from
https://raw.githubusercontent.com/njarrar/SystemPulse/main/<path>.
"""
import hashlib, json, os, sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
platform, pairs = sys.argv[1], sys.argv[2:]
if not pairs:
    sys.exit(__doc__)
version = open(os.path.join(root, "VERSION")).read().strip()
files = {}
for pair in pairs:
    key, path = pair.split("=", 1)
    full = os.path.abspath(path)
    with open(full, "rb") as f:
        digest = hashlib.sha256(f.read()).hexdigest()
    files[key] = {"path": os.path.relpath(full, root).replace(os.sep, "/"), "sha256": digest, "size": os.path.getsize(full)}
out = os.path.join(root, "build", platform, "latest.json")
with open(out, "w") as f:
    json.dump({"version": version, "files": files}, f, indent=2)
    f.write("\n")
print(f"{out}: {version}, {', '.join(files)}")
