/*
 * Text output for Win9x (ANSI + charset font) and NT (UTF-16).
 */
#include "text.h"

#define TXT_CAP 512

static int g_ansi = 1, g_cp = 1252, g_rtl = 0;
static WCHAR g_w[TXT_CAP + 4];
static char g_a[TXT_CAP * 2 + 8];

void txt_mode(int ansi, int codepage, int rtl) { g_ansi = ansi; g_cp = codepage; g_rtl = rtl; }
int txt_is_ansi(void) { return g_ansi; }

int utf8_to_w(const char *s, WCHAR *out, int cap)
{
    const unsigned char *u = (const unsigned char *)s;
    int n = 0;
    while (*u && n < cap - 1) {
        unsigned long cp;
        if (*u < 0x80) cp = *u++;
        else if ((*u & 0xE0) == 0xC0 && u[1]) { cp = ((unsigned long)(*u & 0x1F) << 6) | (u[1] & 0x3F); u += 2; }
        else if ((*u & 0xF0) == 0xE0 && u[1] && u[2]) { cp = ((unsigned long)(*u & 0x0F) << 12) | ((unsigned long)(u[1] & 0x3F) << 6) | (u[2] & 0x3F); u += 3; }
        else if ((*u & 0xF8) == 0xF0 && u[1] && u[2] && u[3]) { u += 4; cp = 0xFFFD; }
        else { u++; cp = 0xFFFD; }
        out[n++] = (WCHAR)cp;
    }
    out[n] = 0;
    return n;
}

int w_to_utf8(const WCHAR *s, int len, char *out, int cap)
{
    int k, n = 0;
    for (k = 0; k < len && s[k]; k++) {
        unsigned long c = s[k];
        if (c < 0x80) { if (n + 1 >= cap) break; out[n++] = (char)c; }
        else if (c < 0x800) { if (n + 2 >= cap) break; out[n++] = (char)(0xC0 | (c >> 6)); out[n++] = (char)(0x80 | (c & 0x3F)); }
        else { if (n + 3 >= cap) break; out[n++] = (char)(0xE0 | (c >> 12)); out[n++] = (char)(0x80 | ((c >> 6) & 0x3F)); out[n++] = (char)(0x80 | (c & 0x3F)); }
    }
    out[n] = 0;
    return n;
}

int acp_to_utf8(const char *s, char *out, int cap)
{
    WCHAR w[260];
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, w, 260);
    if (n <= 0) { out[0] = 0; return 0; }
    return w_to_utf8(w, n - 1, out, cap);
}

static int is_rtl_char(WCHAR c)
{
    return (c >= 0x0590 && c <= 0x08FF) || (c >= 0xFB1D && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF);
}

/* Replaces characters with no glyph or code page slot on 9x/NT4/2000. */
static int prepare(const char *utf8)
{
    int n = utf8_to_w(utf8, g_w, TXT_CAP), k, j;
    for (k = 0; k < n; k++) {
        WCHAR c = g_w[k];
        if (c == 0x2068) {
            /* Pick LRM or RLM from the first strong letter inside the isolate. */
            WCHAR mark = 0x200E;
            for (j = k + 1; j < n && g_w[j] != 0x2069; j++) {
                if (is_rtl_char(g_w[j])) { mark = 0x200F; break; }
                if ((g_w[j] >= 'A' && g_w[j] <= 'Z') || (g_w[j] >= 'a' && g_w[j] <= 'z')) break;
            }
            g_w[k] = mark;
            /* Close with the same mark. */
            for (j = k + 1; j < n; j++) if (g_w[j] == 0x2069) { g_w[j] = mark; break; }
        } else if (c == 0x2069) g_w[k] = 0x200E;
        else if (c == 0x2212) g_w[k] = '-';
        else if (c == 0x2009 || c == 0x202F || c == 0x00A0) g_w[k] = ' ';
        else if (c == 0x2192) g_w[k] = '>';
        else if (c == 0x2190) g_w[k] = '<';
    }
    /* Outside RTL text the marks only add risk; drop them in LTR locales. */
    if (!g_rtl) {
        for (k = 0, j = 0; k < n; k++) if (g_w[k] != 0x200E && g_w[k] != 0x200F) g_w[j++] = g_w[k];
        n = j;
        g_w[n] = 0;
    }
    return n;
}

static int to_ansi(int n)
{
    int len = WideCharToMultiByte(g_cp, 0, g_w, n, g_a, sizeof(g_a) - 1, 0, 0);
    if (len <= 0) len = WideCharToMultiByte(CP_ACP, 0, g_w, n, g_a, sizeof(g_a) - 1, 0, 0);
    if (len < 0) len = 0;
    g_a[len] = 0;
    return len;
}

static int measure(HDC dc, int n)
{
    SIZE sz;
    sz.cx = 0;
    if (g_ansi) { int len = to_ansi(n); GetTextExtentPoint32A(dc, g_a, len, &sz); }
    else GetTextExtentPoint32W(dc, g_w, n, &sz);
    return sz.cx;
}

int txt_width(HDC dc, const char *utf8)
{
    return measure(dc, prepare(utf8));
}

int txt_to_ansi(const char *utf8, char *out, int cap)
{
    int n = prepare(utf8), len;
    len = WideCharToMultiByte(g_cp, 0, g_w, n, out, cap - 1, 0, 0);
    if (len <= 0) len = WideCharToMultiByte(CP_ACP, 0, g_w, n, out, cap - 1, 0, 0);
    if (len < 0) len = 0;
    out[len] = 0;
    return len;
}

int txt_draw(HDC dc, const RECT *r, const char *utf8, int align, COLORREF color)
{
    int n = prepare(utf8), w, x, y, maxw = r->right - r->left;
    TEXTMETRICA tm;
    UINT flags = 0;
    if (maxw <= 0 || n == 0) return 0;
    /* RTL reading order only for text with RTL letters: numbers with units
     * and Latin names keep their LTR order. */
    if (g_rtl) {
        int k;
        for (k = 0; k < n; k++) if (is_rtl_char(g_w[k])) { flags = ETO_RTLREADING; break; }
    }
    w = measure(dc, n);
    if (w > maxw) {
        /* Trim the logical end and add an ellipsis. */
        int lo = 0, hi = n, keep = 0;
        while (lo <= hi) {
            int mid = (lo + hi) / 2, mw;
            WCHAR save = g_w[mid], save2 = g_w[mid + 1];
            g_w[mid] = 0x2026; g_w[mid + 1] = 0;
            mw = measure(dc, mid + 1);
            g_w[mid] = save; g_w[mid + 1] = save2;
            if (mw <= maxw) { keep = mid; lo = mid + 1; } else hi = mid - 1;
        }
        while (keep > 0 && g_w[keep - 1] == ' ') keep--;
        g_w[keep] = 0x2026; g_w[keep + 1] = 0;
        n = keep + 1;
        w = measure(dc, n);
    }
    GetTextMetricsA(dc, &tm);
    y = r->top + ((r->bottom - r->top) - tm.tmHeight) / 2;
    if (align == TA_CENTER_) x = r->left + (maxw - w) / 2;
    else if ((align == TA_START_) != (g_rtl != 0)) x = r->left;
    else x = r->right - w;
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    SetTextAlign(dc, TA_LEFT | TA_TOP);
    if (g_ansi) { int len = to_ansi(n); ExtTextOutA(dc, x, y, flags, 0, g_a, len, 0); }
    else ExtTextOutW(dc, x, y, flags, 0, g_w, n, 0);
    return w;
}
