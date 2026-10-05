# Pulse for ChromeOS

ChromeOS does not run outside apps with this level of system access, so the real Pulse ships as a system app inside a ChromeOS build. Its source is in `native/chromeos/chromium`, laid out to drop into a Chromium checkout; `native/chromeos/README.md` gives the steps.

`preview/` is the same screen running on sample data, so you can try it in any browser:

```
cd build/chromeos/preview
python3 -m http.server 8000
```

Then open http://localhost:8000. It needs a local server because the page loads its scripts as modules.
