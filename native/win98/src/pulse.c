/*
 * Pulse for Windows 98 SE: tray icon plus a 420 px classic flyout.
 * C89, Win32 ANSI APIs, GDI only. See README.md for the data sources.
 */
#include <windows.h>
#include <shellapi.h>
#include "i18n.h"
#include "text.h"
#include "telemetry.h"

/* ---- constants -------------------------------------------------------------- */

#define FLY_W       420
#define CAP_H       18
#define FRAME       3
#define PAD         7
#define STRIP_H     96
#define HIST_N      121     /* 10 minutes at 5 s */
#define SPARK_N     40      /* 1 minute at 1.5 s */
#define TICK_MS     1500
#define HIST_MS     5000
#define HOG_MS      120000UL
#define WM_TRAY     (WM_APP + 1)
#define ID_TIMER    1
#define ID_TRAY     1      /* wave */
#define ID_TRAY_PCT 2      /* CPU percent digits */
#define MAX_HOT     64

/* Win98 classic tokens (prototype CLASSIC.w98) */
#define C_INK       RGB(0x00, 0x00, 0x00)
#define C_INK2      RGB(0x20, 0x20, 0x20)
#define C_INK3      RGB(0x40, 0x40, 0x40)
#define C_TRACK     RGB(0xFF, 0xFF, 0xFF)
#define C_HAIR      RGB(0x80, 0x80, 0x80)
#define C_CPU       RGB(0x00, 0x8A, 0x45)
#define C_MEM       RGB(0x00, 0x50, 0xC8)
#define C_NRG       RGB(0xC8, 0x8A, 0x00)
#define C_THM       RGB(0xC8, 0x10, 0x2E)
#define C_GPU       RGB(0x00, 0x80, 0x80)
#define C_WARN      RGB(0xF2, 0x7B, 0x1E)
#define C_CRIT      RGB(0xD0, 0x00, 0x00)
#define C_CPU_INK   RGB(0x00, 0x5A, 0x2B)
#define C_MEM_INK   RGB(0x00, 0x00, 0x80)
#define C_NRG_INK   RGB(0x6B, 0x45, 0x00)
#define C_THM_INK   RGB(0x8B, 0x00, 0x20)
#define C_GPU_INK   RGB(0x00, 0x55, 0x55)
#define C_WARN_INK  RGB(0x7A, 0x2E, 0x00)
#define C_CRIT_INK  RGB(0x8B, 0x00, 0x00)
#define C_HOG_BG    RGB(0xE8, 0xD2, 0xBC)
#define C_SSD       RGB(0x00, 0x00, 0x80)
#define C_TIP       RGB(0xFF, 0xFF, 0xE1)
#define C_SEG       RGB(0xDF, 0xDF, 0xDF)

enum { V_MAIN, V_DETAIL, V_SETTINGS };
enum { D_CPU, D_MEM, D_NRG, D_THM, D_GPU, D_SSD, D_NET, D_APP };
enum { A_NONE, A_CLOSE, A_LANG, A_LANGMENU, A_SETTINGS, A_CARD, A_BACK, A_SORT, A_ROW,
       A_ENDHOG, A_DISMISS, A_YES, A_NO, A_MONITOR, A_QUIT, A_ENDAPP, A_LIVE, A_UNIT,
       A_SIMHOG, A_SIMCHG, A_RESTORE, A_LANGSET };
enum { S_CPU, S_MEM, S_GPU };

typedef struct { RECT r; int act, arg; } Hot;
typedef struct { char exe[TM_NAME]; float v[HIST_N]; int n; DWORD used; } AppHist;

/* ---- state ------------------------------------------------------------------- */

static HINSTANCE g_inst;
static HWND g_wnd, g_sb;
static Telemetry T;
static const I18nLocale *LC;
static int g_rtl, g_ansi;
static HFONT f_ui, f_bold, f_title, f_brand, f_hero, f_small, f_tile, f_cap;
static HFONT f_lang[8];
static int g_lang_ok[8];
static UINT g_taskbar_msg;
static int g_tray, g_show_flyout_only;
static HICON g_tray_icon, g_tray_pct, g_app_icon;
static DWORD g_hidden_at;

static int g_view = V_MAIN, g_detail = D_CPU, g_sort = S_CPU, g_hover = -1;
static char g_detail_exe[TM_NAME];
static int g_scroll, g_content_h, g_sb_on;
static int g_live = 1, g_unit_f = 0;
static int g_lang_system = 1;      /* follow the Windows UI language */
static HFONT f_lname[8];
/* Settings demo overlays on top of real data (Part 1.3) */
static int g_sim_hog, g_sim_charge, g_sim_ended;
#define SIM_EXE "NAVAPW32.EXE"
typedef struct { char exe[TM_NAME]; char path[MAX_PATH]; int sim; } Ended;
static Ended g_ended[12];
static int g_nended;

static float h_cpu[HIST_N], h_mem[HIST_N], h_nrg[HIST_N], h_thm[HIST_N], h_gpu[HIST_N];
static float h_rd[HIST_N], h_wr[HIST_N], h_dn[HIST_N], h_up[HIST_N];
static int h_n;
static float s_cpu[SPARK_N], s_mem[SPARK_N], s_nrg[SPARK_N], s_thm[SPARK_N];
static int s_n;
static AppHist g_ah[8];
static DWORD g_last_tick, g_last_hist;

static double g_hog_pct = 50.0;   /* --hog-pct=N changes it */
static char g_hog_exe[TM_NAME];
static DWORD g_hog_since;
static int g_hog_on, g_hog_dismissed;
static int g_confirm;
static char g_confirm_exe[TM_NAME];
static char g_toast[256];
static DWORD g_toast_until;

static char g_rows[5][TM_NAME];
static int g_nrows;

/* Render context */
static HDC D;
static RECT CA;            /* content area, client coords */
static int VW;             /* content width used for mirroring */
static int g_measure, g_register;
static Hot g_hot[MAX_HOT];
static int g_nhot;

/* ---- small string helpers ------------------------------------------------------ */

static int s_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void s_cpy(char *d, const char *s, int cap) { int k = 0; while (s && s[k] && k < cap - 1) { d[k] = s[k]; k++; } d[k] = 0; }
static void s_cat(char *d, const char *s, int cap) { int n = s_len(d); s_cpy(d + n, s, cap - n); }
static int s_eq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int s_ieq(const char *a, const char *b)
{
    while (*a && *b) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
        a++; b++;
    }
    return *a == *b;
}
static const char *s_find(const char *h, const char *n)
{
    int k;
    for (; *h; h++) { for (k = 0; n[k] && h[k] == n[k]; k++) ; if (!n[k]) return h; }
    return 0;
}

/* ---- i18n shortcuts -------------------------------------------------------------- */

#define SB 256
typedef char Str[SB];

static char *T0(const char *key, char *out) { return i18n_t(LC, key, 0, 0, out, SB); }
static char *T1(const char *key, const char *n1, const char *v1, char *out)
{
    I18nParam p[1]; p[0].name = n1; p[0].value = v1;
    return i18n_t(LC, key, p, 1, out, SB);
}
static char *T2(const char *key, const char *n1, const char *v1, const char *n2, const char *v2, char *out)
{
    I18nParam p[2]; p[0].name = n1; p[0].value = v1; p[1].name = n2; p[1].value = v2;
    return i18n_t(LC, key, p, 2, out, SB);
}
static char *T3(const char *key, const char *n1, const char *v1, const char *n2, const char *v2,
                const char *n3, const char *v3, char *out)
{
    I18nParam p[3]; p[0].name = n1; p[0].value = v1; p[1].name = n2; p[1].value = v2; p[2].name = n3; p[2].value = v3;
    return i18n_t(LC, key, p, 3, out, SB);
}
static char *H0(const char *key, char *out) { return i18n_hw(LC, key, 0, 0, out, SB); }
static char *H1(const char *key, const char *n1, const char *v1, char *out)
{
    I18nParam p[1]; p[0].name = n1; p[0].value = v1;
    return i18n_hw(LC, key, p, 1, out, SB);
}
static char *J2(const char *a, const char *b, char *out)
{
    const char *it[2]; it[0] = a; it[1] = b;
    return i18n_join(LC, it, 2, out, SB);
}
static char *J3(const char *a, const char *b, const char *c, char *out)
{
    const char *it[3]; it[0] = a; it[1] = b; it[2] = c;
    return i18n_join(LC, it, 3, out, SB);
}
static char *N(double x, int d, char *out) { return i18n_num(LC, x, d, out, SB); }
static char *PCT(double x, int d, char *out) { N(x, d, out); s_cat(out, "%", SB); return out; }
static char *TEMP(double c, int d, char *out)
{
    if (g_unit_f) { N(c * 9.0 / 5.0 + 32.0, d, out); s_cat(out, "\xC2\xB0" "F", SB); }
    else { N(c, d, out); s_cat(out, "\xC2\xB0" "C", SB); }
    return out;
}
static char *BYTES(double b, char *out)
{
    double mb = b / 1048576.0;
    if (mb < 1024.0) { N(mb, mb < 10 ? 1 : 0, out); s_cat(out, " MB", SB); }
    else { N(mb / 1024.0, mb < 10240 ? 2 : 1, out); s_cat(out, " GB", SB); }
    return out;
}
static char *RATE(double bps, char *out)
{
    if (bps < 1048576.0) { N(bps / 1024.0, 1, out); s_cat(out, " KB/s", SB); }
    else { N(bps / 1048576.0, 1, out); s_cat(out, " MB/s", SB); }
    return out;
}
static char *DUR(int secs, char *out)
{
    char h[24], m[24];
    N((double)(secs / 3600), 0, h);
    N((double)((secs % 3600) / 60), 0, m);
    return secs >= 3600 ? T2("durHM", "h", h, "m", m, out) : T1("durM", "m", m, out);
}
static char *PIDHEX(DWORD pid, char *out) { wsprintfA(out, "PID %08lX", (unsigned long)pid); return out; }

/* ---- drawing primitives (content coords, mirrored in RTL) --------------------------- */

static RECT RR(int x, int y, int w, int h)
{
    RECT r;
    int dx = g_rtl ? VW - x - w : x;
    r.left = CA.left + dx;
    r.right = r.left + w;
    r.top = CA.top + y - g_scroll;
    r.bottom = r.top + h;
    return r;
}
static int PX(int x) { return CA.left + (g_rtl ? VW - x : x); }
static int PY(int y) { return CA.top + y - g_scroll; }

static void hot(int x, int y, int w, int h, int act, int arg)
{
    if (!g_register || g_nhot >= MAX_HOT) return;
    g_hot[g_nhot].r = RR(x, y, w, h);
    g_hot[g_nhot].act = act;
    g_hot[g_nhot].arg = arg;
    g_nhot++;
}

static void fill_r(const RECT *r, COLORREF c)
{
    HBRUSH b;
    if (g_measure) return;
    b = CreateSolidBrush(c);
    FillRect(D, r, b);
    DeleteObject(b);
}
static void fill(int x, int y, int w, int h, COLORREF c) { RECT r = RR(x, y, w, h); fill_r(&r, c); }

static void edge(int x, int y, int w, int h, UINT kind)
{
    RECT r = RR(x, y, w, h);
    if (g_measure) return;
    DrawEdge(D, &r, kind, BF_RECT);
}
static void raised(int x, int y, int w, int h) { edge(x, y, w, h, EDGE_RAISED); }
static void sunken(int x, int y, int w, int h) { edge(x, y, w, h, EDGE_SUNKEN); }

static void frame1(int x, int y, int w, int h, COLORREF c)
{
    RECT r = RR(x, y, w, h);
    HBRUSH b;
    if (g_measure) return;
    b = CreateSolidBrush(c);
    FrameRect(D, &r, b);
    DeleteObject(b);
}

static void line(int x1, int y1, int x2, int y2, COLORREF c, int width)
{
    HPEN p, o;
    if (g_measure) return;
    p = CreatePen(PS_SOLID, width, c);
    o = (HPEN)SelectObject(D, p);
    MoveToEx(D, PX(x1), PY(y1), 0);
    LineTo(D, PX(x2), PY(y2));
    SelectObject(D, o);
    DeleteObject(p);
}

static void polyline(const int *xy, int n, COLORREF c, int width)
{
    POINT pt[12];
    HPEN p, o;
    int k;
    if (g_measure || n > 12) return;
    for (k = 0; k < n; k++) { pt[k].x = PX(xy[k * 2]); pt[k].y = PY(xy[k * 2 + 1]); }
    p = CreatePen(PS_SOLID, width, c);
    o = (HPEN)SelectObject(D, p);
    Polyline(D, pt, n);
    SelectObject(D, o);
    DeleteObject(p);
}

static void polygon(const int *xy, int n, COLORREF c)
{
    POINT pt[12];
    HBRUSH b, ob;
    HPEN op;
    int k;
    if (g_measure || n > 12) return;
    for (k = 0; k < n; k++) { pt[k].x = PX(xy[k * 2]); pt[k].y = PY(xy[k * 2 + 1]); }
    b = CreateSolidBrush(c);
    ob = (HBRUSH)SelectObject(D, b);
    op = (HPEN)SelectObject(D, GetStockObject(NULL_PEN));
    Polygon(D, pt, n);
    SelectObject(D, ob);
    SelectObject(D, op);
    DeleteObject(b);
}

static void ellipse(int cx, int cy, int r, COLORREF fillc, COLORREF pen, int width)
{
    HBRUSH b, ob;
    HPEN p, op;
    if (g_measure) return;
    b = fillc == (COLORREF)-1 ? (HBRUSH)GetStockObject(NULL_BRUSH) : CreateSolidBrush(fillc);
    p = CreatePen(PS_SOLID, width, pen);
    ob = (HBRUSH)SelectObject(D, b);
    op = (HPEN)SelectObject(D, p);
    Ellipse(D, PX(cx) - r, PY(cy) - r, PX(cx) + r + 1, PY(cy) + r + 1);
    SelectObject(D, ob);
    SelectObject(D, op);
    if (fillc != (COLORREF)-1) DeleteObject(b);
    DeleteObject(p);
}

static int text_w(HFONT f, const char *s)
{
    HFONT o = (HFONT)SelectObject(D, f);
    int w = txt_width(D, s);
    SelectObject(D, o);
    return w;
}

static int text(int x, int y, int w, int h, const char *s, HFONT f, COLORREF c, int align)
{
    RECT r = RR(x, y, w, h);
    HFONT o;
    int dw;
    if (g_measure) return text_w(f, s) < w ? text_w(f, s) : w;
    o = (HFONT)SelectObject(D, f);
    dw = txt_draw(D, &r, s, align, c);
    SelectObject(D, o);
    return dw;
}

/* Disabled (etched) text, as classic Windows draws grayed labels. */
static void text_etched(int x, int y, int w, int h, const char *s, HFONT f, int align)
{
    text(x + (g_rtl ? -1 : 1), y + 1, w, h, s, f, GetSysColor(COLOR_3DHILIGHT), align);
    text(x, y, w, h, s, f, GetSysColor(COLOR_GRAYTEXT), align);
}

static COLORREF blend(COLORREF a, COLORREF b, double t)   /* t of a over b */
{
    return RGB((int)(GetRValue(a) * t + GetRValue(b) * (1 - t)),
               (int)(GetGValue(a) * t + GetGValue(b) * (1 - t)),
               (int)(GetBValue(a) * t + GetBValue(b) * (1 - t)));
}

