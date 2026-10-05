using Microsoft.Win32;
using Pulse.App.Interop;

namespace Pulse.App.Tray;

/// <summary>
/// Draws the tray icon: two rounded meters, CPU (green, orange under a hog
/// alert) and RAM (sky blue), on a track that suits the taskbar theme.
/// Pixels are written straight into a 32-bit premultiplied DIB.
/// </summary>
public static unsafe class IconRenderer
{
    public static bool TaskbarIsLight()
    {
        try
        {
            using var k = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize");
            return k?.GetValue("SystemUsesLightTheme") is int v && v == 1;
        }
        catch (Exception) { return false; }
    }

    public static nint Render(int size, double cpuPct, double ramPct, bool hog, bool lightTaskbar)
    {
        size = Math.Clamp(size, 16, 64);
        var bmi = new BITMAPINFOHEADER { biSize = (uint)sizeof(BITMAPINFOHEADER), biWidth = size, biHeight = -size, biPlanes = 1, biBitCount = 32 };
        void* bits;
        nint color = Win32.CreateDIBSection(0, &bmi, 0, &bits, 0, 0);
        if (color == 0) return 0;
        nint mask = Win32.CreateBitmap(size, size, 1, 1, null);
        try
        {
            var px = new Span<uint>(bits, size * size);
            px.Clear();
            uint track = lightTaskbar ? Argb(0x40, 0x0E, 0x1E, 0x19) : Argb(0x59, 0xFF, 0xFF, 0xFF);
            uint cpuColor = hog ? Argb(0xFF, 0xF9, 0x73, 0x16) : Argb(0xFF, 0x10, 0xB9, 0x81);
            uint ramColor = Argb(0xFF, 0x0E, 0xA5, 0xE9);

            double s = size / 16.0;
            int w = (int)Math.Round(5 * s), gap = (int)Math.Round(2 * s);
            int top = (int)Math.Round(1 * s), bottom = size - (int)Math.Round(1 * s);
            int x1 = (size - (2 * w + gap)) / 2, x2 = x1 + w + gap;
            Bar(px, size, x1, top, w, bottom - top, cpuPct, track, cpuColor);
            Bar(px, size, x2, top, w, bottom - top, ramPct, track, ramColor);

            var info = new ICONINFO { fIcon = 1, hbmColor = color, hbmMask = mask };
            return Win32.CreateIconIndirect(&info);
        }
        finally
        {
            Win32.DeleteObject(color);
            if (mask != 0) Win32.DeleteObject(mask);
        }
    }

    static void Bar(Span<uint> px, int size, int x, int y, int w, int h, double pct, uint track, uint fill)
    {
        int filled = (int)Math.Round(h * Math.Clamp(pct, 0, 100) / 100.0);
        if (pct > 0.5) filled = Math.Max(filled, 1);
        double r = w / 2.0;
        for (int yy = 0; yy < h; yy++)
            for (int xx = 0; xx < w; xx++)
            {
                // Rounded caps: coverage from distance to the cap centres.
                double cx = xx + 0.5, cy = yy + 0.5, d = 0;
                if (cy < r) d = Math.Sqrt((cx - r) * (cx - r) + (cy - r) * (cy - r)) - r;
                else if (cy > h - r) d = Math.Sqrt((cx - r) * (cx - r) + (cy - (h - r)) * (cy - (h - r))) - r;
                double cover = Math.Clamp(0.5 - d, 0, 1);
                if (cover <= 0) continue;
                uint c = yy >= h - filled ? fill : track;
                px[(y + yy) * size + x + xx] = Scale(c, cover);
            }
    }

    static uint Argb(uint a, uint r, uint g, uint b) => (a << 24) | (r * a / 255 << 16) | (g * a / 255 << 8) | (b * a / 255);

    static uint Scale(uint c, double k)
    {
        uint a = (uint)((c >> 24) * k), r = (uint)(((c >> 16) & 0xFF) * k), g = (uint)(((c >> 8) & 0xFF) * k), b = (uint)((c & 0xFF) * k);
        return (a << 24) | (r << 16) | (g << 8) | b;
    }
}
