/*
 * Pulse locale engine, C89 port of i18n/pulse-i18n.js: plural categories
 * from each locale's CLDR rules, {placeholder} fill, FSI/PDI isolation of
 * inserted values in RTL, locale then fallback then key.
 * No CRT use.
 */
#include "i18n.h"

#define MAX_LOCALES 32

#define RULE_POOL 8

static I18nLocale g_loc[MAX_LOCALES];
static PlRules g_rule_pool[RULE_POOL];
static PlRules g_no_rules;          /* everything is "other" */
static int g_rules_used;
void (*i18n_warn)(const char *msg) = 0;

static const char FSI[] = "\xE2\x81\xA8";   /* U+2068 */
static const char PDI[] = "\xE2\x81\xA9";   /* U+2069 */

static int s_cmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int s_len(const char *s) { int n = 0; while (s[n]) n++; return n; }

static int s_prefix_eq(const char *a, const char *b, int n)
{
    int k;
    for (k = 0; k < n; k++) if (a[k] != b[k]) return 0;
    return b[n] == 0;
}

static const char *kv_find(const I18nKV *kv, int n, const char *key)
{
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2, c = s_cmp(key, kv[mid].k);
        if (!c) return kv[mid].v;
        if (c < 0) hi = mid - 1; else lo = mid + 1;
    }
    return 0;
}

static int locale_index(const char *code)
{
    int k;
    for (k = 0; k < i18n_catalog_count && k < MAX_LOCALES; k++)
        if (!s_cmp(i18n_catalog[k]->code, code)) return k;
    return -1;
}

static int resolve(const char *code)
{
    char buf[32];
    int k, n, idx;
    if (code) {
        idx = locale_index(code);
        if (idx >= 0) return idx;
        for (n = 0; code[n] && n < 31; n++) buf[n] = code[n] == '_' ? '-' : code[n];
        buf[n] = 0;
        for (k = n - 1; k > 0; k--) {
            if (buf[k] == '-') {
                buf[k] = 0;
                idx = locale_index(buf);
                if (idx >= 0) return idx;
            }
        }
    }
    idx = locale_index("en");
    return idx >= 0 ? idx : 0;
}

const I18nLocale *i18n_get(const char *code)
{
    int idx = resolve(code);
    I18nLocale *L = &g_loc[idx];
    if (!L->ready) {
        const I18nLocaleData *d = i18n_catalog[idx];
        const char *fb = d->fallback ? d->fallback : (s_cmp(d->code, "en") ? "en" : 0);
        int c;
        L->d = d;
        L->ready = 1;
        L->fb = 0;
        L->rules = g_rules_used < RULE_POOL ? &g_rule_pool[g_rules_used++] : &g_no_rules;
        if (L->rules != &g_no_rules) {
            for (c = 0; c < PL_OTHER; c++) {
                L->rules->has[c] = 0;
                if (d->rules[c] && pl_compile(d->rules[c], &L->rules->rule[c])) L->rules->has[c] = 1;
            }
        }
        if (fb && locale_index(fb) >= 0 && locale_index(fb) != idx) L->fb = i18n_get(fb);
    }
    return L;
}

int i18n_count(void) { return i18n_catalog_count; }

const I18nLocaleData *i18n_list(int index)
{
    /* Stable order: base locale first, then "order", then code. */
    int used[MAX_LOCALES];
    int k, pick = -1, round;
    for (k = 0; k < MAX_LOCALES; k++) used[k] = 0;
    for (round = 0; round <= index; round++) {
        pick = -1;
        for (k = 0; k < i18n_catalog_count && k < MAX_LOCALES; k++) {
            const I18nLocaleData *a, *b;
            int oa, ob;
            if (used[k]) continue;
            if (pick < 0) { pick = k; continue; }
            a = i18n_catalog[k]; b = i18n_catalog[pick];
            oa = a->order != -1000 ? a->order : (!s_cmp(a->code, "en") ? -1 : 0);
            ob = b->order != -1000 ? b->order : (!s_cmp(b->code, "en") ? -1 : 0);
            if (oa < ob || (oa == ob && s_cmp(a->code, b->code) < 0)) pick = k;
        }
        if (pick < 0) return 0;
        used[pick] = 1;
    }
    return i18n_catalog[pick];
}

