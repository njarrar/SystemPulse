/*
 * Pulse for Windows XP (SP2/SP3, 32-bit x86).
 *
 * One WS_POPUP flyout drawn into a double-buffered DIB: GDI+ for shapes,
 * sparklines and charts (anti-aliased Catmull-Rom curves through
 * GraphicsPath::AddBeziers), GDI DrawTextW for text so Uniscribe shapes
 * Arabic. A Shell_NotifyIconW tray icon shows a live 16x16 CPU waveform.
 *
 * Layout works in logical coordinates where x = 0 is the start edge. In RTL
 * the window gets WS_EX_LAYOUTRTL, so mouse coordinates arrive already
 * logical; drawing mirrors x (x_rtl = width - x - w) through a GDI+ world
 * transform for shapes and through the same formula for text rectangles.
 */
#define _WIN32_WINNT 0x0501
#define WINVER 0x0501
#define _WIN32_IE 0x0600
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <psapi.h>
#include <gdiplus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>

extern "C" {
#include "i18n.h"
}
#include "catalog_keys.h"
#include "telemetry.h"

using namespace Gdiplus;

/* ------------------------------------------------------------------------ */
/* Tokens (Part 1.1, Windows XP classic)                                    */
/* ------------------------------------------------------------------------ */
#define C_FLY_BG   0xFFECE9D8
#define C_BORDER   0xFF0831D9
#define C_CARD     0xFFFFFFFF
#define C_CARD_HOV 0xFFF3F7FF
#define C_CARD_B   0xFFACA899
#define C_INK      0xFF000000
#define C_INK2     0xFF3F3F3F
#define C_INK3     0xFF5A5A5A
#define C_TRACK    0xFFE5E2D3
#define C_TRACK_S  0xFFACA899
#define C_HOVER    0xFFE8EFFC
#define C_TIP_BG   0xFFFFFFE1
#define C_TIP_B    0xFF000000
#define C_SEG_B    0xFF7F9DB9
#define C_CPU      0xFF1FA055
#define C_MEM      0xFF2B7BD6
#define C_NRG      0xFFE8A317
#define C_THM      0xFFD9344F
#define C_GPU      0xFF1D9A8E
#define C_WARN     0xFFF27B1E
#define C_CRIT     0xFFD93A2B
#define C_CPU_INK  0xFF13703A
#define C_MEM_INK  0xFF1A4F96
#define C_NRG_INK  0xFF8A5A00
#define C_THM_INK  0xFFA21C33
#define C_GPU_INK  0xFF106B62
#define C_WARN_INK 0xFFA84A00
#define C_CRIT_INK 0xFFA3261A
#define C_DANGER   0xFFD93A2B

static UINT32 tint(UINT32 c, int pct) { return (c & 0x00FFFFFF) | ((UINT32)(pct * 255 / 100) << 24); }
static COLORREF cref(UINT32 c) { return RGB((c >> 16) & 255, (c >> 8) & 255, c & 255); }
static Color col(UINT32 c) { return Color((BYTE)(c >> 24), (BYTE)(c >> 16), (BYTE)(c >> 8), (BYTE)c); }

enum Acc { A_CPU, A_MEM, A_NRG, A_THM, A_GPU, A_WARN, A_CRIT };
static const UINT32 ACC[] = { C_CPU, C_MEM, C_NRG, C_THM, C_GPU, C_WARN, C_CRIT };
static const UINT32 ACC_INK[] = { C_CPU_INK, C_MEM_INK, C_NRG_INK, C_THM_INK, C_GPU_INK, C_WARN_INK, C_CRIT_INK };

/* ------------------------------------------------------------------------ */
/* State                                                                    */
/* ------------------------------------------------------------------------ */
enum View { V_NONE, V_CPU, V_MEM, V_NRG, V_THM, V_GPU, V_SSD, V_NET, V_APP, V_SETTINGS };
enum Act {
    X_NONE, X_CLOSE, X_LANG, X_LANGMENU, X_SETTINGS, X_CARD, X_SORT, X_ROW, X_END, X_HOG_END, X_HOG_DISMISS,
    X_CONFIRM_YES, X_CONFIRM_NO, X_BACK, X_END_CURRENT, X_MONITOR, X_QUIT, X_SWITCH, X_UNIT, X_RESTORE, X_CHART, X_LANGPICK
};

#define WIN_W 420
#define FRAME 3
#define TB 29
#define X0 13.0f
#define CWID 394.0f
#define GAP 8.0f
#define HALF 193.0f
#define ROW_H 30.0f
#define MAX_HITS 96
#define WM_TRAY (WM_APP + 1)
#define TIMER_POLL 1
#define TIMER_TOAST 2
#define TIMER_SCRIPT 3
#define WM_LANGPICK (WM_APP + 2)

struct Hit { float x, y, w, h; int act, arg, focus; };

static HINSTANCE g_inst;
static HWND g_hwnd;
static Telemetry g_tel;
static Locale *L;
static int g_lang, g_lang_pref = -1;  /* pref: -1 = match system, else locale index */
static float g_combo_x, g_combo_y, g_combo_w, g_combo_h;
static int g_view = V_NONE, g_last_view = V_CPU, g_sort;
static WCHAR g_view_app[64];
static int g_hover = -1, g_hover_row = -1, g_chart = -1, g_focus = -1;
static WCHAR g_dismissed[64], g_confirm[64], g_toast[256];
static int g_sim_hog, g_sim_charging, g_live = 1, g_unit_f, g_pinned, g_visible;
static int g_scroll, g_content_h, g_win_h;
static Hit g_hits[MAX_HITS];
static int g_nhits;
static ULONG_PTR g_gdip;
static HDC g_mdc, g_measure_dc;
static HBITMAP g_mbmp;
static void *g_mbits;
static int g_mbmp_h;
static NOTIFYICONDATAW g_nid;
static int g_tray_ok;
static UINT g_msg_taskbar;
static HICON g_tray_icon;
static WCHAR g_cpu_model[96];
static int g_seed;
static int g_in_menu;
static DWORD g_hidden_at;

/* rendering context */
static Graphics *G;
static int RTL;
static float DY;

/* ------------------------------------------------------------------------ */
/* uxtheme, loaded on demand (absent or inactive: draw Luna by hand)          */
/* ------------------------------------------------------------------------ */
typedef HANDLE HTHEME_;
typedef HTHEME_ (WINAPI *OpenThemeData_t)(HWND, LPCWSTR);
typedef HRESULT (WINAPI *CloseThemeData_t)(HTHEME_);
typedef HRESULT (WINAPI *DrawThemeBackground_t)(HTHEME_, HDC, int, int, const RECT *, const RECT *);
typedef BOOL (WINAPI *IsThemeActive_t)(void);
static OpenThemeData_t pOpenThemeData;
static CloseThemeData_t pCloseThemeData;
static DrawThemeBackground_t pDrawThemeBackground;
static IsThemeActive_t pIsThemeActive;
static HTHEME_ g_th_window, g_th_button;

static void theme_open(void) {
    static HMODULE ux;
    if (!ux) ux = LoadLibraryW(L"uxtheme.dll");
    if (!ux) return;
    pOpenThemeData = (OpenThemeData_t)GetProcAddress(ux, "OpenThemeData");
    pCloseThemeData = (CloseThemeData_t)GetProcAddress(ux, "CloseThemeData");
    pDrawThemeBackground = (DrawThemeBackground_t)GetProcAddress(ux, "DrawThemeBackground");
    pIsThemeActive = (IsThemeActive_t)GetProcAddress(ux, "IsThemeActive");
    if (g_th_window && pCloseThemeData) pCloseThemeData(g_th_window);
    if (g_th_button && pCloseThemeData) pCloseThemeData(g_th_button);
    g_th_window = g_th_button = 0;
    if (pIsThemeActive && pOpenThemeData && pDrawThemeBackground && pIsThemeActive()) {
        g_th_window = pOpenThemeData(g_hwnd, L"WINDOW");
        g_th_button = pOpenThemeData(g_hwnd, L"BUTTON");
    }
}

/* ------------------------------------------------------------------------ */
/* Strings                                                                  */
/* ------------------------------------------------------------------------ */
static WCHAR g_ring[96][512];
static int g_ring_i;
static WCHAR *rb(void) { WCHAR *s = g_ring[g_ring_i++ % 96]; s[0] = 0; return s; }
#define U(x) ((u16 *)(x))
#define CU(x) ((const u16 *)(x))

static const u16 *res_lookup(unsigned id, int *len) {
    const WCHAR *p = 0;
    int n = LoadStringW(g_inst, id, (LPWSTR)&p, 0);
    *len = n;
    return n > 0 ? (const u16 *)p : 0;
}

static WCHAR *tr_v(int key, int hw, int np, const char **names, const WCHAR **vals) {
    I18nParam p[6];
    WCHAR *o = rb();
    for (int k = 0; k < np; k++) { p[k].name = names[k]; p[k].value = CU(vals[k]); }
    if (hw) i18n_hw(L, key, p, np, U(o), 512);
    else i18n_t(L, key, p, np, U(o), 512);
    return o;
}
static WCHAR *tr(int key) { return tr_v(key, 0, 0, 0, 0); }
static WCHAR *tr1(int key, const char *a, const WCHAR *va) { const char *n[] = { a }; const WCHAR *v[] = { va }; return tr_v(key, 0, 1, n, v); }
static WCHAR *tr2(int key, const char *a, const WCHAR *va, const char *b, const WCHAR *vb) { const char *n[] = { a, b }; const WCHAR *v[] = { va, vb }; return tr_v(key, 0, 2, n, v); }
static WCHAR *tr3(int key, const char *a, const WCHAR *va, const char *b, const WCHAR *vb, const char *c, const WCHAR *vc) { const char *n[] = { a, b, c }; const WCHAR *v[] = { va, vb, vc }; return tr_v(key, 0, 3, n, v); }
static WCHAR *hw(int key) { return tr_v(key, 1, 0, 0, 0); }
static WCHAR *hw1(int key, const char *a, const WCHAR *va) { const char *n[] = { a }; const WCHAR *v[] = { va }; return tr_v(key, 1, 1, n, v); }
static WCHAR *hw3(int key, const char *a, const WCHAR *va, const char *b, const WCHAR *vb, const char *c, const WCHAR *vc) { const char *n[] = { a, b, c }; const WCHAR *v[] = { va, vb, vc }; return tr_v(key, 1, 3, n, v); }
static WCHAR *plural(int key, double n) { WCHAR *o = rb(); i18n_plural(L, key, n, -1, 0, 0, U(o), 512); return o; }
static WCHAR *join2(const WCHAR *a, const WCHAR *b) { const u16 *it[2] = { CU(a), CU(b) }; WCHAR *o = rb(); i18n_join(L, it, 2, U(o), 512); return o; }
static WCHAR *join3(const WCHAR *a, const WCHAR *b, const WCHAR *c) { const u16 *it[3] = { CU(a), CU(b), CU(c) }; WCHAR *o = rb(); i18n_join(L, it, 3, U(o), 512); return o; }
static WCHAR *app_name(const WCHAR *n) { WCHAR *o = rb(); i18n_app(L, CU(n), U(o), 512); return o; }
static WCHAR *num(double x, int d) { WCHAR *o = rb(); i18n_num(L, x, d, U(o), 64); return o; }
/* isolates a value inside RTL text, as the i18n engine does for {params} */
static WCHAR *iso(const WCHAR *v) { WCHAR *o = rb(); if (L->rtl) _snwprintf(o, 511, L"\x2068%ls\x2069", v); else lstrcpynW(o, v, 512); o[511] = 0; return o; }
static WCHAR *cat2(const WCHAR *a, const WCHAR *b) { WCHAR *o = rb(); _snwprintf(o, 511, L"%ls%ls", a, b); o[511] = 0; return o; }
static WCHAR *cat3(const WCHAR *a, const WCHAR *b, const WCHAR *c) { WCHAR *o = rb(); _snwprintf(o, 511, L"%ls%ls%ls", a, b, c); o[511] = 0; return o; }
static WCHAR *pct(double x, int d) { return cat2(num(d ? x : floor(x + 0.5), d), L"%"); }
static WCHAR *temp(double c, int d) { return g_unit_f ? cat2(num(c * 9 / 5 + 32, d), L"\x00B0" L"F") : cat2(num(c, d), L"\x00B0" L"C"); }
static WCHAR *watts(double w) { return cat2(num(w, 1), L" W"); }
static WCHAR *gb(double g, int d) { return cat2(num(g, d), L" GB"); }
static WCHAR *memf(double mb) { return mb < 256 ? cat2(num(mb, mb < 10 ? 1 : 0), L" MB") : cat2(num(mb / 1024, 2), L" GB"); }
static WCHAR *down_s(double kbs) { return cat2(num(kbs / 1024, 2), L" MB/s"); }
static WCHAR *up_s(double kbs) { return cat2(num(floor(kbs + 0.5), 0), L" KB/s"); }
static WCHAR *io_s(double mbs) { return cat2(num(mbs, 1), L" MB/s"); }
static WCHAR *dur(int h, int m) {
    return h ? tr2(S_durHM, "h", num(h, 0), "m", num(m, 0)) : tr1(S_durM, "m", num(m, 0));
}
static const WCHAR *DASH = L"\x2014";

/* ------------------------------------------------------------------------ */
/* Fonts                                                                    */
/* ------------------------------------------------------------------------ */
enum { FF_UI, FF_HERO, FF_CAPTION, FF_MONO };
static HFONT g_fonts[4][2][40];
static WCHAR g_face[4][LF_FACESIZE];

static int CALLBACK font_found(const LOGFONTW *, const TEXTMETRICW *, DWORD, LPARAM lp) { *(int *)lp = 1; return 0; }
static int font_installed(const WCHAR *face) {
    LOGFONTW lf;
    int found = 0;
    memset(&lf, 0, sizeof lf);
    lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpynW(lf.lfFaceName, face, LF_FACESIZE);
    EnumFontFamiliesExW(g_measure_dc, &lf, (FONTENUMPROCW)font_found, (LPARAM)&found, 0);
    return found;
}
/* first installed family of a CSS-like stack, else fallback */
static void pick_face(const u16 *stack, const WCHAR *fallback, WCHAR *out) {
    WCHAR buf[128];
    lstrcpynW(out, fallback, LF_FACESIZE);
    if (!stack || !stack[0]) return;
    lstrcpynW(buf, (const WCHAR *)stack, 128);
    for (WCHAR *tok = buf; tok && *tok; ) {
        WCHAR *comma = wcschr(tok, L',');
        if (comma) *comma = 0;
        while (*tok == L' ' || *tok == L'"' || *tok == L'\'') tok++;
        int n = lstrlenW(tok);
        while (n && (tok[n - 1] == L' ' || tok[n - 1] == L'"' || tok[n - 1] == L'\'')) tok[--n] = 0;
        if (n && _wcsicmp(tok, L"sans-serif") && _wcsicmp(tok, L"system-ui") && font_installed(tok)) { lstrcpynW(out, tok, LF_FACESIZE); return; }
        tok = comma ? comma + 1 : 0;
    }
}
static void fonts_reset(void) {
    for (int f = 0; f < 4; f++) for (int b = 0; b < 2; b++) for (int s = 0; s < 40; s++)
        if (g_fonts[f][b][s]) { DeleteObject(g_fonts[f][b][s]); g_fonts[f][b][s] = 0; }
    pick_face(L->font_ui, L"Tahoma", g_face[FF_UI]);
    pick_face(L->font_hero, g_face[FF_UI], g_face[FF_HERO]);
    lstrcpyW(g_face[FF_CAPTION], font_installed(L"Trebuchet MS") && !L->rtl ? L"Trebuchet MS" : g_face[FF_UI]);
    lstrcpyW(g_face[FF_MONO], font_installed(L"Lucida Console") ? L"Lucida Console" : g_face[FF_UI]);
}
static HFONT font(int fam, int px, int bold) {
    if (px >= 40) px = 39;
    HFONT *f = &g_fonts[fam][bold ? 1 : 0][px];
    if (!*f) *f = CreateFontW(-px, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, g_face[fam]);
    return *f;
}

/* ------------------------------------------------------------------------ */
/* Drawing helpers (logical coordinates)                                    */
/* ------------------------------------------------------------------------ */
enum { T_START = 0, T_END = 1, T_CENTER = 2, T_LTR = 4, T_TOP = 8 };

static void set_xform(void) {
    Matrix m(RTL ? -1.0f : 1.0f, 0, 0, 1, RTL ? (REAL)WIN_W : 0, DY);
    G->SetTransform(&m);
}
/*
 * Bidi text. The i18n layer wraps inserted values in FSI ... PDI. Rather
 * than hand those controls to DrawTextW (XP Uniscribe predates Unicode 6.3
 * isolates, and some GDI builds measure them as visible glyphs), each line
 * is split into runs at isolate boundaries and the runs are placed in visual
 * order here: right to left in an RTL paragraph, left to right in an LTR one,
 * recursively for nested isolates. Each run is then drawn on its own, with
 * DT_RTLREADING when its direction is RTL, so numbers and units stay LTR.
 */