/* Horizontal bar: white track with a coloured fill from the start side. */
static void bar(int x, int y, int w, int h, double frac, COLORREF c)
{
    int fw;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    fill(x, y, w, h, C_TRACK);
    fw = (int)(w * frac + 0.5);
    if (fw < 2) fw = 2;
    fill(x, y, fw, h, c);
}

/* Bar with a warn->crit gradient fill, used for the hog row. */
static void bar_grad(int x, int y, int w, int h, double frac)
{
    int k, fw;
    fill(x, y, w, h, C_TRACK);
    if (frac > 1) frac = 1;
    fw = (int)(w * frac + 0.5);
    for (k = 0; k < fw; k++) fill(x + k, y, 1, h, blend(C_CRIT, C_WARN, fw > 1 ? (double)k / (fw - 1) : 0));
}

static void chevron(int x, int y, int fwd, COLORREF c)
{
    int xy[6];
    if (fwd) { xy[0] = x; xy[1] = y; xy[2] = x + 4; xy[3] = y + 4; xy[4] = x; xy[5] = y + 8; }
    else { xy[0] = x + 4; xy[1] = y; xy[2] = x; xy[3] = y + 4; xy[4] = x + 4; xy[5] = y + 8; }
    polyline(xy, 3, c, 2);
}

static void arrow(int x, int y, int down, COLORREF c)
{
    int xy[6];
    fill(x + 2, down ? y : y + 3, 2, 6, c);
    if (down) { xy[0] = x - 1; xy[1] = y + 5; xy[2] = x + 7; xy[3] = y + 5; xy[4] = x + 3; xy[5] = y + 9; }
    else { xy[0] = x - 1; xy[1] = y + 4; xy[2] = x + 7; xy[3] = y + 4; xy[4] = x + 3; xy[5] = y; }
    polygon(xy, 3, c);
}

/* Small line icons, 14x14, drawn in the accent colour. */
static void icon(int kind, int x, int y, COLORREF c)
{
    int k;
    switch (kind) {
    case D_CPU:
        frame1(x + 3, y + 3, 8, 8, c);
        fill(x + 5, y + 5, 4, 4, c);
        for (k = 0; k < 3; k++) {
            fill(x + 4 + k * 3, y + 1, 1, 2, c); fill(x + 4 + k * 3, y + 11, 1, 2, c);
            fill(x + 1, y + 4 + k * 3, 2, 1, c); fill(x + 11, y + 4 + k * 3, 2, 1, c);
        }
        break;
    case D_MEM:
        frame1(x + 1, y + 4, 12, 6, c);
        for (k = 0; k < 3; k++) fill(x + 3 + k * 3, y + 6, 2, 2, c);
        for (k = 0; k < 4; k++) fill(x + 2 + k * 3, y + 10, 1, 2, c);
        break;
    case D_NRG: {
        int xy[14];
        xy[0] = x + 8; xy[1] = y + 1; xy[2] = x + 3; xy[3] = y + 8; xy[4] = x + 7; xy[5] = y + 8;
        xy[6] = x + 5; xy[7] = y + 13; xy[8] = x + 11; xy[9] = y + 6; xy[10] = x + 7; xy[11] = y + 6;
        xy[12] = x + 8; xy[13] = y + 1;
        polyline(xy, 7, c, 1);
        break; }
    case D_THM:
        frame1(x + 5, y + 1, 4, 9, c);
        ellipse(x + 7, y + 11, 2, c, c, 1);
        fill(x + 6, y + 5, 2, 5, c);
        break;
    case D_GPU:
        frame1(x + 1, y + 3, 12, 8, c);
        ellipse(x + 6, y + 7, 2, (COLORREF)-1, c, 1);
        fill(x + 10, y + 5, 2, 4, c);
        break;
    case D_SSD:
        frame1(x + 2, y + 2, 10, 10, c);
        fill(x + 4, y + 4, 6, 3, c);
        fill(x + 9, y + 9, 2, 1, c);
        break;
    case D_NET:
        fill(x + 6, y + 10, 2, 2, c);
        { int a[6]; a[0] = x + 3; a[1] = y + 8; a[2] = x + 7; a[3] = y + 5; a[4] = x + 11; a[5] = y + 8; polyline(a, 3, c, 1); }
        { int a[6]; a[0] = x + 1; a[1] = y + 5; a[2] = x + 7; a[3] = y + 1; a[4] = x + 13; a[5] = y + 5; polyline(a, 3, c, 1); }
        break;
    default: break;
    }
}

/* Catmull-Rom points to Bezier control points, as the prototype's smooth(). */
static int bezier_points(const double *px, const double *py, int n, POINT *out)
{
    int i, k = 0;
    out[k].x = (LONG)(px[0] + 0.5); out[k].y = (LONG)(py[0] + 0.5); k++;
    for (i = 0; i < n - 1; i++) {
        int i0 = i > 0 ? i - 1 : i, i3 = i + 2 < n ? i + 2 : i + 1;
        out[k].x = (LONG)(px[i] + (px[i + 1] - px[i0]) / 6 + 0.5);
        out[k].y = (LONG)(py[i] + (py[i + 1] - py[i0]) / 6 + 0.5); k++;
        out[k].x = (LONG)(px[i + 1] - (px[i3] - px[i]) / 6 + 0.5);
        out[k].y = (LONG)(py[i + 1] - (py[i3] - py[i]) / 6 + 0.5); k++;
        out[k].x = (LONG)(px[i + 1] + 0.5); out[k].y = (LONG)(py[i + 1] + 0.5); k++;
    }
    return k;
}

/* Area + line chart. vals newest last; drawn right-aligned over `slots` steps.
 * base_y is where the area closes (bottom, or the centre line for split charts). */
static void curve(const float *vals, int n, int slots, int x, int y, int w, int h,
                  double lo, double hi, int up, int base_y, COLORREF c, COLORREF area, int width)
{
    static double px[HIST_N], py[HIST_N];
    static POINT bz[HIST_N * 3 + 4];
    HPEN pen, op;
    HBRUSH br, ob;
    int k, nb;
    double step;
    if (g_measure || n < 2) return;
    if (hi - lo < 1e-9) hi = lo + 1;
    step = (double)w / (slots - 1);
    for (k = 0; k < n; k++) {
        double v = (vals[k] - lo) / (hi - lo), lx;
        if (v < 0) v = 0;
        if (v > 1) v = 1;
        lx = x + w - (n - 1 - k) * step;
        px[k] = g_rtl ? CA.left + VW - lx : CA.left + lx;
        py[k] = up ? PY(y) + h - v * h : PY(y) + v * h;
    }
    nb = bezier_points(px, py, n, bz);
    /* Area */
    br = CreateSolidBrush(area);
    ob = (HBRUSH)SelectObject(D, br);
    op = (HPEN)SelectObject(D, GetStockObject(NULL_PEN));
    BeginPath(D);
    MoveToEx(D, bz[0].x, bz[0].y, 0);
    PolyBezierTo(D, bz + 1, (DWORD)(nb - 1));
    LineTo(D, bz[nb - 1].x, PY(base_y));
    LineTo(D, bz[0].x, PY(base_y));
    CloseFigure(D);
    EndPath(D);
    FillPath(D);
    SelectObject(D, ob);
    DeleteObject(br);
    /* Line */
    pen = CreatePen(PS_SOLID, width, c);
    SelectObject(D, pen);
    PolyBezier(D, bz, (DWORD)nb);
    SelectObject(D, op);
    DeleteObject(pen);
}

static void minmax(const float *v, int n, double *mn, double *mx)
{
    int k;
    *mn = 1e30; *mx = -1e30;
    for (k = 0; k < n; k++) { if (v[k] < *mn) *mn = v[k]; if (v[k] > *mx) *mx = v[k]; }
    if (n == 0) { *mn = 0; *mx = 1; }
}

/* Card sparkline (prototype spark(): padding above and below the range). */
static void spark(const float *vals, int n, int x, int y, int w, int h, COLORREF c, const char *simtag)
{
    Str s;
    double mn, mx, rg, lo, hi;
    RECT clip;
    minmax(vals, n, &mn, &mx);
    rg = mx - mn; if (rg < 0.5) rg = 0.5;
    lo = mn - rg * 0.25; hi = mx + rg * 0.35;
    clip = RR(x, y, w, h);
    if (!g_measure) {
        SaveDC(D);
        IntersectClipRect(D, clip.left, clip.top, clip.right, clip.bottom);
        curve(vals, n, SPARK_N, x, y + 2, w, h - 4, lo, hi, 1, y + h, c, blend(c, GetSysColor(COLOR_3DFACE), 0.16), 1);
        RestoreDC(D, -1);
    }
    text(x, y - 12, w / 2, 12, T0("min1", s), f_small, C_INK3, TA_START_);
    if (simtag) text(x + w / 2, y - 12, w / 2, 12, simtag, f_small, C_INK3, TA_END_);
}

/* ---- data helpers ----------------------------------------------------------------- */

static void push(float *a, int cap, int *n, double v, int advance)
{
    int k;
    if (*n < cap) { a[*n] = (float)v; if (advance) (*n)++; return; }
    for (k = 0; k < cap - 1; k++) a[k] = a[k + 1];
    a[cap - 1] = (float)v;
}

static const TmApp *find_app(const char *exe)
{
    int k;
    for (k = 0; k < T.napps; k++) if (s_ieq(T.apps[k].exe, exe)) return &T.apps[k];
    return 0;
}

static AppHist *app_hist(const char *exe, int create)
{
    int k, oldest = 0;
    for (k = 0; k < 8; k++) if (g_ah[k].exe[0] && s_ieq(g_ah[k].exe, exe)) return &g_ah[k];
    if (!create) return 0;
    for (k = 1; k < 8; k++) if (g_ah[k].used < g_ah[oldest].used) oldest = k;
    s_cpy(g_ah[oldest].exe, exe, TM_NAME);
    g_ah[oldest].n = 0;
    return &g_ah[oldest];
}

static double app_metric(const TmApp *a, int sort)
{
    if (!T.proc_cpu) return a->threads;
    if (sort == S_MEM) return a->mem;
    return a->cpu;
}

static int thermal_level(double c) { return c < 48 ? 0 : c < 70 ? 1 : c < 90 ? 2 : 3; }

static int hog_active(void) { return g_hog_on && !g_hog_dismissed && find_app(g_hog_exe) != 0; }

static double mem_used(void)
{
    /* GlobalMemoryStatus caps both fields at 2 GB; fall back to the load figure. */
    double used = T.phys_total - T.phys_avail;
    if (used <= 0 || T.phys_avail >= T.phys_total) used = T.phys_total * T.mem_load / 100.0;
    return used;
}

static void mem_parts(double *app, double *kern, double *cache, double *freeb)
{
    double used = mem_used();
    *kern = T.kernel; *cache = T.cache;
    if (*kern + *cache > used) { *kern = 0; *cache = 0; }
    *app = used - *kern - *cache;
    *freeb = T.phys_total - used;
}

/* Kernel and disk cache sizes, or a dash when this host does not report them. */
static char *PART(double bytes, char *out)
{
    if (T.memdetail_src != SRC_LIVE) { s_cpy(out, "-", SB); return out; }
    return BYTES(bytes, out);
}

static char *cores_label(char *out)
{
    Str a, b;
    i18n_plural(LC, "core", T.ncpu, a, SB);
    acp_to_utf8(T.cpu_model, b, SB);
    return J2(a, b, out);
}

static char *app_name(const TmApp *a) { return (char *)i18n_app(LC, a->name); }

/* ---- shared header and footer -------------------------------------------------------- */

static int lang_pill_w(void) { return 33 * (i18n_count() < 2 ? 2 : i18n_count()); }

static void draw_lang(int x, int y)
{
    int n = i18n_count(), k;
    if (n <= 2) {
        int w = lang_pill_w();
        fill(x, y, w, 24, C_TRACK);
        sunken(x, y, w, 24);
        for (k = 0; k < n; k++) {
            const I18nLocaleData *d = i18n_list(k);
            /* Logical order of the pill follows the list; mirrored in RTL like the prototype. */
            int sx = x + 2 + k * ((w - 4) / n), sw = (w - 4) / n;
            int on = s_eq(d->code, LC->d->code);
            HFONT o;
            RECT r;
            if (on) { fill(sx, y + 2, sw, 20, C_SEG); raised(sx, y + 2, sw, 20); }
            r = RR(sx, y + 2, sw, 20);
            if (!g_measure && f_lang[k]) {
                txt_mode(g_ansi, d->codepage, d->rtl);
                o = (HFONT)SelectObject(D, f_lang[k]);
                if (g_lang_ok[k]) txt_draw(D, &r, d->label, TA_CENTER_, on ? C_INK : C_INK2);
                else {
                    RECT r2 = r;
                    OffsetRect(&r2, 1, 1);
                    txt_draw(D, &r2, d->label, TA_CENTER_, GetSysColor(COLOR_3DHILIGHT));
                    txt_draw(D, &r, d->label, TA_CENTER_, GetSysColor(COLOR_GRAYTEXT));
                }
                SelectObject(D, o);
                txt_mode(g_ansi, LC->d->codepage, LC->d->rtl);
            }
            hot(sx, y + 2, sw, 20, A_LANG, k);
        }
    } else {
        /* Three or more locales: compact menu button. */
        int w = 54;
        Str s;
        fill(x, y, w, 24, GetSysColor(COLOR_3DFACE));
        raised(x, y, w, 24);
        s_cpy(s, LC->d->label, SB);
        text(x + 6, y, w - 18, 24, s, f_bold, C_INK, TA_START_);
        { int xy[6]; xy[0] = x + w - 13; xy[1] = y + 10; xy[2] = x + w - 9; xy[3] = y + 14; xy[4] = x + w - 5; xy[5] = y + 10; polyline(xy, 3, C_INK2, 2); }
        hot(x, y, w, 24, A_LANGMENU, 0);
    }
}

static int header(int y)
{
    int L = PAD, R = VW - PAD, x, lw, hogA = hog_active();
    Str s, v, val;
    COLORREF ink;
    /* Logo badge */
    fill(L, y, 32, 32, GetSysColor(COLOR_3DFACE));
    raised(L, y, 32, 32);
    ellipse(L + 12, y + 16, 6, (COLORREF)-1, C_CPU, 2);
    ellipse(L + 19, y + 16, 6, (COLORREF)-1, C_MEM, 2);
    x = L + 40;
    x += text(x, y, 80, 32, "Pulse", f_brand, C_INK, TA_START_) + 12;
    /* Status */
    if (hogA) {
        const TmApp *a = find_app(g_hog_exe);
        T0("hogPill", v);
        PCT(a ? a->cpu : 0, 0, val);
        T2("pillValue", "label", v, "value", val, s);
        ink = C_WARN_INK;
    } else {
        T0("calm", v);
        PCT(T.cpu, 0, val);
        T2("pillValue", "label", v, "value", val, s);
        ink = C_CPU_INK;
    }
    ellipse(x + 3, y + 16, 3, hogA ? C_WARN : C_CPU, hogA ? C_WARN : C_CPU, 1);
    lw = lang_pill_w();
    text(x + 11, y, R - 28 - 6 - lw - 8 - (x + 11), 32, s, f_bold, ink, TA_START_);
    /* Language and settings */
    draw_lang(R - 28 - 6 - (i18n_count() <= 2 ? lw : 54), y + 4);
    fill(R - 28, y + 2, 28, 28, GetSysColor(COLOR_3DFACE));
    edge(R - 28, y + 2, 28, 28, g_view == V_SETTINGS ? EDGE_SUNKEN : EDGE_RAISED);
    {
        int bx = R - 28 + (g_view == V_SETTINGS ? 1 : 0), by = y + 2 + (g_view == V_SETTINGS ? 1 : 0);
        fill(bx + 7, by + 9, 14, 1, C_INK); fill(bx + 14, by + 7, 3, 5, C_INK);
        fill(bx + 7, by + 14, 14, 1, C_INK); fill(bx + 9, by + 12, 3, 5, C_INK);
        fill(bx + 7, by + 19, 14, 1, C_INK); fill(bx + 16, by + 17, 3, 5, C_INK);
    }
    hot(R - 28, y + 2, 28, 28, A_SETTINGS, 0);
    return y + 32;
}

