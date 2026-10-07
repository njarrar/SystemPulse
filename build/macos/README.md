# Pulse for macOS

`Pulse-macos-universal.zip` holds `Pulse.app` for Apple silicon and Intel, macOS 13 or later. GitHub Actions builds it from `native/macos` on every change and places it here.

1. Unzip and move `Pulse.app` to Applications.
2. Open it. If the build is notarized by Apple, it opens with no warning.

If macOS says Pulse "couldn't be verified" or "can't be opened", the build is not notarized yet. Open it once by hand; macOS remembers the choice:

- **macOS 15 or later:** double-click Pulse and close the warning. Open **System Settings > Privacy & Security**, scroll down and click **Open Anyway** next to Pulse, then enter your password.
- **macOS 13 and 14:** right-click Pulse in Finder and pick **Open**, then **Open** again.
- **Or, in Terminal:** `xattr -dr com.apple.quarantine /Applications/Pulse.app`

Pulse lives in the menu bar; it has no Dock icon.