struct Leaf { const WCHAR *s; int len, rtl; float w; };
#define MAX_LEAVES 24
static int is_rtl_wc(WCHAR c) { return (c >= 0x0590 && c <= 0x08FF) || (c >= 0xFB1D && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF); }
static int is_ltr_wc(WCHAR c) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) return 1;
    if (c >= 0x00C0 && c <= 0x024F) return c != 0x00D7 && c != 0x00F7;
    return (c >= 0x0370 && c < 0x0590) || (c >= 0x0900 && c < 0x2000) || (c >= 0x3040 && c < 0xD800);
}
static int first_strong_rtl(const WCHAR *s, int len, int dflt) {
    int depth = 0;
    for (int i = 0; i < len; i++) {
        WCHAR c = s[i];
        if (c == 0x2066 || c == 0x2067 || c == 0x2068) { depth++; continue; }
        if (c == 0x2069) { if (depth) depth--; continue; }
        if (depth) continue;
        if (is_rtl_wc(c) && !(c >= 0x0660 && c <= 0x0669) && !(c >= 0x06F0 && c <= 0x06F9)) return 1;
        if (is_ltr_wc(c)) return 0;
    }
    return dflt;
}
static void bidi_runs(const WCHAR *s, int len, int rtl, Leaf *out, int *n) {
    struct { const WCHAR *s; int len, iso; } seg[MAX_LEAVES];
    int ns = 0, i = 0;
    while (i < len && ns < MAX_LEAVES) {
        WCHAR c = s[i];
        if (c == 0x2066 || c == 0x2067 || c == 0x2068) {
            int depth = 1, j = i + 1;
            while (j < len && depth) { if (s[j] >= 0x2066 && s[j] <= 0x2068) depth++; else if (s[j] == 0x2069) depth--; if (depth) j++; }
            seg[ns].s = s + i + 1; seg[ns].len = j - i - 1; seg[ns].iso = c == 0x2066 ? 1 : c == 0x2067 ? 2 : 3; ns++;
            i = j + 1;
        } else if (c == 0x2069 || c == 0x202A || c == 0x202B || c == 0x202C || c == 0x200E || c == 0x200F) {
            i++;
        } else {
            int j = i;
            while (j < len && !(s[j] >= 0x2066 && s[j] <= 0x2069) && !(s[j] >= 0x202A && s[j] <= 0x202C)) j++;
            seg[ns].s = s + i; seg[ns].len = j - i; seg[ns].iso = 0; ns++;
            i = j;
        }
    }
    for (int k = 0; k < ns; k++) {
        int idx = rtl ? ns - 1 - k : k;
        if (!seg[idx].len) continue;
        if (seg[idx].iso) {
            int d = seg[idx].iso == 1 ? 0 : seg[idx].iso == 2 ? 1 : first_strong_rtl(seg[idx].s, seg[idx].len, 0);
            bidi_runs(seg[idx].s, seg[idx].len, d, out, n);
        } else if (*n < MAX_LEAVES) {
            out[*n].s = seg[idx].s; out[*n].len = seg[idx].len; out[*n].rtl = rtl; (*n)++;
        }
    }
}
static int leaves_of(const WCHAR *s, int para_rtl, HFONT f, Leaf *lv, float *total) {
    int n = 0;
    bidi_runs(s, lstrlenW(s), para_rtl, lv, &n);
    SelectObject(g_measure_dc, f);
    *total = 0;
    for (int k = 0; k < n; k++) {
        RECT r = { 0, 0, 4000, 100 };
        DrawTextW(g_measure_dc, lv[k].s, lv[k].len, &r, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX | (lv[k].rtl ? DT_RTLREADING : 0));
        lv[k].w = (float)(r.right - r.left);
        *total += lv[k].w;
    }
    return n;
}
static int measure(const WCHAR *s, HFONT f) {
    Leaf lv[MAX_LEAVES];
    float total;
    leaves_of(s, L->rtl, f, lv, &total);
    return (int)ceilf(total) + 1;
}
static void text(const WCHAR *s, float x, float y, float w, float h, HFONT f, UINT32 color, int fl) {
    Leaf lv[MAX_LEAVES];
    float total;
    int para_rtl = RTL && !(fl & T_LTR);
    int n = leaves_of(s, para_rtl, f, lv, &total);
    int al = fl & 3;
    float px = RTL ? WIN_W - x - w : x;
    /* too long: shrink from the logical end (left side in RTL) */
    int first = 0, last = n - 1;
    float avail = w;
    int ell = -1;
    if (total > avail) {
        float acc = 0;
        if (para_rtl) {
            for (last = n - 1; last >= 0; last--) { if (acc + lv[last].w > avail) break; acc += lv[last].w; }
            first = last < 0 ? 0 : last; ell = first; lv[first].w = avail - acc; last = n - 1;
        } else {
            for (first = 0; first < n; first++) { if (acc + lv[first].w > avail) break; acc += lv[first].w; }
            ell = first < n ? first : n - 1; lv[ell].w = avail - acc; last = ell; first = 0;
        }
        total = avail;
    }
    float sx;
    int start_left = (al == T_START) != (RTL != 0);
    if (al == T_CENTER) sx = px + (w - total) / 2;
    else if (start_left) sx = px;
    else sx = px + w - total;
    UINT vf = (fl & T_TOP) ? DT_TOP : DT_VCENTER;
    HDC dc = G->GetHDC();
    SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, cref(color));
    for (int k = first; k <= last && k < n; k++) {
        if (lv[k].w <= 0) continue;
        RECT r;
        r.left = (LONG)floorf(sx + 0.5f);
        r.right = (LONG)floorf(sx + lv[k].w + 0.5f) + (k == ell ? 0 : 2);
        r.top = (LONG)floorf(y + DY + 0.5f);
        r.bottom = (LONG)floorf(y + h + DY + 0.5f);
        UINT dt = DT_SINGLELINE | DT_NOPREFIX | vf | (lv[k].rtl ? DT_RTLREADING | DT_RIGHT : DT_LEFT);
        if (k == ell) dt |= DT_END_ELLIPSIS;
        if (k == ell && lv[k].rtl) { dt &= ~DT_RIGHT; dt |= DT_LEFT; }
        DrawTextW(dc, lv[k].s, lv[k].len, &r, dt);
        sx += lv[k].w;
    }
    G->ReleaseHDC(dc);
}
static void round_path(GraphicsPath &p, float x, float y, float w, float h, float r) {
    if (r <= 0) { p.AddRectangle(RectF(x, y, w, h)); return; }
    float d = r * 2;
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
}
static void fill_round(float x, float y, float w, float h, float r, UINT32 c) {
    GraphicsPath p;
    round_path(p, x, y, w, h, r);
    SolidBrush b(col(c));
    G->FillPath(&b, &p);
}
static void stroke_round(float x, float y, float w, float h, float r, UINT32 c) {
    GraphicsPath p;
    round_path(p, x + 0.5f, y + 0.5f, w - 1, h - 1, r);
    Pen pen(col(c), 1);
    G->DrawPath(&pen, &p);
}
static void card(float x, float y, float w, float h, UINT32 bg, UINT32 border) {
    fill_round(x, y, w, h, 3, bg);
    stroke_round(x, y, w, h, 3, border);
}
static void line(float x1, float y1, float x2, float y2, UINT32 c, float width) {
    Pen pen(col(c), width);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    G->DrawLine(&pen, x1, y1, x2, y2);
}
static int add_hit(float x, float y, float w, float h, int act, int arg, int focus) {
    if (g_nhits >= MAX_HITS) return -1;
    Hit *t = &g_hits[g_nhits];
    t->x = x; t->y = y + DY; t->w = w; t->h = h; t->act = act; t->arg = arg; t->focus = focus;
    return g_nhits++;
}
static int hovered(int act, int arg) {
    return g_hover >= 0 && g_hover < g_nhits && g_hits[g_hover].act == act && g_hits[g_hover].arg == arg;
}
static void focus_ring(float x, float y, float w, float h) {
    Pen pen(col(0xFF000000), 1);
    pen.SetDashStyle(DashStyleDot);
    G->DrawRectangle(&pen, x + 1.5f, y + 1.5f, w - 3, h - 3);
}

/* ---- icons (16 px box) ---------------------------------------------------- */
enum { I_CPU, I_MEM, I_NRG, I_THM, I_GPU, I_DISK, I_NET, I_WARN, I_GEAR, I_EXT, I_LOCK, I_CHEV, I_BACK, I_CLOSE, I_DOWN, I_UP };
static void icon(int which, float x, float y, float s, UINT32 c) {
    float k = s / 16.0f;
    Pen pen(col(c), 1.4f * (k < 1 ? 1 : k));
    pen.SetLineJoin(LineJoinRound);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    SolidBrush br(col(c));
#define P(a, b) PointF(x + (a) * k, y + (b) * k)
    switch (which) {
    case I_CPU:
        G->DrawRectangle(&pen, x + 4 * k, y + 4 * k, 8 * k, 8 * k);
        G->FillRectangle(&br, x + 6.5f * k, y + 6.5f * k, 3 * k, 3 * k);
        for (int i = 0; i < 3; i++) {
            float p = (5.5f + i * 2.5f) * k;
            G->DrawLine(&pen, x + p, y + 1.5f * k, x + p, y + 3.5f * k);
            G->DrawLine(&pen, x + p, y + 12.5f * k, x + p, y + 14.5f * k);
            G->DrawLine(&pen, x + 1.5f * k, y + p, x + 3.5f * k, y + p);
            G->DrawLine(&pen, x + 12.5f * k, y + p, x + 14.5f * k, y + p);
        }
        break;
    case I_MEM:
        G->DrawRectangle(&pen, x + 1.5f * k, y + 4.5f * k, 13 * k, 6 * k);
        for (int i = 0; i < 4; i++) G->DrawLine(&pen, x + (4 + i * 2.7f) * k, y + 10.5f * k, x + (4 + i * 2.7f) * k, y + 12.5f * k);
        G->DrawLine(&pen, x + 5 * k, y + 7 * k, x + 5 * k, y + 8 * k);
        G->DrawLine(&pen, x + 8 * k, y + 7 * k, x + 8 * k, y + 8 * k);
        G->DrawLine(&pen, x + 11 * k, y + 7 * k, x + 11 * k, y + 8 * k);
        break;
    case I_NRG: {
        PointF pts[] = { P(9, 1.5f), P(3.5f, 9), P(7.5f, 9), P(6.5f, 14.5f), P(12.5f, 6.5f), P(8.5f, 6.5f) };
        G->DrawPolygon(&pen, pts, 6);
        break;
    }
    case I_THM:
        G->DrawLine(&pen, x + 8 * k, y + 2 * k, x + 8 * k, y + 9.5f * k);
        G->DrawEllipse(&pen, x + 5.5f * k, y + 9.5f * k, 5 * k, 5 * k);
        G->DrawArc(&pen, x + 6.2f * k, y + 1.2f * k, 3.6f * k, 3.6f * k, 180, 180);
        G->DrawLine(&pen, x + 6.2f * k, y + 3 * k, x + 6.2f * k, y + 10 * k);
        G->DrawLine(&pen, x + 9.8f * k, y + 3 * k, x + 9.8f * k, y + 10 * k);
        break;
    case I_GPU:
        G->DrawRectangle(&pen, x + 1.5f * k, y + 4 * k, 13 * k, 8 * k);
        G->DrawEllipse(&pen, x + 4 * k, y + 5.5f * k, 5 * k, 5 * k);
        G->DrawLine(&pen, x + 11 * k, y + 6.5f * k, x + 12.5f * k, y + 6.5f * k);
        G->DrawLine(&pen, x + 11 * k, y + 9.5f * k, x + 12.5f * k, y + 9.5f * k);
        break;
    case I_DISK:
        G->DrawRectangle(&pen, x + 2 * k, y + 8.5f * k, 12 * k, 5 * k);
        G->DrawLine(&pen, x + 2 * k, y + 8.5f * k, x + 4.5f * k, y + 3 * k);
        G->DrawLine(&pen, x + 14 * k, y + 8.5f * k, x + 11.5f * k, y + 3 * k);
        G->DrawLine(&pen, x + 4.5f * k, y + 3 * k, x + 11.5f * k, y + 3 * k);
        G->FillEllipse(&br, x + 10.5f * k, y + 10.2f * k, 1.6f * k, 1.6f * k);
        break;
    case I_NET:
        G->DrawArc(&pen, x + 1 * k, y + 3 * k, 14 * k, 14 * k, 225, 90);
        G->DrawArc(&pen, x + 3.5f * k, y + 5.5f * k, 9 * k, 9 * k, 225, 90);
        G->DrawArc(&pen, x + 6 * k, y + 8 * k, 4 * k, 4 * k, 225, 90);
        G->FillEllipse(&br, x + 7 * k, y + 12 * k, 2 * k, 2 * k);
        break;
    case I_WARN: {
        PointF pts[] = { P(8, 1.8f), P(14.8f, 14), P(1.2f, 14) };
        G->DrawPolygon(&pen, pts, 3);
        G->DrawLine(&pen, x + 8 * k, y + 6 * k, x + 8 * k, y + 9.5f * k);
        G->FillEllipse(&br, x + 7.2f * k, y + 11 * k, 1.6f * k, 1.6f * k);
        break;
    }
    case I_GEAR:
        for (int i = 0; i < 3; i++) {
            float yy = (4 + i * 4) * k, kx = (i == 1 ? 5 : i == 0 ? 10 : 8) * k;
            G->DrawLine(&pen, x + 2 * k, y + yy, x + 14 * k, y + yy);
            SolidBrush wb(col(C_CARD));
            G->FillEllipse(&wb, x + kx - 1.8f * k, y + yy - 1.8f * k, 3.6f * k, 3.6f * k);
            G->DrawEllipse(&pen, x + kx - 1.8f * k, y + yy - 1.8f * k, 3.6f * k, 3.6f * k);
        }
        break;
    case I_EXT:
        G->DrawLine(&pen, x + 9 * k, y + 2.5f * k, x + 13.5f * k, y + 2.5f * k);
        G->DrawLine(&pen, x + 13.5f * k, y + 2.5f * k, x + 13.5f * k, y + 7 * k);
        G->DrawLine(&pen, x + 13.5f * k, y + 2.5f * k, x + 7.5f * k, y + 8.5f * k);
        { PointF pts[] = { P(11, 9.5f), P(11, 13.5f), P(2.5f, 13.5f), P(2.5f, 5), P(6.5f, 5) }; G->DrawLines(&pen, pts, 5); }
        break;
    case I_LOCK:
        G->DrawRectangle(&pen, x + 3.5f * k, y + 7 * k, 9 * k, 7 * k);
        G->DrawArc(&pen, x + 5 * k, y + 2.5f * k, 6 * k, 8 * k, 180, 180);
        break;
    case I_CHEV: { PointF pts[] = { P(6, 3.5f), P(10.5f, 8), P(6, 12.5f) }; G->DrawLines(&pen, pts, 3); break; }
    case I_BACK: { PointF pts[] = { P(10, 3.5f), P(5.5f, 8), P(10, 12.5f) }; G->DrawLines(&pen, pts, 3); break; }
    case I_DOWN:
    case I_UP: {
        float a = which == I_DOWN ? 13.0f : 3.0f, b = which == I_DOWN ? 3.0f : 13.0f, d = which == I_DOWN ? -4.0f : 4.0f;
        G->DrawLine(&pen, x + 8 * k, y + b * k, x + 8 * k, y + a * k);
        G->DrawLine(&pen, x + 8 * k, y + a * k, x + 4 * k, y + (a + d) * k);
        G->DrawLine(&pen, x + 8 * k, y + a * k, x + 12 * k, y + (a + d) * k);
        break;
    }
    case I_CLOSE:
        G->DrawLine(&pen, x + 4 * k, y + 4 * k, x + 12 * k, y + 12 * k);
        G->DrawLine(&pen, x + 12 * k, y + 4 * k, x + 4 * k, y + 12 * k);
        break;
    }
#undef P
}
static void badge_icon(int which, float x, float y, int acc) {
    fill_round(x, y, 22, 22, 3, tint(ACC[acc], 13));
    icon(which, x + 3, y + 3, 16, ACC_INK[acc]);
}
static void logo(float x, float y, float s) {
    /* Five vital bars: CPU, energy, memory, thermal, GPU */
    static const UINT32 c[5] = {0xFF10B981, 0xFFF59E0B, 0xFF0EA5E9, 0xFFF43F5E, 0xFF14B8A6};
    static const float f[5] = {0.52f, 0.79f, 0.64f, 1.0f, 0.73f};
    card(x, y, s, s, C_CARD, C_CARD_B);
    float bw = s * 0.11f, gap = s * 0.06f, tall = s * 0.56f, bottom = y + s * 0.78f;
    float x0 = x + (s - 5 * bw - 4 * gap) / 2;
    for (int i = 0; i < 5; i++)
        fill_round(x0 + i * (bw + gap), bottom - tall * f[i], bw, tall * f[i], bw / 2, c[i]);
}

/* ---- pills, buttons, bars -------------------------------------------------- */
/* XP push button (uxtheme BP_PUSHBUTTON when a visual style is active) */
static void push_button(float x, float y, float w, float h, int hot, int danger) {
    if (g_th_button && !danger) {
        RECT r;
        float px = RTL ? WIN_W - x - w : x;
        r.left = (LONG)px; r.top = (LONG)(y + DY); r.right = (LONG)(px + w); r.bottom = (LONG)(y + DY + h);
        HDC dc = G->GetHDC();
        pDrawThemeBackground(g_th_button, dc, 1 /*BP_PUSHBUTTON*/, hot ? 2 : 1, &r, 0);
        G->ReleaseHDC(dc);
        return;
    }
    GraphicsPath p;
    round_path(p, x, y, w, h, 3);
    if (danger) {
        LinearGradientBrush b(PointF(0, y), PointF(0, y + h), col(0xFFE4553F), col(0xFFC22C17));
        G->FillPath(&b, &p);
    } else {
        LinearGradientBrush b(PointF(0, y), PointF(0, y + h), col(0xFFFFFFFF), col(0xFFE3E0D6));
        G->FillPath(&b, &p);
    }
    stroke_round(x, y, w, h, 3, danger ? 0xFF8E1B0C : 0xFF003C74);
    if (hot && !danger) stroke_round(x + 1, y + 1, w - 2, h - 2, 2, 0xFFF8B330);
}
static void bar(float x, float y, float w, float h, double frac, UINT32 c) {
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    fill_round(x, y, w, h, h / 2, C_TRACK);
    float fw = (float)(w * frac);
    if (fw < h) fw = h;
    fill_round(x, y, fw, h, h / 2, c);
}