static int button(int x, int y, int w, int h, const char *label, HFONT f, int act, int arg)
{
    fill(x, y, w, h, GetSysColor(COLOR_3DFACE));
    raised(x, y, w, h);
    text(x + 4, y, w - 8, h, label, f, C_INK, TA_CENTER_);
    hot(x, y, w, h, act, arg);
    return w;
}

static int footer(int y)
{
    int L = PAD, R = VW - PAD, bw, qw;
    Str s, m;
    H0(T.is_nt ? "taskManager" : "systemMonitor", m);
    T1("openMonitor", "monitor", m, s);
    bw = text_w(f_bold, s) + 34;
    if (bw > 190) bw = 190;
    fill(L, y, bw, 26, GetSysColor(COLOR_3DFACE));
    raised(L, y, bw, 26);
    text(L + 8, y, bw - 30, 26, s, f_bold, C_INK, TA_START_);
    /* external-link glyph */
    frame1(L + bw - 19, y + 9, 8, 8, C_INK2);
    { int xy[4]; xy[0] = L + bw - 15; xy[1] = y + 12; xy[2] = L + bw - 10; xy[3] = y + 7; polyline(xy, 2, C_INK2, 1); }
    fill(L + bw - 12, y + 7, 3, 1, C_INK2); fill(L + bw - 10, y + 7, 1, 3, C_INK2);
    hot(L, y, bw, 26, A_MONITOR, 0);
    T0("quit", s);
    qw = text_w(f_bold, s) + 12;
    text(R - qw, y, qw, 26, s, f_bold, C_INK, TA_END_);
    hot(R - qw, y, qw, 26, A_QUIT, 0);
    /* lock + privacy */
    T0("privacy", s);
    {
        int mid = L + bw + 8, mw = R - qw - 8 - mid, tw = text_w(f_ui, s);
        int lx;
        if (tw > mw - 14) tw = mw - 14;
        lx = mid + (mw - tw - 14) / 2;
        frame1(lx, y + 12, 8, 6, C_INK3);
        frame1(lx + 2, y + 8, 4, 5, C_INK3);
        text(lx + 13, y, tw + 2, 26, s, f_ui, C_INK3, TA_START_);
    }
    return y + 26;
}

static int status_bar(int y)
{
    int L = PAD, W = VW - 2 * PAD;
    Str s, a, b;
    if (T.has_dyn && T.has_rsrc) {
        const char *it[3];
        it[0] = "PerfStats"; it[1] = "RSRC32"; it[2] = "Toolhelp32";
        i18n_join(LC, it, 3, a, SB);
        T1("srcLine9x", "list", a, s);
    } else {
        const char *it[2];
        int n = 0;
        if (!T.has_dyn) it[n++] = "HKEY_DYN_DATA";
        if (!T.has_rsrc) it[n++] = "RSRC32";
        i18n_join(LC, it, n, a, SB);
        s_cpy(b, T.has_ntq ? "NtQuerySystemInformation" : "GlobalMemoryStatus", SB);
        T2("srcLineNt", "missing", a, "api", b, s);
    }
    edge(L, y, W, 18, BDR_SUNKENOUTER);
    text(L + 4, y + 1, W - 8, 16, s, f_small, C_INK2, TA_START_);
    return y + 18;
}

/* ---- main view --------------------------------------------------------------------- */

static int banner(int y)
{
    int L = PAD, W = VW - 2 * PAD, bw;
    Str s, a, p;
    if (g_confirm) {
        const TmApp *ap = find_app(g_confirm_exe);
        fill(L, y, W, 44, C_HOG_BG);
        raised(L, y, W, 44);
        T1("confirmTitle", "app", ap ? app_name(ap) : g_confirm_exe, s);
        T0("cancel", a);
        T0("confirmBtn", p);
        bw = text_w(f_bold, p) + 18;
        text(L + 10, y + 5, W - 30 - bw - text_w(f_ui, a) - 18, 16, s, f_bold, C_INK, TA_START_);
        text(L + 10, y + 22, W - 30 - bw - text_w(f_ui, a) - 18, 16, T0("confirmSub", s), f_ui, C_INK2, TA_START_);
        {
            int cw = text_w(f_ui, a) + 18;
            button(L + W - 8 - cw, y + 11, cw, 22, a, f_ui, A_NO, 0);
            button(L + W - 8 - cw - 6 - bw, y + 11, bw, 22, p, f_bold, A_YES, 0);
        }
        return y + 44 + 6;
    }
    if (g_toast[0] && GetTickCount() < g_toast_until) {
        fill(L, y, W, 26, C_TIP);
        frame1(L, y, W, 26, C_INK);
        text(L + 8, y, W - 16, 26, g_toast, f_ui, C_INK, TA_CENTER_);
        return y + 26 + 6;
    }
    if (g_view == V_MAIN && hog_active()) {
        const TmApp *ap = find_app(g_hog_exe);
        fill(L, y, W, 40, C_HOG_BG);
        raised(L, y, W, 40);
        { int xy[8]; xy[0] = L + 18; xy[1] = y + 13; xy[2] = L + 25; xy[3] = y + 26; xy[4] = L + 11; xy[5] = y + 26; xy[6] = L + 18; xy[7] = y + 13; polyline(xy, 4, C_WARN_INK, 1); }
        fill(L + 18, y + 17, 1, 5, C_WARN_INK); fill(L + 18, y + 23, 1, 1, C_WARN_INK);
        PCT(ap ? ap->cpu : 0, 0, p);
        T2("hogTitle", "app", ap ? app_name(ap) : "", "pct", p, s);
        T0("endApp", a);
        bw = text_w(f_bold, a) + 18;
        text(L + 34, y + 4, W - 34 - bw - 40, 16, s, f_bold, C_INK, TA_START_);
        PCT(g_hog_pct, 0, p);
        text(L + 34, y + 20, W - 34 - bw - 40, 16, T1("hogSub", "pct", p, s), f_ui, C_INK2, TA_START_);
        button(L + W - 32 - bw, y + 9, bw, 22, a, f_bold, A_ENDHOG, 0);
        line(L + W - 21, y + 16, L + W - 14, y + 23, C_INK, 1);
        line(L + W - 21, y + 23, L + W - 14, y + 16, C_INK, 1);
        line(L + W - 21, y + 15, L + W - 14, y + 22, C_INK, 1);
        line(L + W - 21, y + 22, L + W - 14, y + 15, C_INK, 1);
        hot(L + W - 26, y + 10, 18, 20, A_DISMISS, 0);
        return y + 40 + 6;
    }
    return y;
}

static void card_head(int x, int y, int w, int kind, COLORREF accent, const char *title, const char *badge, COLORREF badge_ink)
{
    int bw = 0;
    icon(kind, x + 10, y + 10, accent);
    if (badge) bw = text_w(f_bold, badge) + 6;
    text(x + 30, y + 8, w - 52 - bw, 18, title, f_title, C_INK, TA_START_);
    if (badge) text(x + w - 24 - bw, y + 8, bw, 18, badge, f_bold, badge_ink, TA_END_);
    chevron(x + w - 16, y + 13, 1, C_INK2);
}

static void card_cpu(int x, int y, int w, int h)
{
    Str s, a, b;
    int k, high = T.cpu >= 50;
    double vals[3];
    const char *labels[3];
    Str l0, l1, l2;
    COLORREF cols[3];
    raised(x, y, w, h);
    card_head(x, y, w, D_CPU, C_CPU, T0("cpu", s), high ? T0("high", a) : 0, C_WARN_INK);
    text(x + 10, y + 28, w - 20, 34, PCT(T.cpu, 0, s), f_hero, high ? C_WARN_INK : C_INK, TA_START_);
    PCT(T.cpu_user, 0, a); PCT(T.cpu_sys, 0, b);
    text(x + 10, y + 62, w - 20, 14, T2("userSys", "user", a, "sys", b, s), f_ui, C_INK2, TA_START_);
    H0("user", l0); H0("kernel", l1); T0("gpu", l2);
    labels[0] = l0; labels[1] = l1; labels[2] = l2;
    vals[0] = T.cpu_user; vals[1] = T.cpu_sys; vals[2] = T.gpu_load;
    cols[0] = C_CPU; cols[1] = blend(C_CPU, C_TRACK, 0.55); cols[2] = C_GPU;
    for (k = 0; k < 3; k++) {
        int ry = y + 80 + k * 16;
        text(x + 10, ry, 70, 14, labels[k], f_ui, C_INK, TA_START_);
        bar(x + 82, ry + 5, w - 82 - 48, 4, vals[k] / 100.0, cols[k]);
        text(x + w - 46, ry, 36, 14, PCT(vals[k], 0, s), f_bold, C_INK, TA_END_);
    }
    spark(s_cpu, s_n, x + 10, y + h - 30, w - 20, 24, C_CPU, 0);
    hot(x, y, w, h, A_CARD, D_CPU);
}

static void card_mem(int x, int y, int w, int h)
{
    Str s, a, b, u, t;
    double app, kern, cache, fr, tot = T.phys_total;
    int hw, bx, bw, k, cw;
    raised(x, y, w, h);
    card_head(x, y, w, D_MEM, C_MEM, T0("mem", s), 0, 0);
    hw = text(x + 10, y + 28, w - 20, 34, PCT(T.mem_load, 0, s), f_hero, C_INK, TA_START_);
    {
        /* Memory pressure from the load figure: under 70% normal, under 90% high. */
        int lv = T.mem_load < 70 ? 0 : T.mem_load < 90 ? 1 : 2;
        static const char *keys[3] = { "pressure", "pressureHigh", "pressureCritical" };
        static const COLORREF dot[3] = { C_CPU, C_WARN, C_CRIT };
        static const COLORREF ink[3] = { C_CPU_INK, C_WARN_INK, C_CRIT_INK };
        ellipse(x + 10 + hw + 10, y + 46, 2, dot[lv], dot[lv], 1);
        text(x + 10 + hw + 16, y + 38, w - 36 - hw, 16, T0(keys[lv], s), f_bold, ink[lv], TA_START_);
    }
    BYTES(mem_used(), a); BYTES(tot, b);
    text(x + 10, y + 62, w - 20, 14, T2("memOf", "used", a, "total", b, s), f_ui, C_INK2, TA_START_);
    mem_parts(&app, &kern, &cache, &fr);
    /* Segmented bar: app, kernel, disk cache (hatched), free */
    bx = x + 10; bw = w - 20;
    fill(bx, y + 80, bw, 8, C_TRACK);
    {
        int w1 = (int)(bw * app / tot), w2 = (int)(bw * kern / tot), w3 = (int)(bw * cache / tot);
        fill(bx, y + 80, w1, 8, C_MEM);
        fill(bx + w1 + 1, y + 80, w2 > 1 ? w2 - 1 : 0, 8, blend(C_MEM, C_TRACK, 0.45));
        for (k = 0; k < w3 - 1; k++) if (((k + 0) / 2) % 2 == 0) fill(bx + w1 + w2 + 1 + k, y + 80, 1, 8, C_MEM);
    }
    cw = (w - 20) / 2;
    fill(x + 10, y + 96, 6, 6, C_MEM);
    T0("segApp", t); BYTES(app, u); s_cat(t, " ", SB); s_cat(t, u, SB);
    text(x + 19, y + 92, cw - 10, 13, t, f_ui, C_INK, TA_START_);
    fill(x + 10 + cw, y + 96, 6, 6, blend(C_MEM, C_TRACK, 0.45));
    H0("kernel", t); PART(kern, u); s_cat(t, " ", SB); s_cat(t, u, SB);
    text(x + 19 + cw, y + 92, cw - 10, 13, t, f_ui, C_INK, TA_START_);
    frame1(x + 10, y + 109, 6, 6, C_MEM); fill(x + 12, y + 111, 2, 2, C_MEM);
    H0("diskCache", t); PART(cache, u); s_cat(t, " ", SB); s_cat(t, u, SB);
    text(x + 19, y + 105, w - 29, 13, t, f_ui, C_INK, TA_START_);
    frame1(x + 10, y + 122, 6, 6, C_HAIR);
    T0("segFree", t); BYTES(fr, u); s_cat(t, " ", SB); s_cat(t, u, SB);
    text(x + 19, y + 118, w - 29, 13, t, f_ui, C_INK, TA_START_);
    spark(s_mem, s_n, x + 10, y + h - 30, w - 20, 24, C_MEM, 0);
    hot(x, y, w, h, A_CARD, D_MEM);
}

static void card_nrg(int x, int y, int w, int h)
{
    Str s, a;
    raised(x, y, w, h);
    card_head(x, y, w, D_NRG, C_NRG, T0("nrg", s), 0, 0);
    if (T.has_batt) {
        text(x + 10, y + 28, w - 20, 34, PCT(T.batt_pct, 0, s), f_hero, C_INK, TA_START_);
        if (T.ac_online) T0("charging", s);
        else if (T.batt_secs > 0) T1("timeLeft", "time", DUR(T.batt_secs, a), s);
        else T0("onBatt", s);
    } else {
        T0("acPower", s);
        text(x + 10, y + 28, w - 20, 34, s, text_w(f_hero, s) <= w - 20 ? f_hero : text_w(f_brand, s) <= w - 20 ? f_brand : f_title, C_INK, TA_START_);
        T0("noBattery", s);
    }
    text(x + 10, y + 62, w - 20, 14, s, f_ui, C_INK2, TA_START_);
    /* Source box, like the prototype's flow chip */
    T0(T.has_batt && !T.ac_online ? "tBattery" : "acPower", a);
    {
        int bw = text_w(f_bold, a) + 36;
        if (bw > w - 20) bw = w - 20;
        fill(x + 10, y + 82, bw, 22, C_TRACK);
        sunken(x + 10, y + 82, bw, 22);
        ellipse(x + 19, y + 93, 2, C_NRG, C_NRG, 1);
        text(x + 25, y + 82, bw - 28, 22, a, f_bold, C_NRG_INK, TA_START_);
    }
    T0("tSource", a);
    text(x + 10, y + 110, w - 20, 14, a, f_ui, C_INK2, TA_START_);
    spark(s_nrg, s_n, x + 10, y + h - 30, w - 20, 24, C_NRG, 0);
    hot(x, y, w, h, A_CARD, D_NRG);
}