const char *i18n_lookup(const I18nLocale *L, int section, const char *key)
{
    while (L) {
        const char *v = 0;
        if (section == I18N_STRINGS) v = kv_find(L->d->strings, L->d->nstrings, key);
        else if (section == I18N_HARDWARE) v = kv_find(L->d->hardware, L->d->nhardware, key);
        else v = kv_find(L->d->apps, L->d->napps, key);
        if (v) return v;
        if (section == I18N_APPS) return 0;   /* apps never fall back */
        L = L->fb;
    }
    return 0;
}

/* ---- output buffer -------------------------------------------------- */

typedef struct { char *p; int size; int len; } Buf;

static void b_put(Buf *b, const char *s, int n)
{
    int k;
    for (k = 0; k < n && b->len < b->size - 1; k++) b->p[b->len++] = s[k];
    b->p[b->len] = 0;
}
static void b_str(Buf *b, const char *s) { b_put(b, s, s_len(s)); }

/* RTL letters: U+0590..U+08FF, U+FB1D..U+FDFF, U+FE70..U+FEFF */
static int has_rtl(const char *s)
{
    const unsigned char *u = (const unsigned char *)s;
    while (*u) {
        unsigned long cp;
        if (*u < 0x80) { u++; continue; }
        if ((*u & 0xE0) == 0xC0 && u[1]) { cp = ((unsigned long)(*u & 0x1F) << 6) | (u[1] & 0x3F); u += 2; }
        else if ((*u & 0xF0) == 0xE0 && u[1] && u[2]) { cp = ((unsigned long)(*u & 0x0F) << 12) | ((unsigned long)(u[1] & 0x3F) << 6) | (u[2] & 0x3F); u += 3; }
        else { u++; continue; }
        if ((cp >= 0x0590 && cp <= 0x08FF) || (cp >= 0xFB1D && cp <= 0xFDFF) || (cp >= 0xFE70 && cp <= 0xFEFF)) return 1;
    }
    return 0;
}

char *i18n_format(const I18nLocale *L, const char *tpl, const I18nParam *p, int np, char *out, int size)
{
    Buf b;
    const char *s = tpl;
    b.p = out; b.size = size; b.len = 0;
    if (size > 0) out[0] = 0;
    if (!tpl) return out;
    while (*s) {
        if (*s == '{') {
            const char *e = s + 1;
            while ((*e >= 'a' && *e <= 'z') || (*e >= 'A' && *e <= 'Z') || (*e >= '0' && *e <= '9') || *e == '_') e++;
            if (*e == '}' && e > s + 1) {
                int k, found = -1;
                for (k = 0; k < np; k++) if (s_prefix_eq(s + 1, p[k].name, (int)(e - s - 1))) { found = k; break; }
                if (found >= 0) {
                    const char *v = p[found].value ? p[found].value : "";
                    if (L->d->rtl || has_rtl(v)) { b_str(&b, FSI); b_str(&b, v); b_str(&b, PDI); }
                    else b_str(&b, v);
                    s = e + 1;
                    continue;
                }
                b_put(&b, s, (int)(e - s + 1));
                s = e + 1;
                continue;
            }
        }
        b_put(&b, s, 1);
        s++;
    }
    return out;
}

static void miss(const I18nLocale *L, const char *section, const char *key)
{
    /* One warning per key: remember up to 64 misses. */
    static const char *seen[64];
    static int nseen = 0;
    char msg[160];
    Buf b;
    int k;
    for (k = 0; k < nseen; k++) if (seen[k] == key) return;
    if (nseen < 64) seen[nseen++] = key;
    if (!i18n_warn) return;
    b.p = msg; b.size = sizeof(msg); b.len = 0;
    b_str(&b, "[i18n] missing "); b_str(&b, section); b_str(&b, "."); b_str(&b, key);
    b_str(&b, " in "); b_str(&b, L->d->code);
    i18n_warn(msg);
}