/* ---- Catmull-Rom curves ------------------------------------------------------ */
static int bezier_points(const PointF *p, int n, PointF *out) {
    int m = 0;
    out[m++] = p[0];
    for (int i = 0; i < n - 1; i++) {
        PointF p0 = p[i > 0 ? i - 1 : i], p1 = p[i], p2 = p[i + 1], p3 = p[i + 2 < n ? i + 2 : i + 1];
        out[m++] = PointF(p1.X + (p2.X - p0.X) / 6, p1.Y + (p2.Y - p0.Y) / 6);
        out[m++] = PointF(p2.X - (p3.X - p1.X) / 6, p2.Y - (p3.Y - p1.Y) / 6);
        out[m++] = p2;
    }
    return m;
}
/* Draws a curve with a gradient fill down to baseline yb. */
static void curve(const PointF *pts, int n, float yb, UINT32 c, float top_alpha, float y_top, float y_bot) {
    static PointF bz[3 * HIST_N + 4];
    if (n < 2) return;
    int m = bezier_points(pts, n, bz);
    GraphicsPath area;
    area.AddBeziers(bz, m);
    area.AddLine(pts[n - 1].X, pts[n - 1].Y, pts[n - 1].X, yb);
    area.AddLine(pts[n - 1].X, yb, pts[0].X, yb);
    area.CloseFigure();
    LinearGradientBrush gb(PointF(0, y_top - 0.5f), PointF(0, y_bot + 0.5f), col(tint(c, (int)(top_alpha * 100))), col(tint(c, 0)));
    G->FillPath(&gb, &area);
    GraphicsPath ln;
    ln.AddBeziers(bz, m);
    Pen pen(col(c), 1.5f);
    pen.SetLineJoin(LineJoinRound);
    G->DrawPath(&pen, &ln);
}
static void sparkline(const float *v, int n, float x, float y, float w, float h, UINT32 c) {
    PointF pts[SPARK_N];
    if (n < 2) { line(x, y + h - 2, x + w, y + h - 2, tint(c, 40), 1); return; }
    float mn = v[0], mx = v[0];
    for (int i = 0; i < n; i++) { if (v[i] < mn) mn = v[i]; if (v[i] > mx) mx = v[i]; }
    float rg = mx - mn < 0.5f ? 0.5f : mx - mn, lo = mn - rg * 0.25f, hi = mx + rg * 0.35f;
    for (int i = 0; i < n; i++) {
        int slot = SPARK_N - n + i;
        pts[i] = PointF(x + slot * w / (SPARK_N - 1), y + h - 2 - (v[i] - lo) / (hi - lo) * (h - 4));
    }
    curve(pts, n, y + h, c, 0.28f, y, y + h);
}
static void spark_tag(float x, float y) {
    HFONT f = font(FF_UI, 10, 0);
    const WCHAR *s = tr(S_min1);
    float w = (float)measure(s, f) + 8;
    fill_round(x, y, w, 13, 2, tint(C_TRACK, 92));
    text(s, x + 4, y, w - 8, 13, f, C_INK3, T_START);
}

/* ------------------------------------------------------------------------ */
/* Data helpers                                                             */
/* ------------------------------------------------------------------------ */
static int thermal_level(double c) { return c < 48 ? 0 : c < 70 ? 1 : c < 90 ? 2 : 3; }
static const int THERMAL_KEYS[] = { S_cool, S_warm, S_hot, S_throttled };
static const int THERMAL_ACC[] = { A_MEM, A_NRG, A_THM, A_CRIT };

static AppGroup *hog_app(void) {
    for (int k = 0; k < g_tel.ngroups; k++) if (g_tel.groups[k].hog) return &g_tel.groups[k];
    return 0;
}
static AppGroup *find_group(const WCHAR *name) {
    for (int k = 0; k < g_tel.ngroups; k++) if (!_wcsicmp(g_tel.groups[k].name, name)) return &g_tel.groups[k];
    return 0;
}
static AppGroup *g_sorted[8];
static int g_nsorted;
static double metric_of(const AppGroup *a) { return g_sort == 0 ? a->cpu : g_sort == 1 ? a->mem_mb : a->gpu; }
static void sort_apps(void) {
    g_nsorted = 0;
    for (int k = 0; k < g_tel.ngroups; k++) {
        AppGroup *a = &g_tel.groups[k];
        if (!_wcsicmp(a->name, L"System Idle Process") || !_wcsicmp(a->name, L"[System Process]")) continue;
        int pos = g_nsorted < 5 ? g_nsorted : 5;
        while (pos > 0 && metric_of(a) > metric_of(g_sorted[pos - 1])) pos--;
        if (pos >= 5) continue;
        for (int j = (g_nsorted < 5 ? g_nsorted : 4); j > pos; j--) g_sorted[j] = g_sorted[j - 1];
        g_sorted[pos] = a;
        if (g_nsorted < 5) g_nsorted++;
    }
}
static void show_toast(const WCHAR *s) {
    lstrcpynW(g_toast, s, 256);
    SetTimer(g_hwnd, TIMER_TOAST, 2800, 0);
}

/* ------------------------------------------------------------------------ */
/* Sections                                                                 */
/* ------------------------------------------------------------------------ */
static void draw_titlebar(void) {
    float savedDY = DY;
    DY = 0;
    set_xform();
    /* Luna caption gradient */
    GraphicsPath p;
    p.AddArc(0.0f, 0.0f, 16.0f, 16.0f, 180, 90);
    p.AddArc((float)WIN_W - 16, 0.0f, 16.0f, 16.0f, 270, 90);
    p.AddLine((float)WIN_W, (float)TB, 0.0f, (float)TB);
    p.CloseFigure();
    LinearGradientBrush b(PointF(0, 0), PointF(0, (REAL)TB), col(0xFF0997FF), col(0xFF003DD7));
    Color cs[] = { col(0xFF0997FF), col(0xFF0053EE), col(0xFF0050EE), col(0xFF0046E0), col(0xFF003DD7) };
    REAL ps[] = { 0.0f, 0.12f, 0.5f, 0.85f, 1.0f };
    b.SetInterpolationColors(cs, ps, 5);
    G->FillPath(&b, &p);
    line(4, 1.5f, WIN_W - 4, 1.5f, 0x803D95FF, 1);
    HFONT f = font(FF_CAPTION, 13, 1);
    WCHAR *title = tr(S_winTitle);
    text(title, 9, 1, WIN_W - 50, TB, f, 0xFF0A1E8C, T_START);
    text(title, 8, 0, WIN_W - 50, TB, f, 0xFFFFFFFF, T_START);
    /* close button */
    float cx = WIN_W - 5 - 21, cy = 4;
    int hot = hovered(X_CLOSE, 0);
    if (g_th_window) {
        RECT r;
        float px = RTL ? WIN_W - cx - 21 : cx;
        r.left = (LONG)px; r.top = (LONG)cy; r.right = (LONG)px + 21; r.bottom = (LONG)cy + 21;
        HDC dc = G->GetHDC();
        pDrawThemeBackground(g_th_window, dc, 18 /*WP_CLOSEBUTTON*/, hot ? 2 : 1, &r, 0);
        G->ReleaseHDC(dc);
    } else {
        GraphicsPath cp;
        round_path(cp, cx, cy, 21, 21, 3);
        LinearGradientBrush cb(PointF(0, cy), PointF(0, cy + 21), col(hot ? 0xFFF39A7D : 0xFFE37B5B), col(hot ? 0xFFD9542F : 0xFFC93D1B));
        G->FillPath(&cb, &cp);
        stroke_round(cx, cy, 21, 21, 3, 0xFFFFFFFF);
        Pen x(col(0xFFFFFFFF), 2);
        G->DrawLine(&x, cx + 6.5f, cy + 6.5f, cx + 14.5f, cy + 14.5f);
        G->DrawLine(&x, cx + 14.5f, cy + 6.5f, cx + 6.5f, cy + 14.5f);
    }
    add_hit(cx, cy, 21, 21, X_CLOSE, 0, 0);
    DY = savedDY;
    set_xform();
}

static void draw_header(float y) {
    logo(X0, y, 30);
    HFONT fb = font(FF_HERO, 15, 1);
    const WCHAR *name = L"Pulse";
    float nw = (float)measure(name, fb);
    text(name, X0 + 38, y, nw + 2, 30, fb, C_INK, T_START | T_LTR);
    /* status pill */
    AppGroup *h = hog_app();
    int hog = h && _wcsicmp(g_dismissed, h->name);
    WCHAR *s = hog ? tr2(S_pillValue, "label", tr(S_hogPill), "value", pct(h->cpu, 0))
                   : tr2(S_pillValue, "label", tr(S_calm), "value", cat2(num(g_tel.watts, 1), L"W"));
    float px = X0 + 38 + nw + 10;
    float maxw = X0 + CWID - 108 - px;
    HFONT fp = font(FF_UI, 11, 1);
    float pw = (float)measure(s, fp) + 22;
    if (pw > maxw) pw = maxw;
    fill_round(px, y + 5, pw, 20, 3, tint(hog ? C_WARN : C_CPU, hog ? 16 : 12));
    { SolidBrush b(col(hog ? C_WARN : C_CPU)); G->FillEllipse(&b, px + 7, y + 12, 6.0f, 6.0f); }
    text(s, px + 16, y + 5, pw - 20, 20, fp, hog ? C_WARN_INK : C_CPU_INK, T_START);
    /* settings gear */
    float gx = X0 + CWID - 28;
    int gh = hovered(X_SETTINGS, 0), on = g_view == V_SETTINGS;
    card(gx, y + 1, 28, 28, gh || on ? C_CARD_HOV : C_CARD, on ? tint(C_MEM, 60) : C_CARD_B);
    icon(I_GEAR, gx + 6, y + 7, 16, on ? C_MEM_INK : C_INK2);
    add_hit(gx, y + 1, 28, 28, X_SETTINGS, 0, 1);
    /* language switcher: 2 locales = pill, 3+ = menu */
    int n = i18n_count();
    if (n <= 2) {
        float lw = 64, lx = gx - 6 - lw;
        fill_round(lx, y + 1, lw, 28, 3, C_TRACK);
        stroke_round(lx, y + 1, lw, 28, 3, C_CARD_B);
        for (int k = 0; k < n; k++) {
            Locale *o = i18n_locale(k);
            float sx = lx + 2 + k * 30;
            int sel = k == g_lang;
            if (sel) { fill_round(sx, y + 3, 30, 24, 3, C_CARD); stroke_round(sx, y + 3, 30, 24, 3, C_SEG_B); }
            else if (hovered(X_LANG, k)) fill_round(sx, y + 3, 30, 24, 3, C_HOVER);
            HFONT lf = font(FF_UI, o->rtl ? 14 : 12, sel);
            text((const WCHAR *)o->label, sx, y + 3, 30, 24, lf, sel ? C_INK : C_INK2, T_CENTER);
            add_hit(sx, y + 3, 30, 24, X_LANG, k, 1);
        }
    } else {
        float lw = 60, lx = gx - 6 - lw;
        int hot = hovered(X_LANGMENU, 0);
        card(lx, y + 1, lw, 28, hot ? C_CARD_HOV : C_CARD, C_SEG_B);
        text((const WCHAR *)L->label, lx + 8, y + 1, lw - 26, 28, font(FF_UI, 12, 1), C_INK, T_START);
        Pen pen(col(C_INK2), 1.4f);
        PointF v[] = { PointF(lx + lw - 16, y + 12), PointF(lx + lw - 12, y + 17), PointF(lx + lw - 8, y + 12) };
        G->DrawLines(&pen, v, 3);
        add_hit(lx, y + 1, lw, 28, X_LANGMENU, 0, 1);
    }
}

/* the slot under the header: confirm card, toast, or hog banner */
static float draw_slot(float y) {
    if (g_confirm[0]) {
        float h = 64;
        card(X0, y, CWID, h, C_CARD, tint(C_CRIT, 50));
        HFONT fb = font(FF_UI, 12, 1), fs = font(FF_UI, 11, 0);
        text(tr1(S_confirmTitle, "app", app_name(g_confirm)), X0 + 12, y + 8, CWID - 24, 16, fb, C_INK, T_START);
        text(tr(S_confirmSub), X0 + 12, y + 24, CWID - 24, 14, fs, C_INK2, T_START);
        WCHAR *qb = tr(S_confirmBtn), *cb = tr(S_cancel);
        float qw = (float)measure(qb, fb) + 22, cw = (float)measure(cb, fb) + 22;
        float qx = X0 + CWID - 10 - qw, cx = qx - 8 - cw;
        push_button(cx, y + 38, cw, 22, hovered(X_CONFIRM_NO, 0), 0);
        text(cb, cx, y + 38, cw, 22, fb, C_INK, T_CENTER);
        push_button(qx, y + 38, qw, 22, hovered(X_CONFIRM_YES, 0), 1);
        text(qb, qx, y + 38, qw, 22, fb, 0xFFFFFFFF, T_CENTER);
        add_hit(cx, y + 38, cw, 22, X_CONFIRM_NO, 0, 1);
        add_hit(qx, y + 38, qw, 22, X_CONFIRM_YES, 0, 1);
        return y + h + 8;
    }
    if (g_toast[0]) {
        float h = 34;
        card(X0, y, CWID, h, C_CARD, tint(C_CPU, 50));
        SolidBrush b(col(C_CPU));
        G->FillEllipse(&b, X0 + 12, y + 14, 7.0f, 7.0f);
        text(g_toast, X0 + 26, y, CWID - 36, h, font(FF_UI, 12, 1), C_INK, T_START);
        return y + h + 8;
    }
    AppGroup *h = hog_app();
    if (g_view == V_NONE && h && _wcsicmp(g_dismissed, h->name)) {
        float bh = 44;
        LinearGradientBrush bg(PointF(RTL ? (REAL)(X0 + CWID) : (REAL)X0, 0), PointF(RTL ? (REAL)X0 : (REAL)(X0 + CWID), 0), col(tint(C_WARN, 13)), col(tint(C_CRIT, 8)));
        GraphicsPath p;
        round_path(p, X0, y, CWID, bh, 3);
        SolidBrush base(col(C_CARD));
        G->FillPath(&base, &p);
        G->FillPath(&bg, &p);
        stroke_round(X0, y, CWID, bh, 3, tint(C_WARN, 45));
        fill_round(X0 + 8, y + 9, 26, 26, 3, tint(C_WARN, 18));
        icon(I_WARN, X0 + 13, y + 14, 16, C_WARN_INK);
        HFONT fb = font(FF_UI, 12, 1), fs = font(FF_UI, 11, 0);
        WCHAR *eb = tr(S_endApp);
        float ew = (float)measure(eb, fb) + 20;
        float ex = X0 + CWID - 36 - ew;
        text(tr2(S_hogTitle, "app", app_name(h->name), "pct", pct(h->cpu, 0)), X0 + 42, y + 6, ex - X0 - 48, 17, fb, C_INK, T_START);
        text(tr1(S_hogSub, "pct", pct(50, 0)), X0 + 42, y + 23, ex - X0 - 48, 14, fs, C_INK2, T_START);
        int eh = hovered(X_HOG_END, 0);
        card(ex, y + 9, ew, 26, eh ? 0xFFFFF4EC : tint(C_WARN, 10), tint(C_WARN, 60));
        text(eb, ex, y + 9, ew, 26, fb, C_WARN_INK, T_CENTER);
        add_hit(ex, y + 9, ew, 26, X_HOG_END, 0, 1);
        int dh = hovered(X_HOG_DISMISS, 0);
        if (dh) fill_round(X0 + CWID - 30, y + 11, 22, 22, 3, tint(C_INK, 8));
        icon(I_CLOSE, X0 + CWID - 27, y + 14, 16, C_INK2);
        add_hit(X0 + CWID - 30, y + 11, 22, 22, X_HOG_DISMISS, 0, 1);
        return y + bh + 8;
    }
    return y;
}

static void card_header(float x, float y, float w, int ic, int acc, int title_key, int view) {
    badge_icon(ic, x + 10, y + 10, acc);
    text(tr(title_key), x + 38, y + 10, w - 80, 22, font(FF_UI, 12, 1), C_INK, T_START);
    icon(I_CHEV, x + w - 24, y + 13, 16, C_INK2);
    (void)view;
}
static int card_box(float x, float y, float w, float h, int view, UINT32 border) {
    int hot = hovered(X_CARD, view);
    card(x, y, w, h, hot ? C_CARD_HOV : C_CARD, border);
    return add_hit(x, y, w, h, X_CARD, view, 1);
}

static void draw_cpu_card(float x, float y, float w, float h) {
    int high = g_tel.cpu >= 50;
    card_box(x, y, w, h, V_CPU, high ? tint(C_WARN, 60) : C_CARD_B);
    card_header(x, y, w, I_CPU, A_CPU, S_cpu, V_CPU);
    if (high) {
        HFONT f = font(FF_UI, 11, 1);
        WCHAR *s = tr(S_high);
        float pw = (float)measure(s, f) + 12;
        fill_round(x + w - 28 - pw, y + 12, pw, 18, 3, tint(C_WARN, 16));
        text(s, x + w - 28 - pw, y + 12, pw, 18, f, C_WARN_INK, T_CENTER);
    }
    text(pct(g_tel.cpu, 0), x + 10, y + 36, w - 20, 32, font(FF_HERO, 26, 1), high ? C_WARN_INK : C_INK, T_START | T_LTR);
    text(tr2(S_userSys, "user", pct(g_tel.user, 0), "sys", pct(g_tel.sys, 0)), x + 10, y + 70, w - 20, 14, font(FF_UI, 11, 0), C_INK2, T_START);
    WCHAR *labels[3] = { hw1(H_coreN, "n", num(0, 0)), hw1(H_coreN, "n", num(1, 0)), tr(S_gpu) };
    double vals[3] = { g_tel.core[0], g_tel.core[1], g_tel.gpu_pct };
    UINT32 cols[3] = { C_CPU, tint(C_CPU, 60), C_GPU };
    for (int i = 0; i < 3; i++) {
        float ry = y + 88 + i * 14;
        text(labels[i], x + 10, ry, 70, 13, font(FF_UI, 11, 0), C_INK2, T_START);
        bar(x + w - 10 - 36 - 64, ry + 4, 60, 5, vals[i] / 100, cols[i]);
        text(pct(vals[i], 0), x + w - 10 - 36, ry, 36, 13, font(FF_UI, 11, 1), C_INK, T_END | T_LTR);
    }
    sparkline(g_tel.sp_cpu, g_tel.sp_n, x + 10, y + h - 30, w - 20, 24, high ? C_WARN : C_CPU);
    spark_tag(x + 10, y + h - 33);
}