static void card_thm(int x, int y, int w, int h)
{
    Str s, a, b, sim;
    static const char *names[4] = { "cool", "warm", "hot", "throttled" };
    static const COLORREF lv_c[4] = { C_MEM, C_NRG, C_THM, C_CRIT };
    static const COLORREF lv_i[4] = { C_MEM_INK, C_NRG_INK, C_THM_INK, C_CRIT_INK };
    int lv = thermal_level(T.cpu_temp), k, sw;
    raised(x, y, w, h);
    card_head(x, y, w, D_THM, C_THM, T0("thm", s), 0, 0);
    text(x + 10, y + 28, w - 20, 34, T0(names[lv], s), f_hero, lv_i[lv], TA_START_);
    TEMP(T.cpu_temp, 0, a); TEMP(T.gpu_temp, 0, b);
    text(x + 10, y + 62, w - 20, 14, T2("tempLine", "cpu", a, "gpu", b, s), f_ui, C_INK2, TA_START_);
    sw = (w - 20 - 6) / 4;
    for (k = 0; k < 4; k++) {
        fill(x + 10 + k * (sw + 2), y + 82, sw, 6, k == lv ? lv_c[lv] : C_TRACK);
        text(x + 10 + k * (sw + 2), y + 92, sw, 14, T0(names[k], s), k == lv ? f_bold : f_small, k == lv ? lv_i[lv] : C_INK3, TA_CENTER_);
    }
    text(x + 10, y + 110, w - 20, 14, T0(lv < 3 ? "zero" : "throttling", s), f_ui, lv < 3 ? C_INK3 : C_CRIT_INK, TA_START_);
    spark(s_thm, s_n, x + 10, y + h - 30, w - 20, 24, C_THM, T0("srcSim", sim));
    hot(x, y, w, h, A_CARD, D_THM);
}

static void strip_gpu(int x, int y, int w)
{
    Str s, g;
    int gw;
    raised(x, y, w, 30);
    icon(D_GPU, x + 10, y + 8, C_GPU);
    gw = text_w(f_bold, T0("gpu", g));
    if (gw > 110) gw = 110;
    gw = text(x + 28, y, gw + 1, 30, g, f_bold, C_INK, TA_START_);
    text(x + 28 + gw + 8, y, w - 28 - gw - 8 - 162, 30, T.gpu_name, f_ui, C_INK2, TA_START_);
    bar(x + w - 150, y + 13, 72, 5, T.gpu_load / 100.0, C_GPU);
    text(x + w - 74, y, 32, 30, PCT(T.gpu_load, 0, s), f_bold, C_INK, TA_END_);
    text(x + w - 42, y, 34, 30, TEMP(T.gpu_temp, 0, s), f_bold, C_GPU_INK, TA_END_);
    hot(x, y, w, 30, A_CARD, D_GPU);
}

static void card_disk(int x, int y, int w)
{
    Str s, a, d;
    double usedf = T.disk_total > 0 ? (T.disk_total - T.disk_free) / T.disk_total : 0;
    raised(x, y, w, 62);
    icon(D_SSD, x + 10, y + 8, C_CPU);
    text(x + 28, y + 6, w - 38, 18, H1("drive", "drive", T.drive, s), f_bold, C_INK, TA_START_);
    PCT(usedf * 100, 0, a);
    text(x + 10, y + 26, w / 2, 16, T1("used", "pct", a, s), f_bold, C_INK, TA_START_);
    text(x + w / 2, y + 26, w / 2 - 10, 16, T1("free", "value", BYTES(T.disk_free, d), s), f_ui, C_INK2, TA_END_);
    fill(x + 10, y + 46, w - 20, 6, C_TRACK);
    frame1(x + 10, y + 46, w - 20, 6, C_HAIR);
    fill(x + 11, y + 47, (int)((w - 22) * usedf), 4, C_SSD);
    hot(x, y, w, 62, A_CARD, D_SSD);
}

static void card_net(int x, int y, int w)
{
    Str s;
    int dw;
    raised(x, y, w, 62);
    icon(D_NET, x + 10, y + 8, C_MEM);
    text(x + 28, y + 6, w - 38, 18, H0(T.has_dun ? "dialUp" : "lanConnection", s), f_bold, C_INK, TA_START_);
    arrow(x + 10, y + 29, 1, C_MEM_INK);
    dw = text(x + 22, y + 26, w / 2 - 12, 16, RATE(T.net_down, s), f_bold, C_MEM_INK, TA_START_);
    arrow(x + 22 + dw + 12, y + 29, 0, C_CPU_INK);
    text(x + 22 + dw + 24, y + 26, w - 32 - dw - 24, 16, RATE(T.net_up, s), f_bold, C_CPU_INK, TA_START_);
    hot(x, y, w, 62, A_CARD, D_NET);
}

/* Sorted top apps: index list into T.apps */
static int top_apps(int *idx, int max)
{
    int k, j, n = 0;
    for (k = 0; k < T.napps; k++) {
        double v;
        int ins;
        v = app_metric(&T.apps[k], g_sort);
        ins = n;
        while (ins > 0 && app_metric(&T.apps[idx[ins - 1]], g_sort) < v) ins--;
        if (ins >= max) continue;
        for (j = (n < max ? n : max - 1); j > ins; j--) idx[j] = idx[j - 1];
        idx[ins] = k;
        if (n < max) n++;
    }
    return n;
}

static int card_apps(int x, int y, int w)
{
    Str s, lab[3];
    int idx[5], n, k, h, segw[3], tot = 0, sx;
    double mx;
    static const char *keys[3] = { "cpu", "mem", "gpu" };
    n = top_apps(idx, 5);
    h = 40 + (n ? n : 1) * 30 + 8 + (T.proc_cpu ? 0 : 18);
    raised(x, y, w, h);
    /* Sort control */
    for (k = 0; k < 3; k++) { T0(keys[k], lab[k]); segw[k] = text_w(f_bold, lab[k]) + 16; tot += segw[k]; }
    text(x + 12, y + 8, w - 24 - tot - 10, 22, T0("top", s), f_title, C_INK, TA_START_);
    sx = x + w - 10 - tot - 4;
    fill(sx, y + 8, tot + 4, 24, C_TRACK);
    sunken(sx, y + 8, tot + 4, 24);
    sx += 2;
    for (k = 0; k < 3; k++) {
        int enabled = T.proc_cpu && k != S_GPU;
        if (enabled && g_sort == k) { fill(sx, y + 10, segw[k], 20, C_SEG); raised(sx, y + 10, segw[k], 20); }
        if (enabled) { text(sx, y + 10, segw[k], 20, lab[k], f_bold, C_INK, TA_CENTER_); hot(sx, y + 10, segw[k], 20, A_SORT, k); }
        else text_etched(sx, y + 10, segw[k], 20, lab[k], f_bold, TA_CENTER_);
        sx += segw[k];
    }
    mx = n ? app_metric(&T.apps[idx[0]], g_sort) : 1;
    if (mx <= 0) mx = 1;
    g_nrows = n;
    for (k = 0; k < n; k++) {
        const TmApp *a = &T.apps[idx[k]];
        int ry = y + 40 + k * 30, nw, hogrow = T.proc_cpu && g_sort == S_CPU && g_hog_on && s_ieq(a->exe, g_hog_exe);
        char mono[8];
        double v = app_metric(a, g_sort);
        s_cpy(g_rows[k], a->exe, TM_NAME);
        if (g_hover == k) fill(x + 4, ry, w - 8, 30, blend(RGB(0, 0, 128), GetSysColor(COLOR_3DFACE), 0.1));
        fill(x + 12, ry + 6, 18, 18, C_TRACK);
        edge(x + 12, ry + 6, 18, 18, BDR_SUNKENOUTER);
        {
            /* first character of the display name (UTF-8) */
            const unsigned char *nm = (const unsigned char *)app_name(a);
            int len = nm[0] < 0x80 ? 1 : (nm[0] & 0xE0) == 0xC0 ? 2 : 3, j;
            for (j = 0; j < len && nm[j]; j++) mono[j] = (char)nm[j];
            mono[j] = 0;
            if (mono[0] >= 'a' && mono[0] <= 'z') mono[0] = (char)(mono[0] - 32);
        }
        text(x + 12, ry + 6, 18, 18, mono, f_bold, C_MEM_INK, TA_CENTER_);
        nw = text(x + 38, ry, w - 38 - 140, 30, app_name(a), f_ui, C_INK, TA_START_);
        if (a->procs > 1) {
            Str c;
            int cw;
            s_cpy(c, "\xC3\x97", SB);
            N(a->procs, 0, s); s_cat(c, s, SB);
            cw = text_w(f_small, c) + 8;
            fill(x + 38 + nw + 6, ry + 9, cw, 13, C_TRACK);
            frame1(x + 38 + nw + 6, ry + 9, cw, 13, C_HAIR);
            text(x + 38 + nw + 6, ry + 9, cw, 13, c, f_small, C_INK, TA_CENTER_);
        }
        if (hogrow) bar_grad(x + w - 120, ry + 13, 56, 4, v / mx);
        else bar(x + w - 120, ry + 13, 56, 4, v / mx, !T.proc_cpu ? C_CPU : g_sort == S_MEM ? C_MEM : C_CPU);
        if (!T.proc_cpu) i18n_plural(LC, "thread", a->threads, s, SB);
        else if (g_sort == S_MEM) BYTES(a->mem, s);
        else PCT(a->cpu, 1, s);
        text(x + w - 62, ry, 52, 30, s, f_bold, hogrow ? C_WARN_INK : C_INK, TA_END_);
        hot(x + 4, ry, w - 8, 30, A_ROW, k);
    }
    if (!T.proc_cpu) text(x + 12, y + h - 24, w - 24, 16, T0("perAppNa", s), f_small, C_INK3, TA_START_);
    return y + h;
}

static int view_main(int y)
{
    int L = PAD, IW = VW - 2 * PAD, cw = (IW - 6) / 2;
    y = banner(y);
    card_cpu(L, y, cw, 176);
    card_mem(L + IW - cw, y, cw, 176);
    y += 182;
    card_nrg(L, y, cw, 176);
    card_thm(L + IW - cw, y, cw, 176);
    y += 182;
    strip_gpu(L, y, IW);
    y += 36;
    card_disk(L, y, cw);
    card_net(L + IW - cw, y, cw);
    y += 68;
    y = card_apps(L, y, IW) + 6;
    return y;
}

/* ---- detail view ---------------------------------------------------------------------- */

typedef struct { Str k, v; } Tile;

