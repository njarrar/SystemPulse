# Pulse icon

`pulse-icon.svg` is the source. Five bars, one per vital, in the spec colours: CPU `#10B981`, energy `#F59E0B`, memory `#0EA5E9`, thermal `#F43F5E`, GPU `#14B8A6`, on a white tile with an ink (`#0E1E19`) baseline.

`node brand/render.mjs` (needs Playwright) redraws:

| File | Used by |
|---|---|
| `brand/pulse-icon-512.png` | README, store pages |
| `brand/social-preview.png` | GitHub social preview (1280 x 640) |
| `native/chromeos/.../resources/app_icon.svg`, `app_icon_{48,128,256}.png` | ChromeOS app |
| `native/win11/src/Pulse.App/Assets/Pulse.ico` | Windows 11 app |
| `native/macos/Assets/AppIcon.icns` | macOS app |

Windows XP and 98 draw their icons at build time with the same shapes (`native/winxp/tools/make_icon.py`, `native/win98/tools/gen_icon.py`). The 98 icon uses 16 colours.