/* 0 normal, 1 high, 2 critical: physical memory in use and commit charge */
static int mem_pressure_level(void) {
    Telemetry *t = &g_tel;
    double commit = t->commit_limit_gb > 0 ? 100.0 * t->commit_gb / t->commit_limit_gb : 0;
    double p = t->mem_pct > commit ? t->mem_pct : commit;
    return p >= 95 ? 2 : p >= 85 ? 1 : 0;
}
static void draw_mem_card(float x, float y, float w, float h) {
    Telemetry *t = &g_tel;
    card_box(x, y, w, h, V_MEM, C_CARD_B);
    card_header(x, y, w, I_MEM, A_MEM, S_mem, V_MEM);
    HFONT hf = font(FF_HERO, 26, 1);
    WCHAR *hero = pct(t->mem_pct, 0);
    float hw_ = (float)measure(hero, hf);
    text(hero, x + 10, y + 36, hw_ + 2, 32, hf, C_INK, T_START | T_LTR);
    {
        int lv = mem_pressure_level();
        static const int LONGK[3] = { S_pressure, S_pressureHigh, S_pressureCritical };
        static const int SHORTK[3] = { S_pressureShort, S_pressureHighShort, S_pressureCriticalShort };
        int acc = lv == 0 ? A_CPU : lv == 1 ? A_WARN : A_CRIT;
        float bx = x + 10 + hw_ + 8, bw = x + w - 10 - bx;
        HFONT f = font(FF_UI, 11, 1);
        WCHAR *s = tr(LONGK[lv]);
        float pw = (float)measure(s, f) + 22;
        if (pw > bw) { s = tr(SHORTK[lv]); pw = (float)measure(s, f) + 22; }
        if (pw > bw) pw = bw;
        if (pw > 40) {
            fill_round(bx, y + 43, pw, 18, 3, tint(ACC[acc], 14));
            SolidBrush b(col(ACC[acc])); G->FillEllipse(&b, bx + 7, y + 49, 6.0f, 6.0f);
            text(s, bx + 16, y + 43, pw - 20, 18, f, ACC_INK[acc], T_START);
        }
    }
    text(tr2(S_memOf, "used", gb(t->mem_used_gb, 2), "total", gb(t->mem_total_gb, t->mem_total_gb < 10 ? 1 : 0)), x + 10, y + 70, w - 20, 14, font(FF_UI, 11, 0), C_INK2, T_START);
    double tot = t->mem_total_gb > 0 ? t->mem_total_gb : 1;
    double app = t->mem_used_gb - t->mem_kernel_gb - t->mem_cache_gb;
    if (app < 0) app = 0;
    float bx = x + 10, bw = w - 20, by = y + 88;
    fill_round(bx, by, bw, 8, 2, C_TRACK);
    float w1 = (float)(bw * app / tot), w2 = (float)(bw * t->mem_kernel_gb / tot), w3 = (float)(bw * t->mem_cache_gb / tot);
    { SolidBrush b(col(C_MEM)); G->FillRectangle(&b, bx, by, w1, 8.0f); }
    { SolidBrush b(col(tint(C_MEM, 55))); G->FillRectangle(&b, bx + w1 + 1, by, w2 > 1 ? w2 - 1 : 0, 8.0f); }
    { HatchBrush b(HatchStyleWideUpwardDiagonal, col(C_MEM), col(tint(C_MEM, 18))); G->FillRectangle(&b, bx + w1 + w2 + 1, by, w3 > 1 ? w3 - 1 : 0, 8.0f); }
    struct { const WCHAR *k; double v; int sw; } lg[4] = {
        { tr(S_segApp), app, 0 }, { hw(H_kernel), t->mem_kernel_gb, 1 }, { hw(H_cache), t->mem_cache_gb, 2 }, { tr(S_segFree), t->mem_free_gb, 3 } };
    HFONT lf = font(FF_UI, 11, 0);
    for (int i = 0; i < 4; i++) {
        float lx = x + 10 + (i % 2) * (w - 20) / 2, ly = y + 101 + (i / 2) * 14;
        UINT32 c = i == 0 ? C_MEM : i == 1 ? tint(C_MEM, 55) : i == 2 ? C_MEM : 0xFFA09C8C;
        if (i == 2) { HatchBrush b(HatchStyleWideUpwardDiagonal, col(C_MEM), col(0xFFFFFFFF)); G->FillRectangle(&b, lx, ly + 3, 7.0f, 7.0f); }
        else fill_round(lx, ly + 3, 7, 7, 1, c);
        text(cat3(lg[i].k, L" ", iso(gb(lg[i].v, 2))), lx + 11, ly, (w - 20) / 2 - 12, 13, lf, C_INK2, T_START);
    }
    sparkline(t->sp_mem, t->sp_n, x + 10, y + h - 30, w - 20, 24, C_MEM);
    spark_tag(x + 10, y + h - 33);
}

static void draw_nrg_card(float x, float y, float w, float h) {
    Telemetry *t = &g_tel;
    card_box(x, y, w, h, V_NRG, C_CARD_B);
    card_header(x, y, w, I_NRG, A_NRG, S_nrg, V_NRG);
    if (t->no_batt) {
        /* desktop: no battery, the draw is an estimate from CPU load */
        int fs = 20;
        while (fs > 13 && measure(tr(S_noBattery), font(FF_HERO, fs, 1)) > w - 20) fs--;
        text(tr(S_noBattery), x + 10, y + 36, w - 20, 32, font(FF_HERO, fs, 1), C_INK, T_START);
        text(tr(S_acPower), x + 10, y + 70, w - 20, 14, font(FF_UI, 11, 0), C_INK2, T_START);
        WCHAR *flow = watts(t->watts);
        HFONT fb = font(FF_UI, 12, 1);
        float pw = (float)measure(flow, fb) + 24;
        fill_round(x + 10, y + 87, pw, 22, 3, tint(C_NRG, 13));
        stroke_round(x + 10, y + 87, pw, 22, 3, tint(C_NRG, 45));
        { SolidBrush b(col(C_NRG)); G->FillEllipse(&b, x + 17, y + 95, 6.0f, 6.0f); }
        text(flow, x + 26, y + 87, pw - 20, 22, fb, C_NRG_INK, T_START | T_LTR);
        text(tr(S_tDraw), x + 18 + pw, y + 87, w - pw - 28, 22, font(FF_UI, 11, 0), C_INK2, T_START);
        sparkline(t->sp_nrg, t->sp_n, x + 10, y + h - 30, w - 20, 24, C_NRG);
        spark_tag(x + 10, y + h - 33);
        return;
    }
    text(pct(t->batt_pct, 0), x + 10, y + 36, w - 20, 32, font(FF_HERO, 26, 1), C_INK, T_START | T_LTR);
    WCHAR *sub;
    if (t->charging) sub = tr1(S_fullIn, "time", dur(0, 52));
    else if (t->batt_min_left >= 0) sub = tr1(S_timeLeft, "time", dur(t->batt_min_left / 60, t->batt_min_left % 60));
    else sub = (WCHAR *)DASH;
    text(sub, x + 10, y + 70, w - 20, 14, font(FF_UI, 11, 0), C_INK2, T_START);
    WCHAR *flow = t->charging ? cat2(L"+", watts(t->batt_sim ? 48 : t->watts)) : cat2(L"\x2212", watts(t->watts));
    HFONT fb = font(FF_UI, 12, 1);
    float pw = (float)measure(flow, fb) + 24;
    fill_round(x + 10, y + 87, pw, 22, 3, tint(C_NRG, 13));
    stroke_round(x + 10, y + 87, pw, 22, 3, tint(C_NRG, 45));
    { SolidBrush b(col(C_NRG)); G->FillEllipse(&b, x + 17, y + 95, 6.0f, 6.0f); }
    text(flow, x + 26, y + 87, pw - 20, 22, fb, C_NRG_INK, T_START | T_LTR);
    text(tr(t->charging ? S_charging : S_onBatt), x + 18 + pw, y + 87, w - pw - 28, 22, font(FF_UI, 11, 0), C_INK2, T_START);
    if (t->batt_sim)
        text(tr2(S_health, "pct", pct(95, 0), "cycles", plural(P_cycle, 118)), x + 10, y + 113, w - 20, 13, font(FF_UI, 11, 0), C_INK2, T_START);
    sparkline(t->sp_nrg, t->sp_n, x + 10, y + h - 30, w - 20, 24, C_NRG);
    spark_tag(x + 10, y + h - 33);
}

static void draw_thm_card(float x, float y, float w, float h) {
    Telemetry *t = &g_tel;
    int lv = thermal_level(t->temp_c);
    card_box(x, y, w, h, V_THM, C_CARD_B);
    card_header(x, y, w, I_THM, A_THM, S_thm, V_THM);
    text(tr(THERMAL_KEYS[lv]), x + 10, y + 36, w - 20, 32, font(FF_HERO, 26, 1), C_INK, T_START);
    text(tr2(S_tempLine, "cpu", temp(t->temp_c, 0), "gpu", temp(t->gpu_temp_c, 0)), x + 10, y + 70, w - 20, 14, font(FF_UI, 11, 0), C_INK2, T_START);
    float sw = (w - 20 - 12) / 4;
    for (int i = 0; i < 4; i++) {
        float sx = x + 10 + i * (sw + 4);
        fill_round(sx, y + 90, sw, 5, 2.5f, i == lv ? ACC[THERMAL_ACC[lv]] : C_TRACK);
        text(tr(THERMAL_KEYS[i]), sx - 2, y + 98, sw + 4, 14, font(FF_UI, 10, i == lv), i == lv ? ACC_INK[THERMAL_ACC[lv]] : C_INK3, T_CENTER);
    }
    text(tr(lv < 3 ? S_zero : S_throttling), x + 10, y + 113, w - 20, 13, font(FF_UI, 11, 0), lv < 3 ? C_INK3 : C_CRIT_INK, T_START);
    sparkline(t->sp_thm, t->sp_n, x + 10, y + h - 30, w - 20, 24, C_THM);
    spark_tag(x + 10, y + h - 33);
}

static void draw_gpu_strip(float y) {
    Telemetry *t = &g_tel;
    float h = 32;
    card_box(X0, y, CWID, h, V_GPU, C_CARD_B);
    fill_round(X0 + 8, y + 6, 20, 20, 3, tint(C_GPU, 13));
    icon(I_GPU, X0 + 10, y + 8, 16, C_GPU_INK);
    HFONT fb = font(FF_UI, 12, 1);
    WCHAR *g = tr(S_gpu);
    float gw = (float)measure(g, fb);
    text(g, X0 + 36, y, gw + 2, h, fb, C_INK, T_START);
    int lvl = t->gpu_temp_c < 60 ? A_MEM : t->gpu_temp_c < 80 ? A_NRG : A_THM;
    WCHAR *tt = temp(t->gpu_temp_c, 0);
    HFONT f11 = font(FF_UI, 11, 1);
    float tw = (float)measure(tt, f11) + 12;
    float tx = X0 + CWID - 8 - tw;
    fill_round(tx, y + 7, tw, 18, 3, tint(ACC[lvl], 13));
    text(tt, tx, y + 7, tw, 18, f11, ACC_INK[lvl], T_CENTER | T_LTR);
    text(pct(t->gpu_pct, 0), tx - 46, y, 40, h, fb, C_INK, T_END | T_LTR);
    bar(tx - 46 - 88, y + 14, 84, 5, t->gpu_pct / 100, C_GPU);
    text(t->gpu_name[0] ? t->gpu_name : DASH, X0 + 42 + gw, y, tx - 46 - 94 - (X0 + 42 + gw), h, font(FF_UI, 11, 0), C_INK2, T_START | T_LTR);
}

static void draw_strips(float y) {
    Telemetry *t = &g_tel;
    float h = 56;
    /* storage */
    card_box(X0, y, HALF, h, V_SSD, C_CARD_B);
    fill_round(X0 + 8, y + 7, 20, 20, 3, tint(C_CPU, 12));
    icon(I_DISK, X0 + 10, y + 9, 16, C_CPU_INK);
    text(hw1(H_localDisk, "drive", t->drive), X0 + 34, y + 7, HALF - 42, 20, font(FF_UI, 11, 1), C_INK, T_START);
    {
        WCHAR *fr = tr1(S_free, "value", gb(t->disk_free_gb, 0));
        float fw = (float)measure(fr, font(FF_UI, 11, 0)) + 2;
        if (fw > (HALF - 20) * 0.5f) fw = (HALF - 20) * 0.5f;
        text(tr1(S_used, "pct", pct(t->disk_pct, 0)), X0 + 10, y + 28, HALF - 26 - fw, 15, font(FF_UI, 12, 1), C_INK, T_START);
        text(fr, X0 + HALF - 10 - fw, y + 28, fw, 15, font(FF_UI, 11, 0), C_INK2, T_END);
    }
    {
        float bx = X0 + 10, bw = HALF - 20, by = y + 46;
        fill_round(bx, by, bw, 5, 2.5f, C_TRACK);
        float fw = (float)(bw * t->disk_pct / 100);
        if (fw > 5) {
            GraphicsPath p;
            round_path(p, bx, by, fw, 5, 2.5f);
            LinearGradientBrush b(PointF(bx - 1, 0), PointF(bx + fw + 1, 0), col(C_CPU), col(C_NRG));
            G->FillPath(&b, &p);
        }
    }
    /* network */
    float nx = X0 + HALF + GAP;
    card_box(nx, y, HALF, h, V_NET, C_CARD_B);
    fill_round(nx + 8, y + 7, 20, 20, 3, tint(C_MEM, 12));
    icon(I_NET, nx + 10, y + 9, 16, C_MEM_INK);
    text(t->net_ok ? hw(H_lanConnection) : DASH, nx + 34, y + 7, HALF - 42, 20, font(FF_UI, 11, 1), C_INK, T_START);
    HFONT fb = font(FF_UI, 12, 1);
    WCHAR *dn = down_s(t->net_down_kbs), *up = up_s(t->net_up_kbs);
    float dw = (float)measure(dn, fb);
    icon(I_DOWN, nx + 8, y + 30, 14, C_MEM_INK);
    text(dn, nx + 22, y + 29, dw + 2, 16, fb, C_MEM_INK, T_START | T_LTR);
    icon(I_UP, nx + 32 + dw, y + 30, 14, C_CPU_INK);
    text(up, nx + 46 + dw, y + 29, HALF - 56 - dw, 16, fb, C_CPU_INK, T_START | T_LTR);
}

static void mono_colors(const AppGroup *a, UINT32 *bg, UINT32 *ink) {
    unsigned h = 2166136261u;
    for (const WCHAR *p = a->name; *p; p++) { h ^= (unsigned)*p; h *= 16777619u; }
    float hue = (float)(h % 360) / 360.0f, s = 0.55f, l = a->system ? 0.92f : 0.84f;
    float q = l < 0.5f ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
    float rgb[3];
    for (int i = 0; i < 3; i++) {
        float t = hue + (1 - i) / 3.0f;
        if (t < 0) t += 1;
        if (t > 1) t -= 1;
        rgb[i] = t < 1 / 6.0f ? p + (q - p) * 6 * t : t < 0.5f ? q : t < 2 / 3.0f ? p + (q - p) * (2 / 3.0f - t) * 6 : p;
    }
    *bg = 0xFF000000 | ((UINT32)(rgb[0] * 255) << 16) | ((UINT32)(rgb[1] * 255) << 8) | (UINT32)(rgb[2] * 255);
    *ink = 0xFF000000 | ((UINT32)(rgb[0] * 95) << 16) | ((UINT32)(rgb[1] * 95) << 8) | (UINT32)(rgb[2] * 95);
}