static int view_detail(int y)
{
    int L = PAD, IW = VW - 2 * PAD, x, k, ntiles = 0, split = 0, zero = 0, hw;
    Str title, hero, sub, stat, a, b, c, la, lb;
    Tile tiles[6];
    const float *sa = h_cpu, *sb2 = 0;
    int n = h_n;
    COLORREF acc = C_CPU, acc_ink = C_CPU_INK, acc2 = C_NRG;
    const TmApp *app = 0;
    AppHist *ah = 0;
    int unit_pct = 1, unit_temp = 0, unit_rate = 0;

    la[0] = lb[0] = 0;
    switch (g_detail) {
    case D_CPU:
        T0("cpu", title); PCT(T.cpu, 0, hero);
        PCT(T.cpu_user, 0, a); PCT(T.cpu_sys, 0, b); cores_label(c);
        T3("cpuDetailSub", "user", a, "sys", b, "cores", c, sub);
        zero = 1;
        T0("tUser", tiles[0].k); PCT(T.cpu_user, 0, tiles[0].v);
        T0("tSystem", tiles[1].k); PCT(T.cpu_sys, 0, tiles[1].v);
        T0("tIdle", tiles[2].k); PCT(100 - T.cpu < 0 ? 0 : 100 - T.cpu, 0, tiles[2].v);
        {
            int procs = 0, threads = 0;
            for (k = 0; k < T.napps; k++) { procs += T.apps[k].procs; threads += T.apps[k].threads; }
            T0("tipProc", tiles[3].k); N(procs, 0, tiles[3].v);
            T0("tThreads", tiles[4].k); N(threads, 0, tiles[4].v);
        }
        T0("cpu", tiles[5].k); acp_to_utf8(T.cpu_model, tiles[5].v, SB);
        ntiles = 6;
        break;
    case D_MEM: {
        double app2, kern, cache, fr;
        acc = C_MEM; acc_ink = C_MEM_INK; sa = h_mem;
        T0("mem", title); PCT(T.mem_load, 0, hero);
        BYTES(mem_used(), a); BYTES(T.phys_total, b);
        T2("memInUse", "used", a, "total", b, sub);
        mem_parts(&app2, &kern, &cache, &fr);
        T0("segApp", tiles[0].k); BYTES(app2, tiles[0].v);
        H0("kernel", tiles[1].k); if (T.memdetail_src == SRC_LIVE) BYTES(kern, tiles[1].v); else T0("srcNone", tiles[1].v);
        H0("diskCache", tiles[2].k); if (T.memdetail_src == SRC_LIVE) BYTES(cache, tiles[2].v); else T0("srcNone", tiles[2].v);
        T0("segFree", tiles[3].k); BYTES(fr, tiles[3].v);
        if (T.swap_is_commit) {
            H0("commitCharge", tiles[4].k);
            BYTES(T.swap_used, a); BYTES(T.swap_total, b);
            s_cpy(tiles[4].v, a, SB); s_cat(tiles[4].v, " / ", SB); s_cat(tiles[4].v, b, SB);
        } else { H0("swapFile", tiles[4].k); BYTES(T.swap_used, tiles[4].v); }
        H0("sysResources", tiles[5].k);
        if (T.res_src == SRC_LIVE) H1("pctFree", "pct", PCT(T.res_free[0], 0, a), tiles[5].v);
        else T0("notOnNt", tiles[5].v);
        ntiles = 6;
        break; }
    case D_NRG:
        acc = C_NRG; acc_ink = C_NRG_INK; sa = h_nrg; zero = 1;
        T0("nrg", title);
        if (T.has_batt) PCT(T.batt_pct, 0, hero); else T0("acPower", hero);
        if (!T.has_batt) T0("noBattery", sub);
        else if (T.ac_online) T0("charging", sub);
        else if (T.batt_secs > 0) T1("onBattLeft", "time", DUR(T.batt_secs, a), sub);
        else T0("onBatt", sub);
        T0("tSource", tiles[0].k); T0(T.has_batt && !T.ac_online ? "tBattery" : "acPower", tiles[0].v);
        T0("tBattery", tiles[1].k); if (T.has_batt) PCT(T.batt_pct, 0, tiles[1].v); else T0("noBattery", tiles[1].v);
        T0("tTimeLeft", tiles[2].k); if (T.has_batt && T.batt_secs > 0) DUR(T.batt_secs, tiles[2].v); else s_cpy(tiles[2].v, "-", SB);
        T0("srcTitle", tiles[3].k); s_cpy(tiles[3].v, "GetSystemPowerStatus", SB);
        ntiles = 4;
        break;
    case D_THM: {
        static const char *names[4] = { "cool", "warm", "hot", "throttled" };
        int lv = thermal_level(T.cpu_temp);
        acc = lv == 0 ? C_MEM : lv == 1 ? C_NRG : lv == 2 ? C_THM : C_CRIT;
        acc_ink = lv == 0 ? C_MEM_INK : lv == 1 ? C_NRG_INK : lv == 2 ? C_THM_INK : C_CRIT_INK;
        sa = h_thm; unit_pct = 0; unit_temp = 1;
        T0("thm", title); TEMP(T.cpu_temp, 0, hero);
        J2(T0(names[lv], a), T0(lv < 3 ? "zero" : "throttling", b), sub);
        T0("tCpuDie", tiles[0].k); TEMP(T.cpu_temp, 1, tiles[0].v);
        T0("gpu", tiles[1].k); TEMP(T.gpu_temp, 1, tiles[1].v);
        T0("tThrottling", tiles[2].k); PCT(0, 0, tiles[2].v);
        T0("srcTitle", tiles[3].k); T0("srcSim", tiles[3].v);
        ntiles = 4;
        break; }
    case D_GPU:
        acc = C_GPU; acc_ink = C_GPU_INK; sa = h_gpu; zero = 1;
        T0("gpu", title); PCT(T.gpu_load, 0, hero);
        J2(T.gpu_name, TEMP(T.gpu_temp, 0, a), sub);
        T0("util", tiles[0].k); PCT(T.gpu_load, 0, tiles[0].v);
        T0("tTemperature", tiles[1].k); TEMP(T.gpu_temp, 1, tiles[1].v);
        T0("gpu", tiles[2].k); s_cpy(tiles[2].v, T.gpu_name, SB);
        T0("srcTitle", tiles[3].k); T0("srcSim", tiles[3].v);
        ntiles = 4;
        break;
    case D_SSD: {
        double usedf = T.disk_total > 0 ? (T.disk_total - T.disk_free) / T.disk_total : 0;
        acc = C_CPU; acc_ink = C_CPU_INK; acc2 = C_NRG; split = 1; sa = h_rd; sb2 = h_wr; unit_rate = 1;
        T0("storage", title); PCT(usedf * 100, 0, hero);
        H1("drive", "drive", T.drive, a);
        J2(a, T1("free", "value", BYTES(T.disk_free, b), c), sub);
        T0("read", la); T0("write", lb);
        T0("tUsed", tiles[0].k); BYTES(T.disk_total - T.disk_free, tiles[0].v);
        T0("tFree", tiles[1].k); BYTES(T.disk_free, tiles[1].v);
        T0("read", tiles[2].k); RATE(T.disk_read, tiles[2].v);
        T0("write", tiles[3].k); RATE(T.disk_write, tiles[3].v);
        T0("tFormat", tiles[4].k); acp_to_utf8(T.fs[0] ? T.fs : "-", tiles[4].v, SB);
        ntiles = 5;
        break; }
    case D_NET: {
        Str link, det, st;
        acc = C_MEM; acc_ink = C_MEM_INK; acc2 = C_CPU; split = 1; sa = h_dn; sb2 = h_up; unit_rate = 1;
        T0("network", title); RATE(T.net_down, hero);
        if (T.link_bps <= 0) s_cpy(det, "-", SB);
        else if (T.link_bps >= 1000000) { N(T.link_bps / 1000000.0, 0, det); s_cat(det, " Mbps", SB); }
        else { N(T.link_bps / 1000.0, 1, det); s_cat(det, " kbps", SB); }
        if (T.has_dun) H1("modem", "speed", "56K", link); else s_cpy(link, "Ethernet", SB);
        H0("connected", st);
        {
            I18nParam p[3];
            p[0].name = "link"; p[0].value = link; p[1].name = "detail"; p[1].value = det; p[2].name = "state"; p[2].value = st;
            i18n_hw(LC, "netSub", p, 3, sub, SB);
        }
        T0("down", la); T0("up", lb);
        T0("down", tiles[0].k); RATE(T.net_down, tiles[0].v);
        T0("up", tiles[1].k); RATE(T.net_up, tiles[1].v);
        T0("tInterface", tiles[2].k); s_cpy(tiles[2].v, T.has_dun ? "Dial-Up Adapter" : "Ethernet", SB);
        H0(T.has_dun ? "lineSpeed" : "linkSpeed", tiles[3].k); s_cpy(tiles[3].v, det, SB);
        ntiles = 4;
        break; }
    default: {
        app = find_app(g_detail_exe);
        ah = app_hist(g_detail_exe, 0);
        acc = (app && g_hog_on && s_ieq(app->exe, g_hog_exe)) ? C_WARN : C_CPU;
        acc_ink = acc == C_WARN ? C_WARN_INK : C_CPU_INK;
        zero = 1;
        if (ah) { sa = ah->v; n = ah->n; } else n = 0;
        s_cpy(title, app ? app_name(app) : g_detail_exe, SB);
        if (!app) { s_cpy(hero, "-", SB); sub[0] = 0; ntiles = 0; break; }
        if (T.proc_cpu) PCT(app->cpu, 1, hero); else i18n_plural(LC, "thread", app->threads, hero, SB);
        if (!T.proc_cpu) unit_pct = 0;
        i18n_plural(LC, "process", app->procs, a, SB);
        i18n_plural(LC, "thread", app->threads, b, SB);
        J3(a, b, PIDHEX(app->pid, c), sub);
        T0("cpu", tiles[0].k); if (T.proc_cpu) PCT(app->cpu, 1, tiles[0].v); else T0("srcNone", tiles[0].v);
        T0("mem", tiles[1].k); if (app->mem >= 0) BYTES(app->mem, tiles[1].v); else T0("srcNone", tiles[1].v);
        T0("tipProc", tiles[2].k); N(app->procs, 0, tiles[2].v);
        T0("tThreads", tiles[3].k); N(app->threads, 0, tiles[3].v);
        s_cpy(tiles[4].k, "PID", SB); wsprintfA(tiles[4].v, "%08lX", (unsigned long)app->pid);
        ntiles = 5;
        break; }
    }

    y = banner(y);
    /* Back row */
    T0("back", a);
    {
        int bw = text_w(f_bold, a) + 30;
        fill(L, y, bw, 26, GetSysColor(COLOR_3DFACE));
        raised(L, y, bw, 26);
        chevron(L + 9, y + 9, 0, C_INK);
        text(L + 19, y, bw - 22, 26, a, f_bold, C_INK, TA_START_);
        hot(L, y, bw, 26, A_BACK, 0);
        x = L + bw + 14;
        ellipse(x + 3, y + 13, 3, acc, acc, 1);
        if (g_detail == D_APP && app) {
            int ew;
            T0("endApp", b);
            ew = text_w(f_bold, b) + 18;
            text(x + 12, y, L + IW - ew - 8 - (x + 12), 26, title, f_title, acc_ink, TA_START_);
            button(L + IW - ew, y, ew, 26, b, f_bold, A_ENDAPP, 0);
        } else text(x + 12, y, L + IW - (x + 12), 26, title, f_title, acc_ink, TA_START_);
    }
    y += 34;
    hw = text(L, y, IW / 2, 34, hero, f_hero, C_INK, TA_START_);
    text(L + hw + 10, y + 10, IW - hw - 10, 18, sub, f_ui, C_INK2, TA_START_);
    y += 40;

    /* Chart card */
    {
        int cx = L + 10, cy = y + 30, cw = IW - 20, ch = 150;
        double mn, mx, avg = 0;
        Str pk, av;
        RECT clip;
        raised(L, y, IW, 214);
        text(L + 10, y + 6, IW / 2, 20, T0("last10", a), f_bold, C_INK, TA_START_);
        fill(cx, cy, cw, ch, C_TRACK);
        sunken(cx - 2, cy - 2, cw + 4, ch + 4);
        if (!g_measure) {
            HPEN p = CreatePen(PS_DOT, 1, RGB(0x90, 0x90, 0x90)), o = (HPEN)SelectObject(D, p);
            int gy;
            SetBkMode(D, TRANSPARENT);
            for (k = 1; k <= 3; k++) {
                gy = PY(cy + ch * k / 4);
                if (split && k != 2) continue;
                MoveToEx(D, PX(cx), gy, 0); LineTo(D, PX(cx + cw), gy);
            }
            if (!split) for (k = 1; k <= 3; k += 2) { gy = PY(cy + ch * k / 4); MoveToEx(D, PX(cx), gy, 0); LineTo(D, PX(cx + cw), gy); }
            SelectObject(D, o);
            DeleteObject(p);
        }
        clip = RR(cx, cy, cw, ch);
        if (!split) {
            double lo, hi;
            minmax(sa, n, &mn, &mx);
            for (k = 0; k < n; k++) avg += sa[k];
            avg = n ? avg / n : 0;
            lo = zero ? 0 : (mn - (mx - mn) * 0.8 < 0 ? 0 : mn - (mx - mn) * 0.8);
            hi = mx + (mx - lo) * 0.2;
            if (hi - lo < 1) hi = lo + 1;
            if (!g_measure) {
                SaveDC(D);
                IntersectClipRect(D, clip.left, clip.top, clip.right, clip.bottom);
                curve(sa, n, HIST_N, cx, cy + 28, cw, ch - 34, lo, hi, 1, cy + ch, acc, blend(acc, C_TRACK, 0.16), 2);
                RestoreDC(D, -1);
            }
            if (unit_temp) { TEMP(mx, 1, pk); TEMP(avg, 1, av); }
            else if (unit_pct) { PCT(mx, zero && g_detail == D_APP ? 1 : 0, pk); PCT(avg, zero && g_detail == D_APP ? 1 : 0, av); }
            else { N(mx, 0, pk); N(avg, 0, av); }
            if (n) T2("peakAvg", "peak", pk, "avg", av, stat); else stat[0] = 0;
        } else {
            double ma, mb, d1, d2;
            int mid = cy + ch / 2;
            minmax(sa, n, &d1, &ma); minmax(sb2, n, &d2, &mb);
            ma = ma * 1.12; mb = mb * 1.12;
            if (ma <= 0) ma = 1;
            if (mb <= 0) mb = 1;
            if (!g_measure) {
                SaveDC(D);
                IntersectClipRect(D, clip.left, clip.top, clip.right, clip.bottom);
                curve(sa, n, HIST_N, cx, cy + 22, cw, ch / 2 - 23, 0, ma, 1, mid, acc, blend(acc, C_TRACK, 0.16), 2);
                curve(sb2, n, HIST_N, cx, mid + 1, cw, ch / 2 - 23, 0, mb, 0, mid, acc2, blend(acc2, C_TRACK, 0.16), 2);
                RestoreDC(D, -1);
            }
            /* legend in the chart corner */
            {
                int lx = cx + 8, w1;
                ellipse(lx + 3, cy + 10, 3, acc, acc, 1);
                w1 = text(lx + 10, cy + 2, cw / 3, 16, la, f_small, C_INK2, TA_START_);
                ellipse(lx + 22 + w1, cy + 10, 3, acc2, acc2, 1);
                text(lx + 29 + w1, cy + 2, cw / 3, 16, lb, f_small, C_INK2, TA_START_);
            }
            RATE(ma / 1.12, pk); RATE(mb / 1.12, av);
            if (unit_rate && n) T2("peakSplit", "a", pk, "b", av, stat); else stat[0] = 0;
        }
        text(L + IW / 2, y + 6, IW / 2 - 10, 20, stat, f_ui, C_INK2, TA_END_);
        text(cx, cy + ch + 4, cw / 3, 16, T0("ago10", a), f_small, C_INK3, TA_START_);
        text(cx + cw / 3, cy + ch + 4, cw / 3, 16, T0("ago5", a), f_small, C_INK3, TA_CENTER_);
        text(cx + 2 * cw / 3, cy + ch + 4, cw / 3, 16, T0("now", a), f_small, C_INK3, TA_END_);
        y += 220;
    }
    /* Tiles */
    {
        int tw = (IW - 6) / 2;
        for (k = 0; k < ntiles; k++) {
            int tx = L + (k % 2) * (IW - tw), ty = y + (k / 2) * 56;
            raised(tx, ty, tw, 50);
            text(tx + 10, ty + 6, tw - 20, 16, tiles[k].k, f_ui, C_INK2, TA_START_);
            text(tx + 10, ty + 23, tw - 20, 20, tiles[k].v, f_tile, C_INK, TA_START_);
        }
        y += ((ntiles + 1) / 2) * 56;
    }
    return y;
}

/* ---- settings view ------------------------------------------------------------------------ */

static int check_row(int x, int y, int w, int on, const char *label, const char *sub, int act_id)
{
    if (!g_measure) {
        RECT r = RR(x + 12, y + 8, 13, 13);
        DrawFrameControl(D, &r, DFC_BUTTON, DFCS_BUTTONCHECK | (on ? DFCS_CHECKED : 0));
    }
    text(x + 32, y + 5, w - 44, 18, label, f_bold, C_INK, TA_START_);
    text(x + 32, y + 21, w - 44, 15, sub, f_ui, C_INK2, TA_START_);
    hot(x + 6, y + 2, w - 12, 36, act_id, 0);
    return y + 40;
}

/* Language: classic radio list with "Match system" and each locale by its own name. */
static int language_rows(int x, int y, int w)
{
    Str s;
    int k, n = i18n_count();
    text(x + 12, y + 3, w - 24, 18, T0("language", s), f_bold, C_INK, TA_START_);
    text(x + 12, y + 20, w - 24, 15, T0("languageSub", s), f_ui, C_INK2, TA_START_);
    y += 40;
    for (k = -1; k < n && k < 8; k++) {
        int on = k < 0 ? g_lang_system : (!g_lang_system && s_eq(i18n_list(k)->code, LC->d->code));
        int ok = k < 0 || g_lang_ok[k];
        if (!g_measure) {
            RECT r = RR(x + 14, y + 3, 13, 13);
            DrawFrameControl(D, &r, DFC_BUTTON, DFCS_BUTTONRADIO | (on ? DFCS_CHECKED : 0) | (ok ? 0 : DFCS_INACTIVE));
        }
        if (k < 0) text(x + 34, y, w - 46, 19, T0("langSystem", s), f_ui, C_INK, TA_START_);
        else {
            /* Each name in its own script, font and reading order. */
            const I18nLocaleData *d = i18n_list(k);
            RECT r = RR(x + 34, y, w - 46, 19);
            if (!g_measure) {
                HFONT o;
                txt_mode(g_ansi, d->codepage, d->rtl);
                o = (HFONT)SelectObject(D, f_lname[k]);
                /* Keep the name next to its radio button in either layout. */
                txt_draw(D, &r, d->name, g_rtl != d->rtl ? TA_END_ : TA_START_, ok ? C_INK : GetSysColor(COLOR_GRAYTEXT));
                SelectObject(D, o);
                txt_mode(g_ansi, LC->d->codepage, LC->d->rtl);
            }
        }
        if (ok) hot(x + 8, y, w - 16, 20, A_LANGSET, k + 1);
        y += 20;
    }
    return y + 6;
}

static void divider(int x, int y, int w)
{
    line(x + 10, y, x + w - 10, y, GetSysColor(COLOR_3DSHADOW), 1);
    line(x + 10, y + 1, x + w - 10, y + 1, GetSysColor(COLOR_3DHILIGHT), 1);
}