char *i18n_t(const I18nLocale *L, const char *key, const I18nParam *p, int np, char *out, int size)
{
    const char *s = i18n_lookup(L, I18N_STRINGS, key);
    if (!s) { miss(L, "strings", key); s = key; }
    return i18n_format(L, s, p, np, out, size);
}

char *i18n_hw(const I18nLocale *L, const char *key, const I18nParam *p, int np, char *out, int size)
{
    const char *s = i18n_lookup(L, I18N_HARDWARE, key);
    if (!s) { miss(L, "hardware", key); s = key; }
    return i18n_format(L, s, p, np, out, size);
}

const char *i18n_app(const I18nLocale *L, const char *name)
{
    const char *s = i18n_lookup(L, I18N_APPS, name);
    return s ? s : name;
}

/* ---- numbers ---------------------------------------------------------- */

static unsigned long zero_digit(const char *ns)
{
    if (!ns || !s_cmp(ns, "latn")) return '0';
    if (!s_cmp(ns, "arab")) return 0x0660;
    if (!s_cmp(ns, "arabext")) return 0x06F0;
    if (!s_cmp(ns, "deva")) return 0x0966;
    if (!s_cmp(ns, "beng")) return 0x09E6;
    if (!s_cmp(ns, "thai")) return 0x0E50;
    if (!s_cmp(ns, "mymr")) return 0x1040;
    if (!s_cmp(ns, "fullwide")) return 0xFF10;
    return '0';
}

static void put_cp(Buf *b, unsigned long cp)
{
    char t[4];
    if (cp < 0x80) { t[0] = (char)cp; b_put(b, t, 1); }
    else if (cp < 0x800) { t[0] = (char)(0xC0 | (cp >> 6)); t[1] = (char)(0x80 | (cp & 0x3F)); b_put(b, t, 2); }
    else { t[0] = (char)(0xE0 | (cp >> 12)); t[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); t[2] = (char)(0x80 | (cp & 0x3F)); b_put(b, t, 3); }
}

/* Fixed-point formatting into latin digits; no grouping (matches the engine). */
static void fixed(double x, int decimals, char *out, int size)
{
    char tmp[48];
    int n = 0, k, len = 0;
    double scale = 1, v;
    unsigned long ip, fp;
    if (decimals < 0) decimals = 0;
    if (decimals > 6) decimals = 6;
    for (k = 0; k < decimals; k++) scale *= 10;
    if (x < 0) { x = -x; if (x * scale >= 0.5) out[len++] = '-'; }
    v = x * scale + 0.5;
    if (v > 4294967295.0) v = 4294967295.0;
    {
        unsigned long whole = (unsigned long)v;
        unsigned long sc = (unsigned long)scale;
        ip = whole / sc;
        fp = whole % sc;
    }
    do { tmp[n++] = (char)('0' + ip % 10); ip /= 10; } while (ip && n < 20);
    while (n && len < size - 1) out[len++] = tmp[--n];
    if (decimals && len < size - 1) {
        out[len++] = '.';
        n = 0;
        for (k = 0; k < decimals; k++) { tmp[n++] = (char)('0' + fp % 10); fp /= 10; }
        while (n && len < size - 1) out[len++] = tmp[--n];
    }
    out[len] = 0;
}

char *i18n_num(const I18nLocale *L, double x, int decimals, char *out, int size)
{
    char raw[48];
    unsigned long z = zero_digit(L->d->numbering);
    Buf b;
    const char *s;
    fixed(x, decimals, raw, sizeof(raw));
    b.p = out; b.size = size; b.len = 0;
    if (size > 0) out[0] = 0;
    for (s = raw; *s; s++) {
        if (*s >= '0' && *s <= '9') put_cp(&b, z + (unsigned long)(*s - '0'));
        else if (*s == '.' && z != '0') put_cp(&b, 0x066B);   /* Arabic decimal separator */
        else b_put(&b, s, 1);
    }
    return out;
}