static float g_rows_y;
static void draw_apps(float y, float *out_h) {
    sort_apps();
    float h = 40 + g_nsorted * ROW_H + 6;
    card(X0, y, CWID, h, C_CARD, C_CARD_B);
    /* sort tabs, each as wide as its label */
    int keys[3] = { S_cpu, S_mem, S_gpu };
    float segw[3], total = 4;
    for (int i = 0; i < 3; i++) { segw[i] = (float)measure(tr(keys[i]), font(FF_UI, 11, 1)) + 18; if (segw[i] < 44) segw[i] = 44; total += segw[i]; }
    float sx0 = X0 + CWID - 10 - total;
    fill_round(sx0, y + 8, total, 26, 3, C_TRACK);
    text(tr(S_top), X0 + 12, y + 8, sx0 - X0 - 18, 26, font(FF_UI, 12, 1), C_INK, T_START);
    float sxi = sx0 + 2;
    for (int i = 0; i < 3; i++) {
        float sx = sxi, sw = segw[i];
        sxi += sw;
        int sel = g_sort == i;
        if (sel) { fill_round(sx, y + 10, sw, 22, 3, C_CARD); stroke_round(sx, y + 10, sw, 22, 3, C_SEG_B); }
        else if (hovered(X_SORT, i)) fill_round(sx, y + 10, sw, 22, 3, C_HOVER);
        text(tr(keys[i]), sx + 2, y + 10, sw - 4, 22, font(FF_UI, 11, 1), sel ? C_INK : C_INK2, T_CENTER);
        add_hit(sx, y + 10, sw, 22, X_SORT, i, 1);
    }
    g_rows_y = y + 40;
    double mx = g_nsorted ? metric_of(g_sorted[0]) : 1;
    if (mx <= 0) mx = 1;
    AppGroup *hog = hog_app();
    for (int i = 0; i < g_nsorted; i++) {
        AppGroup *a = g_sorted[i];
        float ry = y + 40 + i * ROW_H, rx = X0 + 6, rw = CWID - 12;
        int hot = g_hover_row == i;
        int hogrow = a == hog && g_sort == 0;
        if (hot) fill_round(rx, ry, rw, ROW_H, 2, C_HOVER);
        add_hit(rx, ry, rw, ROW_H, X_ROW, i, 1);
        UINT32 mb, mi;
        mono_colors(a, &mb, &mi);
        fill_round(rx + 6, ry + 5, 20, 20, 3, mb);
        WCHAR m[2] = { (WCHAR)towupper(a->name[0]), 0 };
        text(m, rx + 6, ry + 5, 20, 20, font(FF_UI, 11, 1), mi, T_CENTER);
        WCHAR *nm = app_name(a->name);
        HFONT nf = font(FF_UI, 12, 0);
        float barx = rx + rw - 64 - 64;
        float nw = (float)measure(nm, nf);
        WCHAR *pc = cat2(L"\x00D7", num(a->procs, 0));
        HFONT pf = font(FF_UI, 10, 1);
        float pcw = a->procs > 1 ? (float)measure(pc, pf) + 10 : 0;
        float maxn = barx - (rx + 34) - 8 - (pcw ? pcw + 6 : 0);
        if (nw > maxn) nw = maxn;
        text(nm, rx + 34, ry, nw + 1, ROW_H, nf, C_INK, T_START);
        if (pcw) {
            fill_round(rx + 34 + nw + 6, ry + 8, pcw, 14, 2, C_TRACK);
            text(pc, rx + 34 + nw + 6, ry + 8, pcw, 14, pf, C_INK2, T_CENTER | T_LTR);
        }
        double frac = metric_of(a) / mx;
        if (frac < 0.03) frac = 0.03;
        fill_round(barx, ry + 13, 56, 4, 2, C_TRACK);
        if (hogrow) {
            GraphicsPath p;
            round_path(p, barx, ry + 13, (float)(56 * frac), 4, 2);
            LinearGradientBrush b(PointF(barx - 1, 0), PointF(barx + 57, 0), col(C_WARN), col(C_CRIT));
            G->FillPath(&b, &p);
        } else fill_round(barx, ry + 13, (float)(56 * frac), 4, 2, g_sort == 0 ? C_CPU : g_sort == 1 ? C_MEM : C_GPU);
        if (hot) {
            HFONT fb = font(FF_UI, 11, 1);
            int eh = hovered(X_END, i);
            card(rx + rw - 58, ry + 4, 52, 22, eh ? 0xFFFDECEC : C_CARD, tint(C_DANGER, 60));
            text(tr(S_end), rx + rw - 58, ry + 4, 52, 22, fb, C_CRIT_INK, T_CENTER);
            add_hit(rx + rw - 58, ry + 4, 52, 22, X_END, i, 1);
        } else {
            WCHAR *mv = g_sort == 0 ? pct(a->cpu, 1) : g_sort == 1 ? memf(a->mem_mb) : pct(a->gpu, 0);
            text(mv, rx + rw - 64, ry, 58, ROW_H, font(FF_UI, 12, 1), hogrow ? C_WARN_INK : C_INK, T_END | T_LTR);
        }
    }
    *out_h = h;
}

static void draw_tooltip(void) {
    if (g_view != V_NONE || g_hover_row < 0 || g_hover_row >= g_nsorted) return;
    AppGroup *a = g_sorted[g_hover_row];
    HFONT fb = font(FF_UI, 11, 1), f = font(FF_UI, 11, 0);
    const WCHAR *k[4] = { tr(S_tipUserKernel), tr(S_tipRam), tr(S_tipGpu), tr(S_tipProc) };
    const WCHAR *v[4] = {
        cat3(pct(a->cpu * a->user_share / 100, 1), L" \x00B7 ", pct(a->cpu * (100 - a->user_share) / 100, 1)),
        pct(g_tel.mem_total_gb > 0 ? a->mem_mb / 1024 / g_tel.mem_total_gb * 100 : 0, 1), pct(a->gpu, 0), num(a->procs, 0) };
    float w = 214, h = 22 + 4 * 16 + 22;
    int up = g_hover_row >= 3 || (g_nsorted >= 3 && g_hover_row == g_nsorted - 1);
    float ty = up ? g_rows_y + g_hover_row * ROW_H - h - 2 : g_rows_y + (g_hover_row + 1) * ROW_H + 2;
    float tx = X0 + 40;
    { SolidBrush sh(col(0x4D000000)); G->FillRectangle(&sh, tx + 2, ty + 2, w, h); }
    { SolidBrush b(col(C_TIP_BG)); G->FillRectangle(&b, tx, ty, w, h); }
    { Pen p(col(C_TIP_B), 1); G->DrawRectangle(&p, tx + 0.5f, ty + 0.5f, w - 1, h - 1); }
    text(app_name(a->name), tx + 8, ty + 4, w - 16, 16, fb, C_INK, T_START);
    for (int i = 0; i < 4; i++) {
        text(k[i], tx + 8, ty + 21 + i * 16, w - 16, 16, f, C_INK2, T_START);
        text(v[i], tx + 8, ty + 21 + i * 16, w - 16, 16, fb, C_INK, T_END | T_LTR);
    }
    line(tx + 8, ty + h - 21.5f, tx + w - 8, ty + h - 21.5f, 0x40000000, 1);
    text(tr(S_tipClick), tx + 8, ty + h - 20, w - 16, 18, f, C_MEM_INK, T_START);
}

static void draw_footer(float y) {
    HFONT fb = font(FF_UI, 12, 1), f = font(FF_UI, 11, 0);
    WCHAR *ml = tr1(S_openMonitor, "monitor", hw(H_taskManager));
    float bw = (float)measure(ml, fb) + 40;
    if (bw > 196) bw = 196;
    push_button(X0, y, bw, 28, hovered(X_MONITOR, 0), 0);
    text(ml, X0 + 10, y, bw - 34, 28, fb, C_INK, T_START);
    icon(I_EXT, X0 + bw - 22, y + 7, 13, C_INK2);
    add_hit(X0, y, bw, 28, X_MONITOR, 0, 1);
    WCHAR *q = tr(S_quit);
    float qw = (float)measure(q, fb) + 20;
    int qh = hovered(X_QUIT, 0);
    if (qh) fill_round(X0 + CWID - qw, y, qw, 28, 3, C_HOVER);
    text(q, X0 + CWID - qw, y, qw, 28, fb, C_INK2, T_CENTER);
    add_hit(X0 + CWID - qw, y, qw, 28, X_QUIT, 0, 1);
    WCHAR *pv = tr(S_privacy);
    float avail = CWID - bw - qw - 12;
    float pw = (float)measure(pv, f);
    if (pw > avail - 16) pw = avail - 16;
    float px = X0 + bw + 6 + (avail - pw - 16) / 2;
    icon(I_LOCK, px, y + 8, 12, C_INK3);
    text(pv, px + 15, y, pw + 2, 28, f, C_INK3, T_START);
}

/* ---- detail view ------------------------------------------------------------ */
typedef WCHAR *(*FmtFn)(double);
static WCHAR *f_pct0(double x) { return pct(x, 0); }
static WCHAR *f_pct1(double x) { return pct(x, 1); }
static WCHAR *f_w(double x) { return watts(x); }
static WCHAR *f_t1(double x) { return temp(x, 1); }
static WCHAR *f_io(double x) { return io_s(x); }
static WCHAR *f_dn(double x) { return cat2(L"\x2193 ", down_s(x)); }
static WCHAR *f_up(double x) { return cat2(L"\x2191 ", up_s(x)); }

struct Detail {
    WCHAR *title, *hero, *sub, *unit;
    Hist *a, *b;
    float now_a, now_b;
    int split, zero, acc, acc2, is_app;
    FmtFn fa, fb;
    WCHAR *aLabel, *bLabel;
    WCHAR *tk[6], *tv[6];
};

static void build_detail(Detail *d) {
    Telemetry *t = &g_tel;
    static Hist empty;
    memset(d, 0, sizeof *d);
    d->unit = (WCHAR *)L"";
    d->fa = f_pct0;
    switch (g_view) {
    case V_CPU: {
        WCHAR *cores = plural(P_core, t->ncpu);
        d->title = tr(S_cpu); d->hero = pct(t->cpu, 0);
        d->sub = tr3(S_cpuDetailSub, "user", pct(t->user, 0), "sys", pct(t->sys, 0), "cores", g_cpu_model[0] ? join2(cores, g_cpu_model) : cores);
        d->a = &t->h_cpu; d->now_a = (float)t->cpu; d->zero = 1; d->acc = A_CPU; d->unit = tr(S_unitCpu);
        d->tk[0] = tr(S_tUser); d->tv[0] = pct(t->user, 0);
        d->tk[1] = tr(S_tSystem); d->tv[1] = pct(t->sys, 0);
        d->tk[2] = tr(S_tIdle); d->tv[2] = pct(100 - floor(t->cpu + 0.5), 0);
        d->tk[3] = hw1(H_coreN, "n", num(0, 0)); d->tv[3] = pct(t->core[0], 0);
        d->tk[4] = hw1(H_coreN, "n", num(1, 0)); d->tv[4] = pct(t->core[1], 0);
        d->tk[5] = tr(S_tThreads); d->tv[5] = num(t->thread_count, 0);
        break;
    }
    case V_MEM: {
        double app = t->mem_used_gb - t->mem_kernel_gb - t->mem_cache_gb;
        d->title = tr(S_mem); d->hero = pct(t->mem_pct, 0);
        d->sub = tr2(S_memInUse, "used", gb(t->mem_used_gb, 2), "total", gb(t->mem_total_gb, 1));
        d->a = &t->h_mem; d->now_a = (float)t->mem_pct; d->acc = A_MEM; d->unit = tr(S_unitRam);
        d->tk[0] = tr(S_segApp); d->tv[0] = gb(app > 0 ? app : 0, 2);
        d->tk[1] = hw(H_kernel); d->tv[1] = gb(t->mem_kernel_gb, 2);
        d->tk[2] = hw(H_cache); d->tv[2] = gb(t->mem_cache_gb, 2);
        d->tk[3] = tr(S_segFree); d->tv[3] = gb(t->mem_free_gb, 2);
        d->tk[4] = hw(H_pageFile); d->tv[4] = gb(t->pagefile_gb, 1);
        d->tk[5] = hw(H_commitCharge); d->tv[5] = cat3(num(t->commit_gb, 1), L" / ", gb(t->commit_limit_gb, 1));
        break;
    }
    case V_NRG: {
        WCHAR *left = t->batt_min_left >= 0 ? dur(t->batt_min_left / 60, t->batt_min_left % 60) : (WCHAR *)DASH;
        d->title = tr(S_nrg); d->hero = t->no_batt ? tr(S_noBattery) : pct(t->batt_pct, 0);
        d->sub = t->no_batt ? tr(S_acPower) : t->charging ? tr1(S_chargingFullIn, "time", dur(0, 52)) : tr1(S_onBattLeft, "time", left);
        d->a = &t->h_nrg; d->now_a = (float)t->watts; d->zero = 1; d->acc = A_NRG; d->fa = f_w;
        d->tk[0] = tr(S_tDraw); d->tv[0] = t->no_batt ? watts(t->watts) : t->charging ? cat2(L"+", watts(t->batt_sim ? 48 : t->watts)) : cat2(L"\x2212", watts(t->watts));
        d->tk[1] = tr(S_tTimeLeft); d->tv[1] = t->no_batt ? (WCHAR *)DASH : t->charging ? tr1(S_fullIn, "time", dur(0, 52)) : left;
        d->tk[2] = tr(S_tHealth); d->tv[2] = t->batt_sim ? pct(95, 0) : (WCHAR *)DASH;
        d->tk[3] = tr(S_tCycles); d->tv[3] = t->batt_sim ? num(118, 0) : (WCHAR *)DASH;
        d->tk[4] = tr(S_tCapacity); d->tv[4] = t->batt_sim ? cat3(num(69, 1), L" / ", cat2(num(72.6, 1), L" Wh")) : (WCHAR *)DASH;
        d->tk[5] = tr(S_tSource); d->tv[5] = t->no_batt || t->charging ? tr(S_acPower) : tr(S_tBattery);
        break;
    }
    case V_THM: {
        int lv = thermal_level(t->temp_c);
        d->title = tr(S_thm); d->hero = temp(t->temp_c, 0);
        d->sub = join2(tr(THERMAL_KEYS[lv]), tr(lv < 3 ? S_zero : S_throttling));
        d->a = &t->h_thm; d->now_a = (float)t->temp_c; d->acc = THERMAL_ACC[lv]; d->fa = f_t1;
        d->tk[0] = tr(S_tCpuDie); d->tv[0] = temp(t->temp_c, 1);
        d->tk[1] = tr(S_gpu); d->tv[1] = temp(t->gpu_temp_c, 1);
        d->tk[2] = tr(S_tStorage); d->tv[2] = temp(29, 0);
        d->tk[3] = tr(S_tBattery); d->tv[3] = temp(28, 0);
        d->tk[4] = tr(S_tFans); d->tv[4] = cat2(num(2400, 0), L" RPM");
        d->tk[5] = tr(S_tThrottling); d->tv[5] = pct(0, 0);
        break;
    }
    case V_GPU:
        d->title = tr(S_gpu); d->hero = pct(t->gpu_pct, 0);
        d->sub = join2(t->gpu_name[0] ? t->gpu_name : DASH, temp(t->gpu_temp_c, 0));
        d->a = &t->h_gpu; d->now_a = (float)t->gpu_pct; d->zero = 1; d->acc = A_GPU; d->unit = tr(S_unitGpu);
        d->tk[0] = tr(S_util); d->tv[0] = pct(t->gpu_pct, 0);
        d->tk[1] = tr(S_tTemperature); d->tv[1] = temp(t->gpu_temp_c, 1);
        d->tk[2] = tr(S_tVideoMem); d->tv[2] = t->gpu_vram[0] ? t->gpu_vram : (WCHAR *)DASH;
        d->tk[3] = tr(S_tCoreClock); d->tv[3] = (WCHAR *)DASH;
        d->tk[4] = tr(S_tPower); d->tv[4] = (WCHAR *)DASH;
        d->tk[5] = tr(S_tHotspot); d->tv[5] = temp(t->gpu_temp_c + 6.5, 1);
        break;
    case V_SSD:
        d->title = tr(S_storage); d->hero = pct(t->disk_pct, 0);
        d->sub = join2(hw1(H_localDisk, "drive", t->drive), tr1(S_free, "value", gb(t->disk_free_gb, 0)));
        d->a = &t->h_rd; d->b = &t->h_wr; d->now_a = (float)t->disk_read_mbs; d->now_b = (float)t->disk_write_mbs;
        d->split = 1; d->acc = A_CPU; d->acc2 = A_NRG; d->fa = f_io; d->fb = f_io;
        d->aLabel = tr(S_read); d->bLabel = tr(S_write);
        d->tk[0] = tr(S_tUsed); d->tv[0] = gb(t->disk_total_gb - t->disk_free_gb, 0);
        d->tk[1] = tr(S_tFree); d->tv[1] = gb(t->disk_free_gb, 0);
        d->tk[2] = tr(S_read); d->tv[2] = io_s(t->disk_read_mbs);
        d->tk[3] = tr(S_write); d->tv[3] = io_s(t->disk_write_mbs);
        d->tk[4] = tr(S_tCapacity); d->tv[4] = gb(t->disk_total_gb, 0);
        d->tk[5] = tr(S_tFormat); d->tv[5] = t->fs;
        break;
    case V_NET: {
        WCHAR *iface = (WCHAR *)(t->net_type == 71 ? L"Wi-Fi" : L"Ethernet");
        WCHAR *speed = t->net_speed_mbps >= 1 ? cat2(num(t->net_speed_mbps, 0), L" Mbps") : (WCHAR *)DASH;
        d->title = tr(S_network); d->hero = down_s(t->net_down_kbs);
        d->sub = t->net_ok ? hw3(H_netSub, "link", iface, "detail", speed, "state", hw(H_connected)) : (WCHAR *)DASH;
        d->a = &t->h_dn; d->b = &t->h_up; d->now_a = (float)t->net_down_kbs; d->now_b = (float)t->net_up_kbs;
        d->split = 1; d->acc = A_MEM; d->acc2 = A_CPU; d->fa = f_dn; d->fb = f_up;
        d->aLabel = tr(S_down); d->bLabel = tr(S_up);
        d->tk[0] = tr(S_down); d->tv[0] = down_s(t->net_down_kbs);
        d->tk[1] = tr(S_up); d->tv[1] = up_s(t->net_up_kbs);
        d->tk[2] = tr(S_tInterface); d->tv[2] = iface;
        d->tk[3] = hw(H_linkSpeed); d->tv[3] = speed;
        d->tk[4] = tr(S_tLatency); d->tv[4] = (WCHAR *)DASH;
        d->tk[5] = tr(S_tToday); d->tv[5] = cat3(cat2(num(t->net_today_down_mb, 0), L" MB"), L" / ", cat2(num(t->net_today_up_mb, 0), L" MB"));
        break;
    }
    default: {
        AppGroup *a = find_group(g_view_app);
        static AppGroup gone;
        if (!a) { memset(&gone, 0, sizeof gone); lstrcpynW(gone.name, g_view_app, 64); a = &gone; }
        d->is_app = 1;
        d->title = app_name(a->name); d->hero = pct(a->cpu, 1);
        WCHAR pid[24];
        _snwprintf(pid, 24, L"PID %u", (unsigned)a->pid);
        d->sub = join3(plural(P_process, a->procs), plural(P_thread, a->threads), cat2(L"", pid));
        d->a = tel_app_hist(t, a->name);
        if (!d->a) d->a = &empty;
        d->now_a = (float)a->cpu; d->zero = 1; d->acc = a->hog ? A_WARN : A_CPU; d->fa = f_pct1; d->unit = tr(S_unitOfCpu);
        d->tk[0] = tr(S_cpu); d->tv[0] = pct(a->cpu, 1);
        d->tk[1] = tr(S_mem); d->tv[1] = memf(a->mem_mb);
        d->tk[2] = tr(S_gpu); d->tv[2] = pct(a->gpu, 0);
        d->tk[3] = tr(S_tipProc); d->tv[3] = num(a->procs, 0);
        d->tk[4] = tr(S_tThreads); d->tv[4] = num(a->threads, 0);
        d->tk[5] = (WCHAR *)L"PID"; d->tv[5] = num(a->pid, 0);
    }
    }
    if (!d->acc2) d->acc2 = d->acc;
}