static int view_settings(int y)
{
    int L = PAD, IW = VW - 2 * PAD, k, gh, top;
    Str a, b, s;
    y = banner(y);
    T0("back", a);
    {
        int bw = text_w(f_bold, a) + 30;
        fill(L, y, bw, 26, GetSysColor(COLOR_3DFACE));
        raised(L, y, bw, 26);
        chevron(L + 9, y + 9, 0, C_INK);
        text(L + 19, y, bw - 22, 26, a, f_bold, C_INK, TA_START_);
        hot(L, y, bw, 26, A_BACK, 0);
        text(L + bw + 14, y, IW - bw - 14, 26, T0("aSettings", s), f_title, C_INK, TA_START_);
    }
    y += 34;
    top = y;
    gh = 3 * 40 + 6 + 2 + 36 + 2 + 44 + 4 + 2 + 40 + (i18n_count() + 1) * 20 + 6;
    raised(L, y, IW, gh);
    y += 4;
    PCT(g_hog_pct, 0, a);
    y = check_row(L, y, IW, g_sim_hog, T0("simHog", s), T1("simHogSub", "pct", a, b), A_SIMHOG);
    y = check_row(L, y, IW, g_sim_charge, T0("simCharging", s), T1("simChargingSub", "adapter", "AC 60 W", b), A_SIMCHG);
    N(1.5, 1, a);
    T1("seconds", "n", a, b);
    y = check_row(L, y, IW, g_live, T0("simLive", s), T1("simLiveSub", "secs", b, a), A_LIVE);
    y += 2;
    divider(L, y, IW);
    y += 4;
    text(L + 12, y, IW - 120, 32, T0("tempUnit", s), f_bold, C_INK, TA_START_);
    {
        int sx = L + IW - 12 - 84;
        fill(sx, y + 4, 84, 24, C_TRACK);
        sunken(sx, y + 4, 84, 24);
        for (k = 0; k < 2; k++) {
            int seg = sx + 2 + k * 40;
            if (g_unit_f == k) { fill(seg, y + 6, 40, 20, C_SEG); raised(seg, y + 6, 40, 20); }
            text(seg, y + 6, 40, 20, k ? "\xC2\xB0" "F" : "\xC2\xB0" "C", f_bold, C_INK, TA_CENTER_);
            hot(seg, y + 6, 40, 20, A_UNIT, k);
        }
    }
    y += 34;
    divider(L, y, IW);
    y += 4;
    y = language_rows(L, y, IW);
    divider(L, y, IW);
    y += 4;
    {
        int bw;
        T0("restoreBtn", a);
        bw = text_w(f_bold, a) + 24;
        text(L + 12, y + 3, IW - 36 - bw, 18, T0("restore", s), f_bold, C_INK, TA_START_);
        i18n_plural(LC, "ended", g_nended, s, SB);
        text(L + 12, y + 20, IW - 36 - bw, 15, s, f_ui, C_INK2, TA_START_);
        fill(L + IW - 12 - bw, y + 8, bw, 24, GetSysColor(COLOR_3DFACE));
        raised(L + IW - 12 - bw, y + 8, bw, 24);
        if (g_nended) { text(L + IW - 12 - bw, y + 8, bw, 24, a, f_bold, C_INK, TA_CENTER_); hot(L + IW - 12 - bw, y + 8, bw, 24, A_RESTORE, 0); }
        else text_etched(L + IW - 12 - bw, y + 8, bw, 24, a, f_bold, TA_CENTER_);
    }
    y = top + gh + 6;
    /* Data sources */
    {
        struct { const char *label_key; int hw; const char *api; int src; } rows[9];
        int n = 0;
        rows[n].label_key = "cpu"; rows[n].hw = 0;
        rows[n].api = T.has_dyn ? "PerfStats KERNEL\\CPUUsage" : T.has_ntq ? "NtQuerySystemInformation" : "-";
        rows[n].src = T.cpu_src; n++;
        rows[n].label_key = "mem"; rows[n].hw = 0;
        rows[n].api = T.memdetail_src != SRC_LIVE ? "GlobalMemoryStatus" : T.has_dyn ? "GlobalMemoryStatus, PerfStats VMM" : "GlobalMemoryStatus, GetPerformanceInfo";
        rows[n].src = SRC_LIVE; n++;
        rows[n].label_key = "sysResources"; rows[n].hw = 1;
        rows[n].api = T.has_rsrc ? "RSRC32 _MyGetFreeSystemResources32@4" : "RSRC32.DLL";
        rows[n].src = T.res_src; n++;
        rows[n].label_key = "top"; rows[n].hw = 0;
        rows[n].api = T.has_ntq ? "NtQuerySystemInformation" : T.has_th32 ? "Toolhelp32" : "-";
        rows[n].src = T.proc_src; n++;
        rows[n].label_key = "storage"; rows[n].hw = 0;
        rows[n].api = "GetDiskFreeSpaceEx"; rows[n].src = T.disk_src; n++;
        rows[n].label_key = "network"; rows[n].hw = 0;
        rows[n].api = T.has_dun ? "PerfStats Dial-Up Adapter" : T.has_iphlp ? "GetIfTable" : "-";
        rows[n].src = T.net_src; n++;
        rows[n].label_key = "nrg"; rows[n].hw = 0;
        rows[n].api = "GetSystemPowerStatus"; rows[n].src = T.has_power ? SRC_LIVE : SRC_NONE; n++;
        rows[n].label_key = "thm"; rows[n].hw = 0; rows[n].api = "-"; rows[n].src = SRC_SIM; n++;
        rows[n].label_key = "gpu"; rows[n].hw = 0; rows[n].api = "EnumDisplayDevices"; rows[n].src = SRC_SIM; n++;
        gh = 34 + n * 20 + 8;
        raised(L, y, IW, gh);
        text(L + 12, y + 8, IW - 24, 20, T0("srcTitle", s), f_title, C_INK, TA_START_);
        for (k = 0; k < n; k++) {
            int ry = y + 34 + k * 20, lw;
            Str st;
            if (rows[k].hw) H0(rows[k].label_key, s); else T0(rows[k].label_key, s);
            lw = text(L + 12, ry, 120, 18, s, f_bold, C_INK, TA_START_);
            (void)lw;
            T0(rows[k].src == SRC_LIVE ? "srcLive" : rows[k].src == SRC_SIM ? "srcSim" : "srcNone", st);
            if (s_eq(rows[k].api, "-")) s_cpy(b, st, SB); else J2(rows[k].api, st, b);
            text(L + 136, ry, IW - 148, 18, b, f_ui, rows[k].src == SRC_LIVE ? C_CPU_INK : rows[k].src == SRC_SIM ? C_NRG_INK : C_INK3, TA_END_);
        }
        y += gh + 6;
    }
    return y;
}

/* ---- render ------------------------------------------------------------------------------- */

static int render_content(void)
{
    int y = 6;
    g_nhot = g_register ? 0 : g_nhot;
    y = header(y) + 8;
    if (g_view == V_DETAIL) y = view_detail(y) + 6;
    else if (g_view == V_SETTINGS) y = view_settings(y);
    else y = view_main(y);
    y = footer(y) + 6;
    y = status_bar(y) + 6;
    return y;
}

static void render_frame(int W, int H)
{
    RECT r;
    COLORREF c1 = RGB(0, 0, 0x80), c2 = c1;   /* 18 px navy title bar */
    int k, cw = W - 2 * FRAME;
    Str s;
    HFONT o;
    if (c2 == 0 || c2 == c1) c2 = c1;
    r.left = 0; r.top = 0; r.right = W; r.bottom = H;
    DrawEdge(D, &r, EDGE_RAISED, BF_RECT);
    /* Caption gradient in 4 px bands, start colour at the reading start. */
    for (k = 0; k < cw; k += 4) {
        RECT b;
        double t = (double)k / cw;
        HBRUSH br;
        COLORREF c = blend(c2, c1, t);
        b.left = g_rtl ? W - FRAME - k - 4 : FRAME + k;
        b.right = b.left + 4;
        if (b.left < FRAME) b.left = FRAME;
        if (b.right > W - FRAME) b.right = W - FRAME;
        b.top = FRAME; b.bottom = FRAME + CAP_H;
        br = CreateSolidBrush(c);
        FillRect(D, &b, br);
        DeleteObject(br);
    }
    T0("winTitle", s);
    r.top = FRAME; r.bottom = FRAME + CAP_H;
    if (g_rtl) { r.left = FRAME + 22; r.right = W - FRAME - 4; }
    else { r.left = FRAME + 4; r.right = W - FRAME - 22; }
    o = (HFONT)SelectObject(D, f_cap);
    txt_draw(D, &r, s, TA_START_, RGB(0xFF, 0xFF, 0xFF));
    SelectObject(D, o);
    r.top = FRAME + 2; r.bottom = r.top + 14;
    r.left = g_rtl ? FRAME + 2 : W - FRAME - 2 - 16;
    r.right = r.left + 16;
    DrawFrameControl(D, &r, DFC_CAPTION, DFCS_CAPTIONCLOSE);
    if (g_register && g_nhot < MAX_HOT) { g_hot[g_nhot].r = r; g_hot[g_nhot].act = A_CLOSE; g_hot[g_nhot].arg = 0; g_nhot++; }
}

static void set_content_area(int W, int H)
{
    CA.left = FRAME; CA.top = FRAME + CAP_H + 1; CA.right = W - FRAME; CA.bottom = H - FRAME;
    if (g_sb_on) { if (g_rtl) CA.left += GetSystemMetrics(SM_CXVSCROLL); else CA.right -= GetSystemMetrics(SM_CXVSCROLL); }
    VW = CA.right - CA.left;
}

static int measure_content(void)
{
    HDC sdc = GetDC(g_wnd);
    HDC mdc = CreateCompatibleDC(sdc);
    RECT rc;
    int h;
    GetClientRect(g_wnd, &rc);
    if (rc.right < 10) rc.right = FLY_W;
    set_content_area(rc.right, rc.bottom);
    D = mdc;
    g_measure = 1; g_register = 0;
    h = render_content();
    g_measure = 0;
    DeleteDC(mdc);
    ReleaseDC(g_wnd, sdc);
    return h;
}

/* Fit the window to its content; scroll when the screen is too short. */
static void fit_window(void)
{
    RECT wa;
    int ch, wh, maxh, x, y, right = !g_rtl;
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
    {
        /* Some shells report the whole screen; keep clear of a bottom taskbar.
         * Open on the side where the notification area is (mirrored shells
         * put it on the left). */
        HWND tray = FindWindowA("Shell_TrayWnd", 0), notify;
        RECT tr;
        if (tray && GetWindowRect(tray, &tr) && tr.top > wa.top + 100 && tr.top < wa.bottom && tr.left <= wa.left + 4)
            wa.bottom = tr.top;
        notify = tray ? FindWindowExA(tray, 0, "TrayNotifyWnd", 0) : 0;
        if (notify && GetWindowRect(notify, &tr)) right = (tr.left + tr.right) / 2 > (wa.left + wa.right) / 2;
        else if (tray) right = !(GetWindowLongA(tray, GWL_EXSTYLE) & 0x00400000L /* WS_EX_LAYOUTRTL */);
    }
    g_sb_on = 0;
    ch = measure_content();
    maxh = (wa.bottom - wa.top) - 8;
    wh = ch + FRAME * 2 + CAP_H + 1;
    if (wh > maxh) { wh = maxh; g_sb_on = 1; }
    g_content_h = ch;
    x = right ? wa.right - FLY_W - 4 : wa.left + 4;
    y = wa.bottom - wh - 4;
    SetWindowPos(g_wnd, 0, x, y, FLY_W, wh, SWP_NOZORDER | SWP_NOACTIVATE);
    set_content_area(FLY_W, wh);
    if (g_sb_on) {
        SCROLLINFO si;
        int sx = g_rtl ? FRAME : FLY_W - FRAME - GetSystemMetrics(SM_CXVSCROLL);
        MoveWindow(g_sb, sx, CA.top, GetSystemMetrics(SM_CXVSCROLL), CA.bottom - CA.top, TRUE);
        si.cbSize = sizeof(si);
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin = 0; si.nMax = ch; si.nPage = (UINT)(CA.bottom - CA.top);
        if (g_scroll > ch - (int)si.nPage) g_scroll = ch - (int)si.nPage;
        if (g_scroll < 0) g_scroll = 0;
        si.nPos = g_scroll;
        SetScrollInfo(g_sb, SB_CTL, &si, TRUE);
        ShowWindow(g_sb, SW_SHOWNA);
    } else { g_scroll = 0; ShowWindow(g_sb, SW_HIDE); }
    InvalidateRect(g_wnd, 0, FALSE);
}

static void paint(HWND w)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(w, &ps), mdc;
    HBITMAP bmp, obmp;
    RECT rc;
    int sy, first = 1, W, H;
    HBRUSH face = GetSysColorBrush(COLOR_3DFACE);
    GetClientRect(w, &rc);
    W = rc.right; H = rc.bottom;
    set_content_area(W, H);
    mdc = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, W, STRIP_H);
    obmp = (HBITMAP)SelectObject(mdc, bmp);
    D = mdc;
    for (sy = ps.rcPaint.top - ps.rcPaint.top % STRIP_H; sy < ps.rcPaint.bottom || first; sy += STRIP_H) {
        RECT all;
        SetViewportOrgEx(mdc, 0, -sy, 0);
        all.left = 0; all.top = sy; all.right = W; all.bottom = sy + STRIP_H;
        FillRect(mdc, &all, face);
        g_register = first;
        SaveDC(mdc);
        IntersectClipRect(mdc, CA.left, CA.top, CA.right, CA.bottom);
        render_content();
        RestoreDC(mdc, -1);
        render_frame(W, H);
        SetViewportOrgEx(mdc, 0, 0, 0);
        BitBlt(dc, 0, sy, W, STRIP_H, mdc, 0, 0, SRCCOPY);
        first = 0;
        if (sy >= H) break;
    }
    g_register = 0;
    SelectObject(mdc, obmp);
    DeleteObject(bmp);
    DeleteDC(mdc);
    EndPaint(w, &ps);
}

/* ---- fonts and locale ------------------------------------------------------------------------ */

static HFONT mkfont(int px, int weight, int charset, const char *face)
{
    return CreateFontA(-px, 0, 0, 0, weight, 0, 0, 0, (BYTE)charset, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, face);
}

static int CALLBACK font_found(const LOGFONTA *lf, const TEXTMETRICA *tm, DWORD type, LPARAM lp)
{
    (void)lf; (void)tm; (void)type;
    *(int *)lp = 1;
    return 0;
}

/* Can this system draw the locale's text? */
static int locale_supported(const I18nLocaleData *d)
{
    LOGFONTA lf;
    HDC dc;
    int found = 0;
    if (!g_ansi) return 1;
    if (d->codepage == 1252 || (UINT)d->codepage == GetACP()) return 1;
    if (!IsValidCodePage((UINT)d->codepage)) return 0;
    ZeroMemory(&lf, sizeof(lf));
    lf.lfCharSet = (BYTE)d->charset;
    s_cpy(lf.lfFaceName, d->font_ui ? d->font_ui : "Tahoma", LF_FACESIZE);
    dc = GetDC(0);
    EnumFontFamiliesExA(dc, &lf, (FONTENUMPROCA)font_found, (LPARAM)&found, 0);
    ReleaseDC(0, dc);
    return found;
}

static void free_fonts(void)
{
    HFONT *f[8];
    int k;
    f[0] = &f_ui; f[1] = &f_bold; f[2] = &f_title; f[3] = &f_brand; f[4] = &f_hero; f[5] = &f_small; f[6] = &f_tile; f[7] = &f_cap;
    for (k = 0; k < 8; k++) if (*f[k]) { DeleteObject(*f[k]); *f[k] = 0; }
    for (k = 0; k < 8; k++) if (f_lang[k]) { DeleteObject(f_lang[k]); f_lang[k] = 0; }
    for (k = 0; k < 8; k++) if (f_lname[k]) { DeleteObject(f_lname[k]); f_lname[k] = 0; }
}

