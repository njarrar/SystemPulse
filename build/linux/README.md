# Pulse for Linux

`pulse` is one 64-bit binary built on Ubuntu 24.04. It needs GTK 4.14+ and libadwaita 1.5+ (Ubuntu 24.04, Fedora 40 and later have them).

```
chmod +x pulse
./pulse
```

The top bar readout needs the AppIndicator extension on GNOME. `./pulse --show-flyout` opens the panel without it. `./pulse --help` lists every option.