/* values for the chart: history with the live value as the last point */
static int chart_values(const Hist *h, float now, float *out) {
    int n = h->n;
    memcpy(out, h->v, sizeof(float) * n);
    if (n) out[n - 1] = now;
    else { out[0] = now; n = 1; }
    return n;
}

static float draw_chart(Detail *d, float y) {
    float cx = X0, cw = CWID, top = d->split ? 48 : 34, ch = 140, h = top + ch + 26;
    float va[HIST_N], vb[HIST_N];
    int na = chart_values(d->a, d->now_a, va), nb = d->split ? chart_values(d->b, d->now_b, vb) : 0;
    card(cx, y, cw, h, C_CARD, C_CARD_B);
    HFONT fb = font(FF_UI, 12, 1), f = font(FF_UI, 11, 0);
    text(tr(S_last10), cx + 12, y + 8, 160, 18, fb, C_INK, T_START);
    float mxa = 0, mna = 1e30f, suma = 0, mxb = 0;
    for (int i = 0; i < na; i++) { if (va[i] > mxa) mxa = va[i]; if (va[i] < mna) mna = va[i]; suma += va[i]; }
    for (int i = 0; i < nb; i++) if (vb[i] > mxb) mxb = vb[i];
    WCHAR *stat;
    if (!d->split) stat = tr2(S_peakAvg, "peak", d->fa(mxa), "avg", d->fa(suma / na));
    else stat = tr2(S_peakSplit, "a", d->fa(mxa), "b", d->fb(mxb));
    if (d->split) {
        float bx = cx + cw - 12;
        for (int s = 1; s >= 0; s--) {
            WCHAR *lb = s ? d->bLabel : d->aLabel;
            int acc = s ? d->acc2 : d->acc;
            float w = (float)measure(lb, font(FF_UI, 11, 1)) + 26;
            bx -= w;
            fill_round(bx, y + 9, w, 18, 3, tint(ACC[acc], 13));
            icon(s ? I_DOWN : I_UP, bx + 4, y + 11, 14, ACC_INK[acc]);
            text(lb, bx + 18, y + 9, w - 22, 18, font(FF_UI, 11, 1), ACC_INK[acc], T_START);
            bx -= 6;
        }
        text(stat, cx + 12, y + 28, cw - 24, 14, f, C_INK2, T_START);
    } else text(stat, cx + 180, y + 8, cw - 192, 18, f, C_INK2, T_END);

    float px0 = cx + 12, pw = cw - 24, py = y + top;
    /* grid */
    {
        Pen dp(col(0x55ACA899), 1);
        REAL pat[] = { 1, 3 };
        dp.SetDashPattern(pat, 2);
        for (int i = 1; i <= 3; i++) {
            float gy = floorf(py + ch * i / 4) + 0.5f;
            if (d->split && i == 2) { Pen sp(col(0xFFACA899), 1); G->DrawLine(&sp, px0, gy, px0 + pw, gy); }
            else G->DrawLine(&dp, px0, gy, px0 + pw, gy);
        }
    }
    PointF pa[HIST_N], pb[HIST_N];
    int off_a = HIST_N - na, off_b = HIST_N - nb;
    if (!d->split) {
        float lo = d->zero ? 0 : (mna - (mxa - mna) * 0.8f > 0 ? mna - (mxa - mna) * 0.8f : 0), hi = mxa + (mxa - lo) * 0.2f;
        if (hi - lo < 1e-3f) hi = lo + 1;
        for (int i = 0; i < na; i++) pa[i] = PointF(px0 + (off_a + i) * pw / (HIST_N - 1), py + ch - 6 - (va[i] - lo) / (hi - lo) * (ch - 34));
        curve(pa, na, py + ch, ACC[d->acc], 0.30f, py + 10, py + ch);
    } else {
        float c = py + ch / 2, ma = mxa * 1.12f, mb = mxb * 1.12f;
        if (ma <= 0) ma = 1;
        if (mb <= 0) mb = 1;
        for (int i = 0; i < na; i++) pa[i] = PointF(px0 + (off_a + i) * pw / (HIST_N - 1), c - 1 - va[i] / ma * (ch / 2 - 22));
        for (int i = 0; i < nb; i++) pb[i] = PointF(px0 + (off_b + i) * pw / (HIST_N - 1), c + 1 + vb[i] / mb * (ch / 2 - 22));
        curve(pa, na, c, ACC[d->acc], 0.30f, py, c);
        curve(pb, nb, c, ACC[d->acc2], 0.30f, c + ch / 2, c);
    }
    /* axis */
    text(tr(S_ago10), px0, py + ch + 4, 100, 14, f, C_INK3, T_START);
    text(tr(S_ago5), px0 + pw / 2 - 50, py + ch + 4, 100, 14, f, C_INK3, T_CENTER);
    text(tr(S_now), px0 + pw - 100, py + ch + 4, 100, 14, f, C_INK3, T_END);
    add_hit(px0, py, pw, ch, X_CHART, 0, 0);
    /* scrub */
    if (g_chart >= 0) {
        int i = g_chart > HIST_N - 1 ? HIST_N - 1 : g_chart;
        int ia = i - off_a;
        if (ia >= 0 && ia < na) {
            float x = px0 + i * pw / (HIST_N - 1);
            Pen dl(col(0xB0000000), 1);
            REAL pat[] = { 3, 3 };
            dl.SetDashPattern(pat, 2);
            G->DrawLine(&dl, floorf(x) + 0.5f, py, floorf(x) + 0.5f, py + ch);
            SolidBrush wb(col(0xFFFFFFFF));
            Pen ca(col(ACC[d->acc]), 2);
            G->FillEllipse(&wb, pa[ia].X - 4, pa[ia].Y - 4, 8.0f, 8.0f);
            G->DrawEllipse(&ca, pa[ia].X - 4, pa[ia].Y - 4, 8.0f, 8.0f);
            int ib = i - off_b;
            if (d->split && ib >= 0 && ib < nb) {
                Pen cb(col(ACC[d->acc2]), 2);
                G->FillEllipse(&wb, pb[ib].X - 4, pb[ib].Y - 4, 8.0f, 8.0f);
                G->DrawEllipse(&cb, pb[ib].X - 4, pb[ib].Y - 4, 8.0f, 8.0f);
            }
            int secs = (HIST_N - 1 - i) * 5, m = secs / 60, sc = secs % 60;
            WCHAR *ago = secs == 0 ? tr(S_now) : !m ? tr1(S_agoS, "s", num(sc, 0)) : sc ? tr2(S_agoMS, "m", num(m, 0), "s", num(sc, 0)) : tr1(S_agoM, "m", num(m, 0));
            WCHAR *val = d->split ? join2(d->fa(va[ia]), (ib >= 0 && ib < nb) ? d->fb(vb[ib]) : DASH)
                                  : (d->unit[0] ? cat3(d->fa(va[ia]), L" ", d->unit) : d->fa(va[ia]));
            WCHAR *lab = join2(ago, val);
            HFONT pf = font(FF_UI, 11, 0);
            float lw = (float)measure(lab, pf) + 14;
            float centre = x, lim = px0 + pw * 0.16f, rim = px0 + pw * 0.84f;
            if (centre < lim) centre = lim;
            if (centre > rim) centre = rim;
            float lx = centre - lw / 2;
            if (lx < px0) lx = px0;
            if (lx + lw > px0 + pw) lx = px0 + pw - lw;
            SolidBrush tb(col(C_TIP_BG));
            G->FillRectangle(&tb, lx, py - 2, lw, 18.0f);
            Pen tp(col(C_TIP_B), 1);
            G->DrawRectangle(&tp, lx + 0.5f, py - 1.5f, lw - 1, 17.0f);
            text(lab, lx, py - 2, lw, 18, pf, C_INK, T_CENTER);
        }
    }
    return h;
}

static float draw_detail(float y) {
    Detail d;
    build_detail(&d);
    HFONT fb = font(FF_UI, 12, 1);
    /* back + pill (+ End App) */
    WCHAR *bk = tr(S_back);
    float bw = (float)measure(bk, fb) + 34;
    push_button(X0, y, bw, 26, hovered(X_BACK, 0), 0);
    icon(I_BACK, X0 + 6, y + 5, 16, C_INK);
    text(bk, X0 + 22, y, bw - 26, 26, fb, C_INK, T_START);
    add_hit(X0, y, bw, 26, X_BACK, 0, 1);
    float endw = 0;
    if (d.is_app) {
        WCHAR *eb = tr(S_endApp);
        endw = (float)measure(eb, fb) + 22;
        int eh = hovered(X_END_CURRENT, 0);
        card(X0 + CWID - endw, y, endw, 26, eh ? 0xFFFDECEC : C_CARD, tint(C_DANGER, 60));
        text(eb, X0 + CWID - endw, y, endw, 26, fb, C_CRIT_INK, T_CENTER);
        add_hit(X0 + CWID - endw, y, endw, 26, X_END_CURRENT, 0, 1);
    }
    float px = X0 + bw + 8, maxw = CWID - bw - 8 - (endw ? endw + 8 : 0);
    float pw = (float)measure(d.title, fb) + 30;
    if (pw > maxw) pw = maxw;
    fill_round(px, y, pw, 26, 3, tint(ACC[d.acc], 15));
    { SolidBrush b(col(ACC[d.acc])); G->FillEllipse(&b, px + 9, y + 9, 8.0f, 8.0f); SolidBrush hb(col(tint(ACC[d.acc], 35))); G->FillEllipse(&hb, px + 6, y + 6, 14.0f, 14.0f); G->FillEllipse(&b, px + 9, y + 9, 8.0f, 8.0f); }
    text(d.title, px + 24, y, pw - 28, 26, fb, ACC_INK[d.acc], T_START);
    y += 34;
    /* hero */
    HFONT hf = font(FF_HERO, 28, 1);
    float hw_ = (float)measure(d.hero, hf);
    text(d.hero, X0 + 2, y, hw_ + 2, 36, hf, C_INK, T_START | T_LTR);
    text(d.sub, X0 + hw_ + 14, y + 2, CWID - hw_ - 16, 36, font(FF_UI, 11, 0), C_INK2, T_START);
    y += 42;
    y += draw_chart(&d, y) + 8;
    /* tiles */
    for (int i = 0; i < 6; i++) {
        float tx = X0 + (i % 2) * (HALF + GAP), ty = y + (i / 2) * 58;
        card(tx, ty, HALF, 50, C_CARD, C_CARD_B);
        text(d.tk[i], tx + 12, ty + 8, HALF - 24, 14, font(FF_UI, 11, 0), C_INK2, T_START);
        text(d.tv[i], tx + 12, ty + 24, HALF - 24, 20, font(i == 5 && d.is_app ? FF_MONO : FF_UI, 14, 1), C_INK, T_START | T_LTR);
    }
    return y + 3 * 58;
}

static float draw_settings(float y) {
    HFONT fb = font(FF_UI, 12, 1), f = font(FF_UI, 11, 0);
    WCHAR *bk = tr(S_back);
    float bw = (float)measure(bk, fb) + 34;
    push_button(X0, y, bw, 26, hovered(X_BACK, 0), 0);
    icon(I_BACK, X0 + 6, y + 5, 16, C_INK);
    text(bk, X0 + 22, y, bw - 26, 26, fb, C_INK, T_START);
    add_hit(X0, y, bw, 26, X_BACK, 0, 1);
    WCHAR *st = tr(S_aSettings);
    float pw = (float)measure(st, fb) + 38;
    fill_round(X0 + bw + 8, y, pw, 26, 3, tint(C_MEM, 15));
    icon(I_GEAR, X0 + bw + 13, y + 5, 16, C_MEM_INK);
    text(st, X0 + bw + 32, y, pw - 34, 26, fb, C_MEM_INK, T_START);
    y += 34;
    float rh = 48, h = rh * 6 + 8;
    card(X0, y, CWID, h, C_CARD, C_CARD_B);
    {
        /* Language: a drop-down that opens a native popup menu */
        float ry = y + 4;
        WCHAR *cur = g_lang_pref < 0 ? tr(S_langSystem) : (WCHAR *)i18n_locale(g_lang_pref)->name;
        float cw = 150, cx = X0 + CWID - 12 - cw, cy = ry + 12;
        text(tr(S_language), X0 + 12, ry + 7, cx - X0 - 20, 17, fb, C_INK, T_START);
        text(tr(S_languageSub), X0 + 12, ry + 24, cx - X0 - 20, 15, f, C_INK2, T_START);
        int hot = hovered(X_LANGPICK, 0);
        { SolidBrush b(col(0xFFFFFFFF)); G->FillRectangle(&b, cx, cy, cw, 24.0f); }
        { Pen p(col(hot ? 0xFF3C7FB1 : C_SEG_B), 1); G->DrawRectangle(&p, cx + 0.5f, cy + 0.5f, cw - 1, 23.0f); }
        float bx = cx + cw - 19;
        {
            GraphicsPath bp;
            round_path(bp, bx, cy + 2, 17, 20, 2);
            LinearGradientBrush bb(PointF(0, cy + 2), PointF(0, cy + 22), col(hot ? 0xFFE6EEFC : 0xFFC9DAF8), col(hot ? 0xFFB8CFF8 : 0xFFA5C0F2));
            G->FillPath(&bb, &bp);
            Pen ap(col(0xFF4D6185), 1.6f);
            PointF v[] = { PointF(bx + 5, cy + 10), PointF(bx + 8.5f, cy + 14), PointF(bx + 12, cy + 10) };
            G->DrawLines(&ap, v, 3);
        }
        text(cur, cx + 6, cy, cw - 30, 24, font(FF_UI, 12, 0), C_INK, T_START);
        add_hit(cx, cy, cw, 24, X_LANGPICK, 0, 1);
        g_combo_x = cx; g_combo_y = cy + DY; g_combo_w = cw; g_combo_h = 24;
        y += rh;
    }
    struct { WCHAR *l, *s; int on; } sw[3] = {
        { tr(S_simHog), tr1(S_simHogSub, "pct", pct(50, 0)), g_sim_hog },
        { tr(S_simCharging), tr1(S_simChargingSub, "adapter", (WCHAR *)L"65 W"), g_sim_charging },
        { tr(S_simLive), tr1(S_simLiveSub, "secs", tr1(S_seconds, "n", num(1.5, 1))), g_live } };
    for (int i = 0; i < 5; i++) {
        float ry = y + 4 + i * rh;
        line(X0 + 12, floorf(ry) + 0.5f, X0 + CWID - 12, floorf(ry) + 0.5f, C_TRACK, 1);
        if (i < 3) {
            text(sw[i].l, X0 + 12, ry + 7, CWID - 80, 17, fb, C_INK, T_START);
            text(sw[i].s, X0 + 12, ry + 24, CWID - 80, 15, f, C_INK2, T_START);
            float sx = X0 + CWID - 12 - 38, sy = ry + 15;
            int hot = hovered(X_SWITCH, i);
            fill_round(sx, sy, 38, 20, 10, sw[i].on ? C_CPU : (hot ? 0xFF9A9686 : C_TRACK_S));
            SolidBrush kb(col(0xFFFFFFFF));
            G->FillEllipse(&kb, sx + (sw[i].on ? 20.0f : 2.0f), sy + 2, 16.0f, 16.0f);
            add_hit(X0 + 6, ry, CWID - 12, rh, X_SWITCH, i, 1);
        } else if (i == 3) {
            text(tr(S_tempUnit), X0 + 12, ry, CWID - 120, rh, fb, C_INK, T_START);
            float ux = X0 + CWID - 12 - 84;
            fill_round(ux, ry + 11, 84, 26, 3, C_TRACK);
            const WCHAR *lab[2] = { L"\x00B0" L"C", L"\x00B0" L"F" };
            for (int k = 0; k < 2; k++) {
                float sx = ux + 2 + k * 40;
                int sel = g_unit_f == k;
                if (sel) { fill_round(sx, ry + 13, 40, 22, 3, C_CARD); stroke_round(sx, ry + 13, 40, 22, 3, C_SEG_B); }
                text(lab[k], sx, ry + 13, 40, 22, font(FF_UI, 12, sel), sel ? C_INK : C_INK2, T_CENTER | T_LTR);
                add_hit(sx, ry + 13, 40, 22, X_UNIT, k, 1);
            }
        } else {
            int n = tel_ended_count();
            text(tr(S_restore), X0 + 12, ry + 7, CWID - 120, 17, fb, n ? C_INK : C_INK3, T_START);
            text(plural(P_ended, n), X0 + 12, ry + 24, CWID - 120, 15, f, C_INK2, T_START);
            WCHAR *rbt = tr(S_restoreBtn);
            float rw = (float)measure(rbt, fb) + 24;
            push_button(X0 + CWID - 12 - rw, ry + 11, rw, 26, n && hovered(X_RESTORE, 0), 0);
            text(rbt, X0 + CWID - 12 - rw, ry + 11, rw, 26, fb, n ? C_INK : 0xFFA0A0A0, T_CENTER);
            add_hit(X0 + CWID - 12 - rw, ry + 11, rw, 26, X_RESTORE, 0, 1);
        }
    }
    y -= rh;
    text(tr(S_simNote), X0 + 4, y + h + 4, CWID - 8, 15, f, C_INK3, T_START);
    return y + h + 20;
}

/* ------------------------------------------------------------------------ */
/* Frame                                                                    */
/* ------------------------------------------------------------------------ */
static void ensure_buffer(int h) {
    if (g_mbmp && g_mbmp_h >= h) return;
    if (g_mbmp) DeleteObject(g_mbmp);
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = WIN_W;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    g_mbmp = CreateDIBSection(0, &bi, DIB_RGB_COLORS, &g_mbits, 0, 0);
    g_mbmp_h = h;
    if (!g_mdc) g_mdc = CreateCompatibleDC(0);
    SelectObject(g_mdc, g_mbmp);
}