static void set_locale(const char *code)
{
    const char *face;
    int cs, k;
    LC = i18n_get(code);
    g_rtl = LC->d->rtl;
    txt_mode(g_ansi, LC->d->codepage, g_rtl);
    free_fonts();
    face = LC->d->font_ui ? LC->d->font_ui : "Tahoma";
    cs = LC->d->charset;
    f_ui = mkfont(11, FW_NORMAL, cs, face);
    f_bold = mkfont(11, FW_BOLD, cs, face);
    f_small = mkfont(10, FW_NORMAL, cs, face);
    f_title = mkfont(12, FW_BOLD, cs, face);
    f_brand = mkfont(16, FW_BOLD, cs, face);
    f_hero = mkfont(26, FW_BOLD, cs, face);
    f_tile = mkfont(13, FW_BOLD, cs, face);
    f_cap = mkfont(11, FW_BOLD, cs, face);
    for (k = 0; k < i18n_count() && k < 8; k++) {
        const I18nLocaleData *d = i18n_list(k);
        f_lang[k] = mkfont(12, FW_BOLD, d->charset, d->font_ui ? d->font_ui : "Tahoma");
        f_lname[k] = mkfont(11, FW_NORMAL, d->charset, d->font_ui ? d->font_ui : "Tahoma");
        g_lang_ok[k] = locale_supported(d);
    }
    {
        Str title;
        char t[256];
        T0("winTitle", title);
        if (g_ansi) { txt_to_ansi(title, t, sizeof(t)); SetWindowTextA(g_wnd, t); }
        else {
            /* Loaded by name: 9x user32 may lack the export. */
            typedef BOOL (WINAPI *SetTextW_t)(HWND, LPCWSTR);
            SetTextW_t f = (SetTextW_t)GetProcAddress(GetModuleHandleA("user32.dll"), "SetWindowTextW");
            WCHAR wt[128];
            utf8_to_w(title, wt, 128);
            if (f) f(g_wnd, wt);
        }
    }
}

static void save_prefs(void)
{
    HKEY k;
    DWORD unit = (DWORD)g_unit_f;
    if (RegCreateKeyA(HKEY_CURRENT_USER, "Software\\Pulse98", &k) != ERROR_SUCCESS) return;
    {
        const char *v = g_lang_system ? "system" : LC->d->code;
        RegSetValueExA(k, "Lang", 0, REG_SZ, (const BYTE *)v, (DWORD)s_len(v) + 1);
    }
    RegSetValueExA(k, "UnitF", 0, REG_DWORD, (const BYTE *)&unit, sizeof(unit));
    RegCloseKey(k);
}

static void load_prefs(char *lang, int cap)
{
    HKEY k;
    DWORD size = (DWORD)cap, type, unit = 0, us = sizeof(unit);
    lang[0] = 0;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Pulse98", 0, KEY_READ, &k) != ERROR_SUCCESS) return;
    if (RegQueryValueExA(k, "Lang", 0, &type, (BYTE *)lang, &size) != ERROR_SUCCESS) lang[0] = 0;
    if (RegQueryValueExA(k, "UnitF", 0, &type, (BYTE *)&unit, &us) == ERROR_SUCCESS) g_unit_f = unit != 0;
    RegCloseKey(k);
}

/* Windows language id to a locale code (only the primary language matters). */
static const char *system_lang(void)
{
    static const struct { WORD id; const char *code; } map[] = {
        { 0x01, "ar" }, { 0x02, "bg" }, { 0x03, "ca" }, { 0x04, "zh" }, { 0x05, "cs" }, { 0x06, "da" },
        { 0x07, "de" }, { 0x08, "el" }, { 0x09, "en" }, { 0x0A, "es" }, { 0x0B, "fi" }, { 0x0C, "fr" },
        { 0x0D, "he" }, { 0x0E, "hu" }, { 0x10, "it" }, { 0x11, "ja" }, { 0x12, "ko" }, { 0x13, "nl" },
        { 0x14, "nb" }, { 0x15, "pl" }, { 0x16, "pt" }, { 0x18, "ro" }, { 0x19, "ru" }, { 0x1D, "sv" },
        { 0x1E, "th" }, { 0x1F, "tr" }, { 0x20, "ur" }, { 0x22, "uk" }, { 0x29, "fa" }, { 0x2A, "vi" }
    };
    WORD p = (WORD)(GetUserDefaultLangID() & 0x3FF);
    int k;
    for (k = 0; k < (int)(sizeof(map) / sizeof(map[0])); k++) if (map[k].id == p) return map[k].code;
    return "en";
}

static void tray_update(DWORD msg);
static void fit_window(void);

/* Switches language live. code NULL with follow_system set: use the
 * Windows UI language, falling back to English. save: write the choice. */
static void apply_lang(const char *code, int follow_system, int save)
{
    g_lang_system = follow_system;
    set_locale(follow_system ? system_lang() : code);
    if (g_ansi && !locale_supported(LC->d)) set_locale("en");
    if (!save) return;
    save_prefs();
    tray_update(NIM_MODIFY);
    fit_window();
}

/* ---- tray ------------------------------------------------------------------------------------ */

typedef struct { DWORD cbSize; HWND hWnd; UINT uID; UINT uFlags; UINT uCallbackMessage; HICON hIcon; CHAR szTip[64]; } NID98A;
typedef struct { DWORD cbSize; HWND hWnd; UINT uID; UINT uFlags; UINT uCallbackMessage; HICON hIcon; WCHAR szTip[64]; } NID98W;

/* 3x5 digit glyphs, one row per byte (bits 2..0 = left..right). */
static const BYTE g_digits[10][5] = {
    { 7, 5, 5, 5, 7 }, { 2, 6, 2, 2, 7 }, { 7, 1, 7, 4, 7 }, { 7, 1, 3, 1, 7 }, { 5, 5, 7, 1, 1 },
    { 7, 4, 7, 1, 7 }, { 7, 4, 7, 5, 7 }, { 7, 1, 1, 2, 2 }, { 7, 5, 7, 5, 7 }, { 7, 5, 7, 1, 7 }
};

/* Tier 1 tray: icon 1 is a 16x16 green-on-black bar graph (wave) of the last
 * CPU samples, icon 2 shows CPU percent as digits. Both open the flyout. */
static HICON make_tray_icon(int digits)
{
    HDC sdc = GetDC(0), mdc = CreateCompatibleDC(sdc);
    HBITMAP color = CreateCompatibleBitmap(sdc, 16, 16), mask, old;
    BYTE zeros[32];
    ICONINFO ii;
    RECT r;
    HBRUSH g;
    HICON ic;
    int k;
    ZeroMemory(zeros, sizeof(zeros));
    mask = CreateBitmap(16, 16, 1, 1, zeros);
    old = (HBITMAP)SelectObject(mdc, color);
    r.left = 0; r.top = 0; r.right = 16; r.bottom = 16;
    FillRect(mdc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
    g = CreateSolidBrush(RGB(0, 0xFF, 0));
    if (!digits) {
        for (k = 0; k < 5; k++) {
            int idx = s_n - 5 + k, h;
            double v = idx >= 0 && idx < s_n ? s_cpu[idx] : 0;
            h = (int)(v / 100.0 * 13 + 0.5);
            if (h < 1) h = 1;
            if (h > 14) h = 14;
            r.left = 1 + k * 3; r.right = r.left + 2; r.bottom = 15; r.top = 15 - h;
            FillRect(mdc, &r, g);
        }
    } else {
        int v = (int)(T.cpu + 0.5), nd, d[3], sx, cw, row, col;
        if (v > 100) v = 100;
        if (v < 0) v = 0;
        nd = v >= 100 ? 3 : v >= 10 ? 2 : 1;
        d[0] = v / 100; d[1] = (v / 10) % 10; d[2] = v % 10;
        cw = nd == 3 ? 1 : 2;                      /* glyph scale across */
        sx = (16 - (nd * 3 * cw + (nd - 1))) / 2;
        for (k = 0; k < nd; k++) {
            int digit = d[3 - nd + k];
            for (row = 0; row < 5; row++)
                for (col = 0; col < 3; col++)
                    if (g_digits[digit][row] & (4 >> col)) {
                        r.left = sx + k * (3 * cw + 1) + col * cw; r.right = r.left + cw;
                        r.top = 3 + row * 2; r.bottom = r.top + 2;
                        FillRect(mdc, &r, g);
                    }
        }
    }
    DeleteObject(g);
    SelectObject(mdc, old);
    ii.fIcon = TRUE; ii.xHotspot = 0; ii.yHotspot = 0; ii.hbmMask = mask; ii.hbmColor = color;
    ic = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(color);
    DeleteDC(mdc);
    ReleaseDC(0, sdc);
    return ic;
}

static void tray_set(DWORD msg, UINT id, HICON ic, const char *tip)
{
    typedef BOOL (WINAPI *NotifyW_t)(DWORD, void *);
    NotifyW_t fw = (NotifyW_t)GetProcAddress(GetModuleHandleA("shell32.dll"), "Shell_NotifyIconW");
    if (g_ansi || !T.is_nt || !fw) {
        NID98A n;
        ZeroMemory(&n, sizeof(n));
        n.cbSize = sizeof(n); n.hWnd = g_wnd; n.uID = id;
        n.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE; n.uCallbackMessage = WM_TRAY; n.hIcon = ic;
        txt_to_ansi(tip, n.szTip, sizeof(n.szTip));
        Shell_NotifyIconA(msg, (PNOTIFYICONDATAA)&n);
    } else {
        NID98W n;
        ZeroMemory(&n, sizeof(n));
        n.cbSize = sizeof(n); n.hWnd = g_wnd; n.uID = id;
        n.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE; n.uCallbackMessage = WM_TRAY; n.hIcon = ic;
        utf8_to_w(tip, n.szTip, 64);
        fw(msg, &n);
    }
}

static void tray_update(DWORD msg)
{
    HICON wave, pct, old1 = g_tray_icon, old2 = g_tray_pct;
    Str tip, v;
    if (!g_tray) return;
    wave = make_tray_icon(0);
    pct = make_tray_icon(1);
    T0("barCpu", tip); s_cat(tip, " ", SB); s_cat(tip, PCT(T.cpu, 0, v), SB);
    /* Order: percent first so the wave sits next to it, as in the prototype. */
    tray_set(msg, ID_TRAY_PCT, pct, tip);
    tray_set(msg, ID_TRAY, wave, tip);
    g_tray_icon = wave;
    g_tray_pct = pct;
    if (old1) DestroyIcon(old1);
    if (old2) DestroyIcon(old2);
}

static void tray_remove(void)
{
    NID98A n;
    if (!g_tray) return;
    ZeroMemory(&n, sizeof(n));
    n.cbSize = sizeof(n); n.hWnd = g_wnd; n.uID = ID_TRAY;
    Shell_NotifyIconA(NIM_DELETE, (PNOTIFYICONDATAA)&n);
    n.uID = ID_TRAY_PCT;
    Shell_NotifyIconA(NIM_DELETE, (PNOTIFYICONDATAA)&n);
}

static void show_flyout(int show)
{
    if (show) {
        fit_window();
        ShowWindow(g_wnd, SW_SHOW);
        SetForegroundWindow(g_wnd);
    } else {
        ShowWindow(g_wnd, SW_HIDE);
        g_hidden_at = GetTickCount();
        g_view = V_MAIN; g_confirm = 0; g_hover = -1;
    }
}

static void quit_app(void)
{
    tray_remove();
    DestroyWindow(g_wnd);
}

/* ---- sampling ------------------------------------------------------------------------------- */

/* Settings demo switches: drawn over real readings, never instead of them. */
static void overlay(void)
{
    static unsigned long seed = 12345;
    double j;
    seed = seed * 1103515245UL + 12345UL;
    j = (double)((seed >> 8) & 0xFFFF) / 65535.0 - 0.5;
    if (g_sim_charge) { T.has_batt = 1; T.ac_online = 1; T.batt_pct = 84; T.batt_secs = -1; }
    if (g_sim_hog && !g_sim_ended && T.napps < TM_MAX_APPS) {
        TmApp *a = &T.apps[T.napps++];
        ZeroMemory(a, sizeof(*a));
        s_cpy(a->name, "Norton AntiVirus", TM_NAME);
        s_cpy(a->exe, SIM_EXE, TM_NAME);
        a->procs = 3; a->threads = 9; a->pid = 0xFFF4A2B1UL;
        a->mem = 11.5 * 1048576.0;
        a->cpu = 56.3 + j * 6;
        if (T.cpu < 62) {
            T.cpu = 64 + j * 6;
            T.cpu_user = T.cpu * 0.66;
            T.cpu_sys = T.cpu - T.cpu_user;
        }
    }
}

static void sample(int force)
{
    DWORD now = GetTickCount();
    double secs = g_last_tick ? (now - g_last_tick) / 1000.0 : 0;
    int k, hist;
    if (!g_live && !force) return;
    tm_sample(&T, secs);
    g_last_tick = now;
    overlay();
    /* Sparks every tick, history every 5 s */
    push(s_cpu, SPARK_N, &s_n, T.cpu, 0);
    push(s_mem, SPARK_N, &s_n, T.mem_load, 0);
    push(s_nrg, SPARK_N, &s_n, T.has_batt ? T.batt_pct : 100, 0);
    push(s_thm, SPARK_N, &s_n, T.cpu_temp, 1);
    hist = !g_last_hist || now - g_last_hist >= HIST_MS;
    if (hist) {
        g_last_hist = now;
        push(h_cpu, HIST_N, &h_n, T.cpu, 0);
        push(h_mem, HIST_N, &h_n, T.mem_load, 0);
        push(h_nrg, HIST_N, &h_n, T.has_batt ? T.batt_pct : 100, 0);
        push(h_thm, HIST_N, &h_n, T.cpu_temp, 0);
        push(h_gpu, HIST_N, &h_n, T.gpu_load, 0);
        push(h_rd, HIST_N, &h_n, T.disk_read, 0);
        push(h_wr, HIST_N, &h_n, T.disk_write, 0);
        push(h_dn, HIST_N, &h_n, T.net_down, 0);
        push(h_up, HIST_N, &h_n, T.net_up, 1);
        /* Per-app history for the busiest apps and the one on screen */
        {
            int idx[5], n = top_apps(idx, 5);
            for (k = 0; k < n; k++) {
                AppHist *h = app_hist(T.apps[idx[k]].exe, 1);
                push(h->v, HIST_N, &h->n, T.proc_cpu ? T.apps[idx[k]].cpu : T.apps[idx[k]].threads, 1);
                h->used = now;
            }
            if (g_view == V_DETAIL && g_detail == D_APP) {
                const TmApp *a = find_app(g_detail_exe);
                AppHist *h = app_hist(g_detail_exe, 1);
                int seen = 0;
                for (k = 0; k < n; k++) if (s_ieq(T.apps[idx[k]].exe, g_detail_exe)) seen = 1;
                if (a && !seen) { push(h->v, HIST_N, &h->n, T.proc_cpu ? a->cpu : a->threads, 1); h->used = now; }
            }
        }
    }
    /* Hog: one app above 50% of total CPU for over 2 minutes */
    if (T.proc_cpu) {
        const TmApp *top = 0;
        for (k = 0; k < T.napps; k++) if (!top || T.apps[k].cpu > top->cpu) top = &T.apps[k];
        if (top && top->cpu >= g_hog_pct) {
            if (!s_ieq(top->exe, g_hog_exe)) { s_cpy(g_hog_exe, top->exe, TM_NAME); g_hog_since = now; g_hog_on = 0; g_hog_dismissed = 0; }
            else if (now - g_hog_since >= HOG_MS) g_hog_on = 1;
        } else { g_hog_exe[0] = 0; g_hog_on = 0; }
    }
    if (g_sim_hog && !g_sim_ended) {
        if (!s_ieq(g_hog_exe, SIM_EXE)) { s_cpy(g_hog_exe, SIM_EXE, TM_NAME); g_hog_dismissed = 0; }
        g_hog_on = 1;
    } else if (s_ieq(g_hog_exe, SIM_EXE)) { g_hog_exe[0] = 0; g_hog_on = 0; }
}

/* ---- input ------------------------------------------------------------------------------------ */

static void open_view(int view, int detail)
{
    g_view = view; g_detail = detail; g_hover = -1; g_scroll = 0;
    fit_window();
}

static void act(int a, int arg)
{
    Str s, m;
    switch (a) {
    case A_CLOSE:
        if (g_show_flyout_only && !g_tray) quit_app(); else show_flyout(0);
        return;
    case A_LANG: {
        const I18nLocaleData *d = i18n_list(arg);
        if (!d) return;
        if (!g_lang_ok[arg]) {
            T0("arabicNa", s);
            s_cpy(g_toast, s, sizeof(g_toast));
            g_toast_until = GetTickCount() + 2800;
            break;
        }
        if (s_eq(d->code, LC->d->code)) {
            /* With two locales the pill acts as a toggle */
            if (i18n_count() == 2) d = i18n_list(1 - arg);
            if (!g_lang_ok[s_eq(d->code, i18n_list(0)->code) ? 0 : 1]) return;
        }
        apply_lang(d->code, 0, 1);
        return; }
    case A_LANGMENU: {
        HMENU menu = CreatePopupMenu();
        POINT pt;
        int k, cmd;
        for (k = 0; k < i18n_count(); k++) {
            const I18nLocaleData *d = i18n_list(k);
            char item[128];
            Str u;
            s_cpy(u, d->label, SB); s_cat(u, "  ", SB); s_cat(u, d->name, SB);
            if (!T.is_nt) { txt_mode(1, d->codepage, d->rtl); txt_to_ansi(u, item, sizeof(item)); txt_mode(g_ansi, LC->d->codepage, g_rtl); }
            else s_cpy(item, u, sizeof(item));
            AppendMenuA(menu, MF_STRING | (s_eq(d->code, LC->d->code) ? MF_CHECKED : 0) | (g_lang_ok[k] ? 0 : MF_GRAYED), (UINT)(k + 1), item);
        }
        GetCursorPos(&pt);
        cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_wnd, 0);
        DestroyMenu(menu);
        if (cmd > 0) apply_lang(i18n_list(cmd - 1)->code, 0, 1);
        return; }
    case A_SETTINGS: open_view(g_view == V_SETTINGS ? V_MAIN : V_SETTINGS, g_detail); return;
    case A_CARD: open_view(V_DETAIL, arg); return;
    case A_BACK: g_confirm = 0; open_view(V_MAIN, g_detail); return;
    case A_SORT: g_sort = arg; break;
    case A_ROW:
        if (arg < g_nrows) { s_cpy(g_detail_exe, g_rows[arg], TM_NAME); open_view(V_DETAIL, D_APP); }
        return;
    case A_ENDHOG: g_confirm = 1; s_cpy(g_confirm_exe, g_hog_exe, TM_NAME); fit_window(); return;
    case A_ENDAPP: g_confirm = 1; s_cpy(g_confirm_exe, g_detail_exe, TM_NAME); fit_window(); return;
    case A_DISMISS: g_hog_dismissed = 1; fit_window(); return;
    case A_NO: g_confirm = 0; fit_window(); return;
    case A_YES: {
        const TmApp *ap = find_app(g_confirm_exe);
        g_confirm = 0;
        if (ap) {
            Str nm;
            s_cpy(nm, app_name(ap), SB);
            if (g_nended < 12) {
                Ended *e = &g_ended[g_nended++];
                s_cpy(e->exe, ap->exe, TM_NAME);
                e->sim = s_ieq(ap->exe, SIM_EXE);
                if (!e->sim) tm_app_path(ap->exe, e->path, MAX_PATH); else e->path[0] = 0;
            }
            if (s_ieq(ap->exe, SIM_EXE)) { g_sim_ended = 1; T1("toastEnded", "app", nm, s); }
            else if (tm_end_process(&T, ap)) T1("toastEnded", "app", nm, s);
            else { if (g_nended) g_nended--; T1("endFailed", "app", nm, s); }
            s_cpy(g_toast, s, sizeof(g_toast));
            g_toast_until = GetTickCount() + 2800;
            if (s_ieq(g_confirm_exe, g_hog_exe)) { g_hog_on = 0; g_hog_exe[0] = 0; }
        }
        if (g_view == V_DETAIL && g_detail == D_APP) g_view = V_MAIN;
        sample(1);
        fit_window();
        return; }
    case A_MONITOR:
        WinExec(T.is_nt ? "taskmgr.exe" : "sysmon.exe", SW_SHOWNORMAL);
        H0(T.is_nt ? "taskManager" : "systemMonitor", m);
        T1("toastMonitor", "monitor", m, s);
        s_cpy(g_toast, s, sizeof(g_toast));
        g_toast_until = GetTickCount() + 2800;
        fit_window();
        return;
    case A_QUIT: quit_app(); return;
    case A_LIVE: g_live = !g_live; break;
    case A_LANGSET:
        if (arg == 0) apply_lang(0, 1, 1);
        else if (i18n_list(arg - 1) && g_lang_ok[arg - 1]) apply_lang(i18n_list(arg - 1)->code, 0, 1);
        return;
    case A_SIMHOG: g_sim_hog = !g_sim_hog; g_sim_ended = 0; sample(1); break;
    case A_SIMCHG: g_sim_charge = !g_sim_charge; sample(1); break;
    case A_RESTORE: {
        int k;
        for (k = 0; k < g_nended; k++) {
            if (g_ended[k].sim) g_sim_ended = 0;
            else if (g_ended[k].path[0]) {
                char q[MAX_PATH + 4];
                q[0] = '"'; s_cpy(q + 1, g_ended[k].path, MAX_PATH); s_cat(q, "\"", sizeof(q));
                WinExec(q, SW_SHOWNORMAL);
            }
        }
        g_nended = 0;
        g_hog_dismissed = 0;
        T0("toastRestored", s);
        s_cpy(g_toast, s, sizeof(g_toast));
        g_toast_until = GetTickCount() + 2800;
        sample(1);
        fit_window();
        return; }
    case A_UNIT: g_unit_f = arg; save_prefs(); break;
    default: return;
    }
    InvalidateRect(g_wnd, 0, FALSE);
}

