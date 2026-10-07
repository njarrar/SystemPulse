# Pulse for Windows 11

`Pulse-win-x64.zip` (most PCs) and `Pulse-win-arm64.zip` (Arm PCs such as Surface Pro X or Snapdragon laptops). GitHub Actions builds them from `native/win11` on every change, starts each one from a clean folder on two Windows machines, and places them here.

Right-click the zip, choose **Extract All**, then run `Pulse.exe` from the extracted folder. Running it from inside the zip does not work. Pulse lives in the notification area next to the clock (Windows may tuck the icon under the `^` arrow). Built for Windows 11 22H2 or later.

Inside the zip:

| Item | What it is |
|---|---|
| `Pulse.exe` | Starts Pulse |
| `data\` | The app and the Windows App SDK files it needs |
| `lang\` | One folder per language, such as `lang\en` and `lang\ar` |

If Pulse cannot start, it says so in a message box and writes the details to `%LOCALAPPDATA%\Pulse\crash.log`.