int i18n_category(const I18nLocale *L, const char *n)
{
    return pl_select_str(L->rules, n);
}

static const char *plural_form(const I18nLocaleData *d, const char *key, const char *form, int *has_key)
{
    int lo = 0, hi = d->nplurals - 1, mid, c;
    /* Find the first entry with this key, then scan its forms. */
    while (lo <= hi) {
        mid = (lo + hi) / 2;
        c = s_cmp(key, d->plurals[mid].key);
        if (c <= 0) hi = mid - 1; else lo = mid + 1;
    }
    for (; lo < d->nplurals && !s_cmp(d->plurals[lo].key, key); lo++) {
        *has_key = 1;
        if (!s_cmp(d->plurals[lo].form, form)) return d->plurals[lo].tpl;
    }
    return 0;
}

char *i18n_plural_s(const I18nLocale *L, const char *key, const char *n, int decimals,
                    const I18nParam *p, int np, char *out, int size)
{
    I18nParam all[9];
    char exact[40], nbuf[48], digits[48];
    const char *tpl = 0;
    int has = 0, k, cat;
    double val;
    PlOperands o;

    if (size > 0) out[0] = 0;
    exact[0] = '=';
    for (k = 0; n[k] && k < 38; k++) exact[k + 1] = n[k];
    exact[k + 1] = 0;
    plural_form(L->d, key, "other", &has);
    if (!has) {
        if (L->fb) return i18n_plural_s(L->fb, key, n, decimals, p, np, out, size);
        miss(L, "plurals", key);
        return i18n_format(L, key, 0, 0, out, size);
    }
    /* A fixed number of decimals formats the value before taking operands. */
    pl_operands(n, &o);
    val = o.n;
    if (decimals >= 0) { fixed(val, decimals, digits, sizeof(digits)); pl_operands(digits, &o); }
    cat = pl_select(L->rules, &o);
    tpl = plural_form(L->d, key, exact, &has);
    if (!tpl) tpl = plural_form(L->d, key, pl_category_names[cat], &has);
    if (!tpl) tpl = plural_form(L->d, key, "other", &has);
    if (!tpl) return out;
    if (n[0] == '-') val = -val;
    i18n_num(L, val, decimals < 0 ? 0 : decimals, nbuf, sizeof(nbuf));
    all[0].name = "n"; all[0].value = nbuf;
    for (k = 0; k < np && k < 8; k++) all[k + 1] = p[k];
    return i18n_format(L, tpl, all, k + 1, out, size);
}

char *i18n_plural(const I18nLocale *L, const char *key, long n, char *out, int size)
{
    char s[24];
    int len = 0, k;
    char tmp[24];
    unsigned long u = (unsigned long)(n < 0 ? -n : n);
    if (n < 0) s[len++] = '-';
    k = 0;
    do { tmp[k++] = (char)('0' + u % 10); u /= 10; } while (u);
    while (k) s[len++] = tmp[--k];
    s[len] = 0;
    return i18n_plural_s(L, key, s, -1, 0, 0, out, size);
}

char *i18n_join(const I18nLocale *L, const char *const *items, int n, char *out, int size)
{
    const char *sep = i18n_lookup(L, I18N_STRINGS, "listSep");
    Buf b;
    int k, first = 1;
    if (!sep) sep = " \xC2\xB7 ";
    b.p = out; b.size = size; b.len = 0;
    if (size > 0) out[0] = 0;
    for (k = 0; k < n; k++) {
        if (!items[k] || !items[k][0]) continue;
        if (!first) b_str(&b, sep);
        first = 0;
        if (L->d->rtl) { b_str(&b, FSI); b_str(&b, items[k]); b_str(&b, PDI); }
        else b_str(&b, items[k]);
    }
    return out;
}