/* Renders the whole flyout into the back buffer. Returns the content height. */
static int render(int win_h) {
    ensure_buffer(win_h);
    Graphics gr(g_mdc);
    G = &gr;
    RTL = L->rtl;
    gr.SetSmoothingMode(SmoothingModeAntiAlias);
    gr.SetPixelOffsetMode(PixelOffsetModeHalf);
    g_nhits = 0;
    DY = 0;
    set_xform();
    {
        SolidBrush bg(col(C_FLY_BG));
        gr.FillRectangle(&bg, 0, 0, WIN_W, win_h);
    }
    DY = (float)-g_scroll;
    set_xform();
    float y = TB + 8;
    draw_header(y);
    y += 38;
    y = draw_slot(y);
    if (g_view == V_NONE) {
        draw_cpu_card(X0, y, HALF, 162);
        draw_mem_card(X0 + HALF + GAP, y, HALF, 162);
        y += 170;
        draw_nrg_card(X0, y, HALF, 162);
        draw_thm_card(X0 + HALF + GAP, y, HALF, 162);
        y += 170;
        draw_gpu_strip(y);
        y += 40;
        draw_strips(y);
        y += 64;
        float ah;
        draw_apps(y, &ah);
        y += ah + 8;
    } else if (g_view == V_SETTINGS) {
        y = draw_settings(y) + 8;
    } else {
        y = draw_detail(y) + 8;
    }
    draw_footer(y);
    y += 28 + 10 + FRAME;
    draw_tooltip();
    /* focus ring */
    if (g_focus >= 0 && g_focus < g_nhits) {
        Hit *h = &g_hits[g_focus];
        DY = 0; set_xform();
        focus_ring(h->x, h->y, h->w, h->h);
    }
    draw_titlebar();
    /* window frame */
    {
        G->ResetTransform();
        SolidBrush fb(col(C_BORDER));
        gr.FillRectangle(&fb, 0, TB, FRAME, win_h - TB);
        gr.FillRectangle(&fb, WIN_W - FRAME, TB, FRAME, win_h - TB);
        gr.FillRectangle(&fb, 0, win_h - FRAME, WIN_W, FRAME);
        Pen hl(col(0xFF3A6BEA), 1);
        gr.DrawLine(&hl, 1.5f, (REAL)TB, 1.5f, (REAL)win_h - 2);
        gr.DrawLine(&hl, WIN_W - 1.5f, (REAL)TB, WIN_W - 1.5f, (REAL)win_h - 2);
    }
    G = 0;
    return (int)(y + g_scroll + 0.5f);
}

static int max_win_h(void) {
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    return wa.bottom - wa.top - 8;
}
static void place_window(int h) {
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = L->rtl ? wa.left + 6 : wa.right - WIN_W - 6;
    int y = wa.bottom - h - 4;
    if (y < wa.top) y = wa.top;
    HRGN r = CreateRoundRectRgn(0, 0, WIN_W + 1, h + 1, 16, 16);
    HRGN b = CreateRectRgn(0, 10, WIN_W, h);
    CombineRgn(r, r, b, RGN_OR);
    DeleteObject(b);
    SetWindowPos(g_hwnd, HWND_TOPMOST, x, y, WIN_W, h, SWP_NOACTIVATE);
    SetWindowRgn(g_hwnd, r, TRUE);
    g_win_h = h;
}
/* Lays out, resizes the window to fit, and repaints. */
static void refresh(void) {
    if (!g_visible) return;
    int h = g_win_h ? g_win_h : 700;
    int content = render(h);
    int want = content, mx = max_win_h();
    if (want > mx) want = mx;
    g_content_h = content;
    if (g_scroll > content - want) g_scroll = content - want > 0 ? content - want : 0;
    if (want != g_win_h) { place_window(want); render(want); }
    InvalidateRect(g_hwnd, 0, FALSE);
}

/* ------------------------------------------------------------------------ */
/* Tray icon: 16x16 live waveform                                             */
/* ------------------------------------------------------------------------ */
static HICON make_tray_icon(DWORD *pixels_out) {
    BITMAPINFO bi;
    void *bits;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = 16;
    bi.bmiHeader.biHeight = -16;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    HBITMAP color = CreateDIBSection(0, &bi, DIB_RGB_COLORS, &bits, 0, 0);
    DWORD *px = (DWORD *)bits;
    memset(px, 0, 16 * 16 * 4);
    /* rounded light plate so the bars read on the blue taskbar */
    for (int y = 1; y < 15; y++) for (int x = 0; x < 16; x++) {
        int corner = (y == 1 || y == 14) && (x == 0 || x == 15);
        if (!corner) px[y * 16 + x] = 0xE0FFFFFF;
    }
    for (int y = 1; y < 15; y++) { px[y * 16] = 0xFF7F9DB9; px[y * 16 + 15] = 0xFF7F9DB9; }
    for (int x = 1; x < 15; x++) { px[16 + x] = 0xFF7F9DB9; px[14 * 16 + x] = 0xFF7F9DB9; }
    int n = g_tel.sp_n;
    int hog = g_tel.cpu >= 50;
    DWORD c = hog ? 0xFFF27B1E : 0xFF1FA055, c2 = hog ? 0xFFA84A00 : 0xFF13703A;
    for (int b = 0; b < 6; b++) {
        float v = n >= 6 ? g_tel.sp_cpu[n - 6 + b] : (n ? g_tel.sp_cpu[n - 1] : 0);
        int h = (int)(1 + v / 70 * 10 + 0.5f);
        if (h > 11) h = 11;
        if (h < 1) h = 1;
        int x = 2 + b * 2;
        for (int y = 13 - h + 1; y <= 13; y++) { px[y * 16 + x] = c; }
        px[(13 - h + 1) * 16 + x] = c2;
    }
    /* premultiply */
    for (int i = 0; i < 256; i++) {
        DWORD p = px[i], a = p >> 24;
        px[i] = (a << 24) | ((((p >> 16) & 255) * a / 255) << 16) | ((((p >> 8) & 255) * a / 255) << 8) | ((p & 255) * a / 255);
    }
    if (pixels_out) memcpy(pixels_out, px, 256 * 4);
    HBITMAP mask = CreateBitmap(16, 16, 1, 1, 0);
    ICONINFO ii;
    ii.fIcon = TRUE; ii.xHotspot = ii.yHotspot = 0; ii.hbmMask = mask; ii.hbmColor = color;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(color);
    return icon;
}
/* Second tray icon: CPU % as digits (5x7 font; 3x5 for 100) */
static const unsigned char DIG5x7[10][7] = {
    {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},{2,6,10,18,31,2,2},
    {31,16,30,1,1,17,14},{6,8,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,2,12} };
static const unsigned short DIG3x5[10] = { 0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF };
static HICON make_pct_icon(DWORD *pixels_out) {
    DWORD px[256];
    int v = (int)floor(g_tel.cpu + 0.5);
    DWORD ink = g_tel.cpu >= 50 ? 0xFFFFD27F : 0xFFFFFFFF;
    for (int i = 0; i < 256; i++) px[i] = 0;
    for (int y = 1; y < 15; y++) for (int x = 0; x < 16; x++) {
        int corner = (y == 1 || y == 14) && (x == 0 || x == 15);
        if (!corner) px[y * 16 + x] = g_tel.cpu >= 50 ? 0xF0A84A00 : 0xF013703A;
    }
    if (v >= 100) {
        int d[3] = { 1, 0, 0 };
        for (int k = 0; k < 3; k++) for (int r = 0; r < 5; r++) for (int c = 0; c < 3; c++)
            if (DIG3x5[d[k]] >> (14 - (r * 3 + c)) & 1) px[(5 + r) * 16 + 2 + k * 4 + c] = ink;
    } else {
        int d[2] = { v / 10, v % 10 }, x0 = 2;
        for (int k = 0; k < 2; k++) for (int r = 0; r < 7; r++) for (int c = 0; c < 5; c++)
            if ((DIG5x7[d[k]][r] >> (4 - c)) & 1) px[(4 + r) * 16 + x0 + k * 7 + c] = ink;
    }
    for (int i = 0; i < 256; i++) {
        DWORD p = px[i], a = p >> 24;
        px[i] = (a << 24) | ((((p >> 16) & 255) * a / 255) << 16) | ((((p >> 8) & 255) * a / 255) << 8) | ((p & 255) * a / 255);
    }
    if (pixels_out) memcpy(pixels_out, px, sizeof px);
    BITMAPINFO bi;
    void *bits;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = 16; bi.bmiHeader.biHeight = -16; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    HBITMAP color = CreateDIBSection(0, &bi, DIB_RGB_COLORS, &bits, 0, 0);
    memcpy(bits, px, sizeof px);
    HBITMAP mask = CreateBitmap(16, 16, 1, 1, 0);
    ICONINFO ii;
    ii.fIcon = TRUE; ii.xHotspot = ii.yHotspot = 0; ii.hbmMask = mask; ii.hbmColor = color;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(color);
    return icon;
}
static HICON g_pct_icon;
static NOTIFYICONDATAW g_nid2;

static void tray_update(int add) {
    if (g_pinned) return;
    HICON old = g_tray_icon;
    g_tray_icon = make_tray_icon(0);
    memset(&g_nid, 0, sizeof g_nid);
    g_nid.cbSize = NOTIFYICONDATAW_V2_SIZE;
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = g_tray_icon;
    _snwprintf(g_nid.szTip, 128, L"Pulse \x00B7 %ls %ls", tr(S_barCpu), pct(g_tel.cpu, 0));
    if (add) g_tray_ok = Shell_NotifyIconW(NIM_ADD, &g_nid);
    else if (!Shell_NotifyIconW(NIM_MODIFY, &g_nid)) g_tray_ok = Shell_NotifyIconW(NIM_ADD, &g_nid);
    if (old) DestroyIcon(old);
    /* Tier 1 on XP: wave + CPU %. The % reading is a second icon next to it. */
    HICON old2 = g_pct_icon;
    g_pct_icon = make_pct_icon(0);
    g_nid2 = g_nid;
    g_nid2.uID = 2;
    g_nid2.hIcon = g_pct_icon;
    if (add || !Shell_NotifyIconW(NIM_MODIFY, &g_nid2)) Shell_NotifyIconW(NIM_ADD, &g_nid2);
    if (old2) DestroyIcon(old2);
}

/* ------------------------------------------------------------------------ */
/* PNG output (screenshots and the tray icon)                                 */
/* ------------------------------------------------------------------------ */
static int png_clsid(CLSID *id) {
    UINT n = 0, sz = 0;
    GetImageEncodersSize(&n, &sz);
    if (!sz) return 0;
    ImageCodecInfo *info = (ImageCodecInfo *)malloc(sz);
    GetImageEncoders(n, sz, info);
    int ok = 0;
    for (UINT i = 0; i < n; i++) if (!wcscmp(info[i].MimeType, L"image/png")) { *id = info[i].Clsid; ok = 1; }
    free(info);
    return ok;
}
static void save_png(const WCHAR *path, void *bits, int w, int h, int stride, PixelFormat pf) {
    CLSID id;
    if (!png_clsid(&id)) return;
    Bitmap bm(w, h, stride, pf, (BYTE *)bits);
    bm.Save(path, &id, 0);
}
static void capture(const WCHAR *path) {
    int h = g_win_h ? g_win_h : 700;
    render(h);
    /* opaque copy, with the window region's top corners cut away */
    DWORD *src = (DWORD *)g_mbits;
    DWORD *tmp = (DWORD *)malloc(WIN_W * h * 4);
    HRGN rg = CreateRoundRectRgn(0, 0, WIN_W + 1, h + 1, 16, 16);
    for (int y = 0; y < h; y++) for (int x = 0; x < WIN_W; x++) {
        DWORD p = src[y * WIN_W + x] | 0xFF000000;
        if (y < 10 && !PtInRegion(rg, x, y)) p = 0x00000000;
        tmp[y * WIN_W + x] = p;
    }
    DeleteObject(rg);
    save_png(path, tmp, WIN_W, h, WIN_W * 4, PixelFormat32bppARGB);
    free(tmp);
}

/* ------------------------------------------------------------------------ */
/* Actions                                                                  */
/* ------------------------------------------------------------------------ */
static int system_lang(void) {
    WCHAR nm[16] = L"";
    char a[16] = "";
    LANGID id = GetUserDefaultUILanguage();
    GetLocaleInfoW(MAKELCID(id, SORT_DEFAULT), LOCALE_SISO639LANGNAME, nm, 16);
    WideCharToMultiByte(CP_UTF8, 0, nm, -1, a, 16, 0, 0);
    return i18n_resolve(a);  /* unknown -> en */
}
/* The choice lives in HKCU\Software\Pulse, value Language: a locale code,
   or empty for "Match system". */
static void save_lang_pref(void) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Pulse", 0, 0, 0, KEY_WRITE, 0, &k, 0) != ERROR_SUCCESS) return;
    WCHAR code[32] = L"";
    if (g_lang_pref >= 0) MultiByteToWideChar(CP_UTF8, 0, i18n_locale(g_lang_pref)->code, -1, code, 32);
    RegSetValueExW(k, L"Language", 0, REG_SZ, (const BYTE *)code, (lstrlenW(code) + 1) * sizeof(WCHAR));
    RegCloseKey(k);
}
static int load_lang_pref(void) {
    HKEY k;
    WCHAR code[32] = L"";
    DWORD sz = sizeof code - sizeof(WCHAR), type = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Pulse", 0, KEY_READ, &k) != ERROR_SUCCESS) return -1;
    if (RegQueryValueExW(k, L"Language", 0, &type, (BYTE *)code, &sz) != ERROR_SUCCESS || type != REG_SZ) code[0] = 0;
    RegCloseKey(k);
    if (!code[0]) return -1;
    char a[32];
    WideCharToMultiByte(CP_UTF8, 0, code, -1, a, 32, 0, 0);
    for (int i = 0; i < i18n_count(); i++) if (!strcmp(i18n_locale(i)->code, a)) return i;
    return -1;  /* that locale file is gone: match system */
}
static void set_lang(int idx);
static void set_lang_pref(int pref, int save) {
    g_lang_pref = pref;
    if (save) save_lang_pref();
    set_lang(pref < 0 ? system_lang() : pref);
}
static void lang_picker(void) {
    HMENU m = CreatePopupMenu();
    int n = i18n_count();
    AppendMenuW(m, MF_STRING, 200, tr(S_langSystem));
    AppendMenuW(m, MF_SEPARATOR, 0, 0);
    for (int k = 0; k < n; k++) AppendMenuW(m, MF_STRING, 201 + k, (const WCHAR *)i18n_locale(k)->name);
    int sel = g_lang_pref < 0 ? 200 : 201 + g_lang_pref;
    CheckMenuRadioItem(m, 200, 201 + n - 1, sel, MF_BYCOMMAND);
    RECT wr;
    GetWindowRect(g_hwnd, &wr);
    float px = L->rtl ? WIN_W - g_combo_x - g_combo_w : g_combo_x;
    int sx = wr.left + (int)(L->rtl ? px + g_combo_w : px), sy = wr.top + (int)(g_combo_y + g_combo_h);
    g_in_menu = 1;
    /* TPM_LAYOUTRTL flips the alignment flag, so LEFTALIGN here puts the
       menu's right edge under the combo's right edge */
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTBUTTON | TPM_LEFTALIGN | (L->rtl ? TPM_LAYOUTRTL : 0), sx, sy, 0, g_hwnd, 0);
    g_in_menu = 0;
    DestroyMenu(m);
    if (cmd == 200) set_lang_pref(-1, 1);
    else if (cmd > 200) set_lang_pref(cmd - 201, 1);
    else refresh();
}