static int hit(int x, int y, int *arg)
{
    int k;
    for (k = g_nhot - 1; k >= 0; k--) {
        RECT r = g_hot[k].r;
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) {
            if (g_hot[k].act != A_CLOSE && (y < CA.top || y >= CA.bottom)) continue;
            *arg = g_hot[k].arg;
            return g_hot[k].act;
        }
    }
    return A_NONE;
}

static void scroll_to(int pos)
{
    int page = CA.bottom - CA.top, maxp = g_content_h - page;
    if (!g_sb_on) return;
    if (pos > maxp) pos = maxp;
    if (pos < 0) pos = 0;
    g_scroll = pos;
    SetScrollPos(g_sb, SB_CTL, pos, TRUE);
    InvalidateRect(g_wnd, 0, FALSE);
}

typedef BOOL (WINAPI *TrackMouse_t)(LPTRACKMOUSEEVENT);

static LRESULT CALLBACK wndproc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    int a, arg = 0;
    switch (msg) {
    case WM_PAINT: paint(w); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_TIMER:
        sample(0);
        if (g_tray) tray_update(NIM_MODIFY);
        if (IsWindowVisible(w)) {
            int before = g_content_h;
            if (measure_content() != before) fit_window();
            else InvalidateRect(w, 0, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN:
        a = hit((short)LOWORD(lp), (short)HIWORD(lp), &arg);
        if (a) act(a, arg);
        return 0;
    case WM_MOUSEMOVE: {
        int old = g_hover;
        a = hit((short)LOWORD(lp), (short)HIWORD(lp), &arg);
        g_hover = a == A_ROW ? arg : -1;
        if (g_hover != old) {
            TrackMouse_t tme = (TrackMouse_t)GetProcAddress(GetModuleHandleA("user32.dll"), "TrackMouseEvent");
            if (tme) { TRACKMOUSEEVENT t; t.cbSize = sizeof(t); t.dwFlags = TME_LEAVE; t.hwndTrack = w; t.dwHoverTime = 0; tme(&t); }
            InvalidateRect(w, 0, FALSE);
        }
        SetCursor(LoadCursor(0, a ? IDC_HAND : IDC_ARROW));
        return 0; }
    case 0x02A3: /* WM_MOUSELEAVE */
        if (g_hover != -1) { g_hover = -1; InvalidateRect(w, 0, FALSE); }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) return 1;
        break;
    case 0x020A: /* WM_MOUSEWHEEL */
        scroll_to(g_scroll - (short)HIWORD(wp) / 120 * 40);
        return 0;
    case WM_VSCROLL: {
        int page = CA.bottom - CA.top;
        switch (LOWORD(wp)) {
        case SB_LINEUP: scroll_to(g_scroll - 20); break;
        case SB_LINEDOWN: scroll_to(g_scroll + 20); break;
        case SB_PAGEUP: scroll_to(g_scroll - page); break;
        case SB_PAGEDOWN: scroll_to(g_scroll + page); break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: scroll_to((short)HIWORD(wp)); break;
        case SB_TOP: scroll_to(0); break;
        case SB_BOTTOM: scroll_to(g_content_h); break;
        }
        return 0; }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE || wp == VK_BACK) {
            if (g_confirm) { g_confirm = 0; fit_window(); }
            else if (g_view != V_MAIN) open_view(V_MAIN, g_detail);
            else if (wp == VK_ESCAPE) act(A_CLOSE, 0);
        } else if (wp == VK_PRIOR) scroll_to(g_scroll - (CA.bottom - CA.top));
        else if (wp == VK_NEXT) scroll_to(g_scroll + (CA.bottom - CA.top));
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE && g_tray && !g_show_flyout_only && IsWindowVisible(w)) show_flyout(0);
        return 0;
    case WM_TRAY:
        if (lp == WM_LBUTTONUP) {
            if (IsWindowVisible(w)) show_flyout(0);
            else if (GetTickCount() - g_hidden_at > 300) show_flyout(1);
        } else if (lp == WM_RBUTTONUP) {
            HMENU m = CreatePopupMenu();
            POINT pt;
            Str s;
            char t[128];
            int cmd;
            txt_to_ansi(T0("openPulse", s), t, sizeof(t)); AppendMenuA(m, MF_STRING, 1, t);
            AppendMenuA(m, MF_SEPARATOR, 0, 0);
            txt_to_ansi(T0("quit", s), t, sizeof(t)); AppendMenuA(m, MF_STRING, 2, t);
            SetMenuDefaultItem(m, 1, FALSE);
            GetCursorPos(&pt);
            SetForegroundWindow(w);
            cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, w, 0);
            PostMessageA(w, WM_NULL, 0, 0);
            DestroyMenu(m);
            if (cmd == 1) show_flyout(1);
            else if (cmd == 2) quit_app();
        }
        return 0;
    case WM_DESTROY:
        KillTimer(w, ID_TIMER);
        tm_shutdown(&T);
        free_fonts();
        if (g_tray_icon) DestroyIcon(g_tray_icon);
        if (g_tray_pct) DestroyIcon(g_tray_pct);
        PostQuitMessage(0);
        return 0;
    default:
        if (msg == g_taskbar_msg && g_taskbar_msg) { if (g_tray) tray_update(NIM_ADD); return 0; }
        break;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

/* ---- entry ----------------------------------------------------------------------------------- */

static const char *arg_value(const char *cmd, const char *name, char *out, int cap)
{
    const char *p = s_find(cmd, name);
    int k = 0;
    if (!p) return 0;
    p += s_len(name);
    while (*p && *p != ' ' && k < cap - 1) out[k++] = *p++;
    out[k] = 0;
    return out;
}

static void warn_dbg(const char *m) { OutputDebugStringA(m); OutputDebugStringA("\n"); }

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASSA wc;
    MSG m;
    char lang[32], view[16], sort[8];
    HANDLE mutex;
    (void)prev; (void)show;
    g_inst = inst;
    i18n_warn = warn_dbg;

    g_show_flyout_only = s_find(cmd, "--show-flyout") != 0;
    mutex = CreateMutexA(0, FALSE, "Pulse98.Running");
    if (!g_show_flyout_only && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowA("Pulse98Flyout", 0);
        if (other) PostMessageA(other, WM_TRAY, 0, WM_LBUTTONUP);
        return 0;
    }

    tm_init(&T);
    g_ansi = !T.is_nt;
    if (s_find(cmd, "--unicode")) g_ansi = 0;
    if (s_find(cmd, "--ansi")) g_ansi = 1;

    g_app_icon = LoadIconA(inst, MAKEINTRESOURCEA(1));
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hIcon = g_app_icon;
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.lpszClassName = "Pulse98Flyout";
    RegisterClassA(&wc);
    g_wnd = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, "Pulse98Flyout", "Pulse",
                            WS_POPUP | WS_CLIPCHILDREN, 0, 0, FLY_W, 600, 0, 0, inst, 0);
    g_sb = CreateWindowExA(0, "SCROLLBAR", 0, WS_CHILD | SBS_VERT, 0, 0, 16, 100, g_wnd, 0, inst, 0);

    load_prefs(lang, sizeof(lang));
    if (arg_value(cmd, "--lang=", lang, sizeof(lang))) { /* command line wins */ }
    g_lang_system = !lang[0] || s_eq(lang, "system");
    apply_lang(g_lang_system ? 0 : lang, g_lang_system, 0);

    if (arg_value(cmd, "--hog-pct=", sort, sizeof(sort))) {
        int v = 0, k;
        for (k = 0; sort[k] >= '0' && sort[k] <= '9'; k++) v = v * 10 + (sort[k] - '0');
        if (v > 0 && v <= 100) g_hog_pct = v;
    }
    g_sim_hog = s_find(cmd, "--sim-hog") != 0;
    g_sim_charge = s_find(cmd, "--sim-charging") != 0;
    if (arg_value(cmd, "--sort=", sort, sizeof(sort))) g_sort = s_eq(sort, "mem") ? S_MEM : S_CPU;
    if (arg_value(cmd, "--view=", view, sizeof(view))) {
        static const char *names[] = { "cpu", "mem", "nrg", "thm", "gpu", "ssd", "net" };
        int k;
        if (s_eq(view, "settings")) g_view = V_SETTINGS;
        for (k = 0; k < 7; k++) if (s_eq(view, names[k])) { g_view = V_DETAIL; g_detail = k; }
    }

    sample(1);
    g_taskbar_msg = RegisterWindowMessageA("TaskbarCreated");
    if (!g_show_flyout_only || s_find(cmd, "--tray")) { g_tray = 1; tray_update(NIM_ADD); }
    SetTimer(g_wnd, ID_TIMER, TICK_MS, 0);
    if (g_show_flyout_only) show_flyout(1);

    while (GetMessageA(&m, 0, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    if (mutex) CloseHandle(mutex);
    return 0;
}