static void set_lang(int idx) {
    g_lang = idx;
    L = i18n_locale(idx);
    fonts_reset();
    LONG ex = GetWindowLongW(g_hwnd, GWL_EXSTYLE);
    ex = L->rtl ? (ex | WS_EX_LAYOUTRTL) : (ex & ~WS_EX_LAYOUTRTL);
    SetWindowLongW(g_hwnd, GWL_EXSTYLE, ex);
    g_focus = -1;
    g_win_h = 0;
    if (g_visible) { place_window(700); refresh(); }
    tray_update(0);
}
static void open_view(int v, const WCHAR *app) {
    if (v != V_SETTINGS) g_last_view = v;
    g_view = v;
    if (app) lstrcpynW(g_view_app, app, 64);
    g_chart = -1; g_hover_row = -1; g_scroll = 0; g_focus = -1;
}
static void request_end(const WCHAR *name) { lstrcpynW(g_confirm, name, 64); g_hover_row = -1; }
static void confirm_end(void) {
    AppGroup *a = find_group(g_confirm);
    WCHAR name[64];
    lstrcpynW(name, g_confirm, 64);
    int ok = 1;
    if (a && a->sim) g_sim_hog = 0;
    else ok = tel_end_group(&g_tel, name) > 0;
    if (ok && g_view == V_APP && !_wcsicmp(g_view_app, name)) g_view = V_NONE;
    g_confirm[0] = 0;
    show_toast(ok ? tr1(S_toastEnded, "app", app_name(name)) : tr1(S_endFailed, "app", app_name(name)));
    tel_sample(&g_tel, g_sim_hog, g_sim_charging);
}
static void show_flyout(int show) {
    g_visible = show;
    if (show) {
        g_win_h = 0;
        place_window(700);
        refresh();
        ShowWindow(g_hwnd, SW_SHOWNORMAL);
        SetForegroundWindow(g_hwnd);
    } else {
        ShowWindow(g_hwnd, SW_HIDE);
        g_hidden_at = GetTickCount();
        g_confirm[0] = 0; g_hover_row = -1;
        SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
    }
}
static void quit_app(void) {
    if (g_tray_ok) { Shell_NotifyIconW(NIM_DELETE, &g_nid); Shell_NotifyIconW(NIM_DELETE, &g_nid2); }
    DestroyWindow(g_hwnd);
}
static void lang_menu(void) {
    HMENU m = CreatePopupMenu();
    for (int k = 0; k < i18n_count(); k++)
        AppendMenuW(m, MF_STRING | (k == g_lang ? MF_CHECKED : 0), 100 + k, (const WCHAR *)i18n_locale(k)->name);
    POINT p;
    GetCursorPos(&p);
    g_in_menu = 1;
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | (L->rtl ? TPM_LAYOUTRTL : 0), p.x, p.y, 0, g_hwnd, 0);
    g_in_menu = 0;
    DestroyMenu(m);
    if (cmd >= 100) set_lang_pref(cmd - 100, 1);
}
static void activate(int act, int arg) {
    switch (act) {
    case X_CLOSE: if (g_pinned) quit_app(); else show_flyout(0); return;
    case X_LANG: set_lang_pref(arg != g_lang ? arg : (g_lang + 1) % i18n_count(), 1); return;
    case X_LANGPICK: lang_picker(); return;
    case X_LANGMENU: lang_menu(); return;
    case X_SETTINGS: if (g_view == V_SETTINGS) open_view(V_NONE, 0); else open_view(V_SETTINGS, 0); break;
    case X_CARD: open_view(arg, 0); break;
    case X_SORT: g_sort = arg; g_hover_row = -1; break;
    case X_ROW: if (arg < g_nsorted) open_view(V_APP, g_sorted[arg]->name); break;
    case X_END: if (arg < g_nsorted) request_end(g_sorted[arg]->name); break;
    case X_HOG_END: { AppGroup *h = hog_app(); if (h) request_end(h->name); break; }
    case X_HOG_DISMISS: { AppGroup *h = hog_app(); if (h) lstrcpynW(g_dismissed, h->name, 64); break; }
    case X_CONFIRM_YES: confirm_end(); break;
    case X_CONFIRM_NO: g_confirm[0] = 0; break;
    case X_BACK: open_view(V_NONE, 0); break;
    case X_END_CURRENT: request_end(g_view_app); break;
    case X_MONITOR:
        ShellExecuteW(0, L"open", L"taskmgr.exe", 0, 0, SW_SHOWNORMAL);
        show_toast(tr1(S_toastMonitor, "monitor", hw(H_taskManager)));
        break;
    case X_QUIT: quit_app(); return;
    case X_SWITCH:
        if (arg == 0) { g_sim_hog = !g_sim_hog; g_dismissed[0] = 0; }
        else if (arg == 1) g_sim_charging = !g_sim_charging;
        else g_live = !g_live;
        tel_sample(&g_tel, g_sim_hog, g_sim_charging);
        break;
    case X_UNIT: g_unit_f = arg; break;
    case X_RESTORE: if (tel_ended_count()) { tel_restore(); show_toast(tr(S_toastRestored)); tel_sample(&g_tel, g_sim_hog, g_sim_charging); } break;
    default: return;
    }
    refresh();
}
static int hit_at(int x, int y) {
    for (int k = g_nhits - 1; k >= 0; k--) {
        Hit *h = &g_hits[k];
        if (x >= h->x && x < h->x + h->w && y >= h->y && y < h->y + h->h) return k;
    }
    return -1;
}

/* ------------------------------------------------------------------------ */
/* Script (for automated screenshots)                                        */
/* ------------------------------------------------------------------------ */
static char g_script[64][256];
static int g_nscript, g_script_i;
static DWORD g_script_wait_until;
static const char *VIEW_NAMES[] = { "none", "cpu", "mem", "nrg", "thm", "gpu", "ssd", "net", "app", "settings" };

static void report_memory(const char *path) {
    PROCESS_MEMORY_COUNTERS pmc;
    pmc.cb = sizeof pmc;
    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc);
    FILE *f = fopen(path, "a");
    if (!f) return;
    fprintf(f, "working_set_kb=%lu peak_working_set_kb=%lu pagefile_kb=%lu\n", (unsigned long)(pmc.WorkingSetSize / 1024),
            (unsigned long)(pmc.PeakWorkingSetSize / 1024), (unsigned long)(pmc.PagefileUsage / 1024));
    fclose(f);
}
/* Grabs the screen around the flyout, popup menus included. */
static void screen_capture(const WCHAR *path) {
    RECT wr;
    GetWindowRect(g_hwnd, &wr);
    int x0 = wr.left - 40, y0 = wr.top - 10, w = (wr.right - wr.left) + 80, h = (wr.bottom - wr.top) + 20;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x0 + w > sw) w = sw - x0;
    if (y0 + h > sh) h = sh - y0;
    HDC sdc = GetDC(0), mdc = CreateCompatibleDC(sdc);
    BITMAPINFO bi;
    void *bits;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    HBITMAP bm = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, &bits, 0, 0);
    HGDIOBJ old = SelectObject(mdc, bm);
    BitBlt(mdc, 0, 0, w, h, sdc, x0, y0, SRCCOPY);
    GdiFlush();
    for (int i = 0; i < w * h; i++) ((DWORD *)bits)[i] |= 0xFF000000;
    save_png(path, bits, w, h, w * 4, PixelFormat32bppARGB);
    SelectObject(mdc, old);
    DeleteObject(bm);
    DeleteDC(mdc);
    ReleaseDC(0, sdc);
}
static void script_step(void) {
    while (g_script_i < g_nscript && GetTickCount() >= g_script_wait_until) {
        char *c = g_script[g_script_i++];
        char a[256] = "", b[256] = "";
        sscanf(c, "%255s %255[^\n]", a, b);
        WCHAR wb[256];
        MultiByteToWideChar(CP_UTF8, 0, b, -1, wb, 256);
        if (!strcmp(a, "wait")) { g_script_wait_until = GetTickCount() + (DWORD)(atof(b) * 1000); return; }
        else if (!strcmp(a, "capture")) capture(wb);
        else if (!strcmp(a, "lang")) set_lang(i18n_resolve(b));
        else if (!strcmp(a, "langpref")) set_lang_pref(!strcmp(b, "system") ? -1 : i18n_resolve(b), 1);
        else if (!strcmp(a, "langpicker")) PostMessageW(g_hwnd, WM_LANGPICK, 0, 0);
        else if (!strcmp(a, "endmenu")) EndMenu();
        else if (!strcmp(a, "screenshot")) screen_capture(wb);
        else if (!strcmp(a, "view")) {
            char v[64] = "", app[192] = "";
            sscanf(b, "%63s %191[^\n]", v, app);
            for (int k = 0; k < 10; k++) if (!strcmp(v, VIEW_NAMES[k])) {
                WCHAR wa[192];
                MultiByteToWideChar(CP_UTF8, 0, app, -1, wa, 192);
                if (k == V_APP && !app[0] && g_nsorted) lstrcpynW(wa, g_sorted[0]->name, 192);
                open_view(k, wa);
            }
        }
        else if (!strcmp(a, "hover-row")) g_hover_row = atoi(b);
        else if (!strcmp(a, "chart")) g_chart = atoi(b);
        else if (!strcmp(a, "sort")) g_sort = atoi(b);
        else if (!strcmp(a, "unit")) g_unit_f = b[0] == 'f';
        else if (!strcmp(a, "simhog")) { g_sim_hog = atoi(b); tel_sample(&g_tel, g_sim_hog, g_sim_charging); }
        else if (!strcmp(a, "seed")) tel_seed_history(&g_tel);
        else if (!strcmp(a, "confirm")) { AppGroup *h = hog_app(); if (h) request_end(h->name); }
        else if (!strcmp(a, "cancel")) g_confirm[0] = 0;
        else if (!strcmp(a, "trim")) SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
        else if (!strcmp(a, "memreport")) report_memory(b);
        else if (!strcmp(a, "tray")) {
            DWORD px[256];
            HICON ic = make_tray_icon(px);
            DestroyIcon(ic);
            save_png(wb, px, 16, 16, 64, PixelFormat32bppPARGB);
        }
        else if (!strcmp(a, "traypct")) {
            DWORD px[256];
            HICON ic = make_pct_icon(px);
            DestroyIcon(ic);
            save_png(wb, px, 16, 16, 64, PixelFormat32bppPARGB);
        }
        else if (!strcmp(a, "quit")) { quit_app(); return; }
        refresh();
    }
}

/* ------------------------------------------------------------------------ */
/* Window procedure                                                         */
/* ------------------------------------------------------------------------ */
static void poll(void) {
    if (g_live) tel_sample(&g_tel, g_sim_hog, g_sim_charging);
    AppGroup *h = hog_app();
    if (!h) g_dismissed[0] = 0;
    tray_update(0);
    if (g_visible) refresh();
}

static LRESULT CALLBACK wnd_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    if (m == g_msg_taskbar && g_msg_taskbar) { tray_update(1); return 0; }
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        if (g_mdc) {
            /* the back buffer already holds mirrored pixels; blit it 1:1 */
            DWORD old = SetLayout(dc, 0);
            BitBlt(dc, 0, 0, WIN_W, g_win_h, g_mdc, 0, 0, SRCCOPY);
            SetLayout(dc, old);
        }
        EndPaint(w, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_TIMER:
        if (wp == TIMER_POLL) poll();
        else if (wp == TIMER_TOAST) { KillTimer(w, TIMER_TOAST); g_toast[0] = 0; refresh(); }
        else if (wp == TIMER_SCRIPT) script_step();
        return 0;
    case WM_MOUSEMOVE: {
        /* WS_EX_LAYOUTRTL windows report x from the right edge, which is the
           logical x this layout uses */
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        int k = hit_at(x, y);
        int row = -1, chart = -1;
        for (int i = 0; i < g_nhits; i++) {
            Hit *h = &g_hits[i];
            if (x >= h->x && x < h->x + h->w && y >= h->y && y < h->y + h->h) {
                if (h->act == X_ROW || h->act == X_END) row = h->arg;
                if (h->act == X_CHART) chart = (int)floorf((x - h->x) / h->w * (HIST_N - 1) + 0.5f);
            }
        }
        if (k != g_hover || row != g_hover_row || chart != g_chart) {
            g_hover = k; g_hover_row = g_view == V_NONE ? row : -1; g_chart = chart;
            refresh();
        }
        TRACKMOUSEEVENT tme = { sizeof tme, TME_LEAVE, w, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        g_hover = -1; g_hover_row = -1; g_chart = -1;
        refresh();
        return 0;
    case WM_LBUTTONUP: {
        int k = hit_at(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (k >= 0) { g_focus = -1; activate(g_hits[k].act, g_hits[k].arg); }
        return 0;
    }
    case WM_LBUTTONDOWN:
        if (GET_Y_LPARAM(lp) < TB && hit_at(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)) < 0 && g_pinned) {
            ReleaseCapture();
            SendMessageW(w, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        }
        return 0;
    case WM_MOUSEWHEEL: {
        int d = GET_WHEEL_DELTA_WPARAM(wp);
        int mx = g_content_h - g_win_h;
        if (mx > 0) {
            g_scroll -= d / 120 * 40;
            if (g_scroll < 0) g_scroll = 0;
            if (g_scroll > mx) g_scroll = mx;
            refresh();
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_TAB) {
            int dir = GetKeyState(VK_SHIFT) < 0 ? -1 : 1, n = g_nhits;
            for (int i = 1; i <= n; i++) {
                int k = ((g_focus < 0 ? (dir > 0 ? -1 : 0) : g_focus) + dir * i + n * 2) % n;
                if (g_hits[k].focus) { g_focus = k; break; }
            }
            refresh();
        } else if ((wp == VK_RETURN || wp == VK_SPACE) && g_focus >= 0 && g_focus < g_nhits) {
            Hit h = g_hits[g_focus];
            activate(h.act, h.arg);
        } else if (wp == VK_ESCAPE) {
            if (g_confirm[0]) { g_confirm[0] = 0; refresh(); }
            else if (g_view != V_NONE) { open_view(V_NONE, 0); refresh(); }
            else if (!g_pinned) show_flyout(0);
        }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE && !g_pinned && g_visible && !g_in_menu) show_flyout(0);
        return 0;
    case WM_THEMECHANGED: theme_open(); refresh(); return 0;
    case WM_LANGPICK: lang_picker(); return 0;
    case WM_TRAY:
        /* a click on the icon first deactivates an open flyout; do not reopen it */
        if (lp == WM_LBUTTONUP) { if (g_visible || GetTickCount() - g_hidden_at > 400) show_flyout(!g_visible); }
        else if (lp == WM_RBUTTONUP) {
            HMENU mn = CreatePopupMenu();
            AppendMenuW(mn, MF_STRING, 1, tr(S_openPulse));
            AppendMenuW(mn, MF_STRING, 2, tr(S_quit));
            POINT p;
            GetCursorPos(&p);
            SetForegroundWindow(w);
            int cmd = TrackPopupMenu(mn, TPM_RETURNCMD | TPM_RIGHTBUTTON | (L->rtl ? TPM_LAYOUTRTL : 0), p.x, p.y, 0, w, 0);
            DestroyMenu(mn);
            if (cmd == 1) show_flyout(1);
            else if (cmd == 2) quit_app();
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(w, m, wp, lp);
}

/* ------------------------------------------------------------------------ */
/* Self-test (the i18n unit tests against the embedded STRINGTABLE)           */
/* ------------------------------------------------------------------------ */
extern "C" {
int run_vectors(const char *vec_path);
int run_extra(void);
extern int g_fail, g_pass;
}

static void read_cpu_model(void) {
    HKEY k;
    g_cpu_model[0] = 0;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &k) == ERROR_SUCCESS) {
        WCHAR buf[128];
        DWORD sz = sizeof buf, type;
        if (RegQueryValueExW(k, L"ProcessorNameString", 0, &type, (BYTE *)buf, &sz) == ERROR_SUCCESS && type == REG_SZ) {
            buf[127] = 0;
            WCHAR *p = buf;
            while (*p == L' ') p++;
            lstrcpynW(g_cpu_model, p, 96);
        }
        RegCloseKey(k);
    }
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int) {
    int argc;
    WCHAR **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    char lang[32] = "", view[32] = "", script[MAX_PATH] = "", selftest[MAX_PATH] = "", selfout[MAX_PATH] = "";
    g_inst = inst;
    for (int i = 1; i < argc; i++) {
        char a[MAX_PATH] = "", b[MAX_PATH] = "";
        WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, a, MAX_PATH, 0, 0);
        if (i + 1 < argc) WideCharToMultiByte(CP_UTF8, 0, argv[i + 1], -1, b, MAX_PATH, 0, 0);
        if (!strcmp(a, "--show-flyout")) g_pinned = 1;
        else if (!strcmp(a, "--simulate-hog")) g_sim_hog = 1;
        else if (!strcmp(a, "--seed-history")) g_seed = 1;
        else if (!strcmp(a, "--lang") && b[0]) { strcpy(lang, b); i++; }
        else if (!strcmp(a, "--view") && b[0]) { strcpy(view, b); i++; }
        else if (!strcmp(a, "--script") && b[0]) { strcpy(script, b); i++; }
        else if (!strcmp(a, "--selftest") && b[0]) {
            strcpy(selftest, b); i++;
            if (i + 1 < argc) { WideCharToMultiByte(CP_UTF8, 0, argv[i + 1], -1, selfout, MAX_PATH, 0, 0); i++; }
        }
    }
    LocalFree(argv);

    if (!i18n_init(res_lookup)) { MessageBoxW(0, L"No locales in the catalog.", L"Pulse", MB_ICONERROR); return 1; }

    if (selftest[0]) {
        if (selfout[0]) freopen(selfout, "w", stdout);
        printf("locales: %d (embedded STRINGTABLE)\n", i18n_count());
        run_vectors(selftest);
        run_extra();
        printf("%d passed, %d failed\n", g_pass, g_fail);
        fflush(stdout);
        return g_fail ? 1 : 0;
    }

    /* --lang wins for this run; else the saved choice; else the system */
    g_lang_pref = load_lang_pref();
    if (lang[0]) g_lang = i18n_resolve(lang);
    else g_lang = g_lang_pref < 0 ? system_lang() : g_lang_pref;
    L = i18n_locale(g_lang);

    GdiplusStartupInput gsi;
    GdiplusStartup(&g_gdip, &gsi, 0);
    g_measure_dc = CreateCompatibleDC(0);
    fonts_reset();
    read_cpu_model();
    tel_init(&g_tel);
    tel_sample(&g_tel, g_sim_hog, g_sim_charging);

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"PulseFlyout";
    wc.style = CS_DROPSHADOW;
    RegisterClassExW(&wc);
    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | (L->rtl ? WS_EX_LAYOUTRTL : 0), L"PulseFlyout", L"Pulse",
                             WS_POPUP, 0, 0, WIN_W, 700, 0, 0, inst, 0);
    theme_open();
    g_msg_taskbar = RegisterWindowMessageW(L"TaskbarCreated");
    if (view[0]) for (int k = 0; k < 10; k++) if (!strcmp(view, VIEW_NAMES[k])) open_view(k, 0);
    SetTimer(g_hwnd, TIMER_POLL, 1500, 0);
    if (!g_pinned) tray_update(1);
    else show_flyout(1);
    if (g_seed) { Sleep(300); tel_sample(&g_tel, g_sim_hog, g_sim_charging); tel_seed_history(&g_tel); refresh(); }
    if (script[0]) {
        FILE *f = fopen(script, "r");
        if (f) {
            while (g_nscript < 64 && fgets(g_script[g_nscript], 256, f)) {
                g_script[g_nscript][strcspn(g_script[g_nscript], "\r\n")] = 0;
                if (g_script[g_nscript][0] && g_script[g_nscript][0] != '#') g_nscript++;
            }
            fclose(f);
        }
        SetTimer(g_hwnd, TIMER_SCRIPT, 100, 0);
    }
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);

    MSG msg;
    while (GetMessageW(&msg, 0, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    GdiplusShutdown(g_gdip);
    return 0;
}
