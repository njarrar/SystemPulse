/* Pulse i18n core. See i18n.h. Plain C89-ish so it builds on the host too. */
#include "i18n.h"
#include "catalog_keys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static i18n_lookup_fn g_lookup;
static Locale g_loc[I18N_MAX_LOCALES];
static int g_count;
static const char *g_snames[] = CAT_S_NAMES;
static const char *g_hnames[] = CAT_H_NAMES;
static const char *g_pnames[] = CAT_P_NAMES;

/* ------------------------------------------------------------------ */
/* UTF-16 helpers                                                     */
/* ------------------------------------------------------------------ */
int u16len(const u16 *s) { int n = 0; if (!s) return 0; while (s[n]) n++; return n; }

static int put(u16 *d, int pos, int cap, u16 ch) {
    if (pos < cap - 1) d[pos++] = ch;
    return pos;
}
static int putn(u16 *d, int pos, int cap, const u16 *s, int n) {
    int k;
    for (k = 0; k < n; k++) pos = put(d, pos, cap, s[k]);
    return pos;
}
int u16cpy(u16 *d, const u16 *s, int cap) {
    int n = putn(d, 0, cap, s, u16len(s));
    if (cap > 0) d[n] = 0;
    return n;
}
int u16cat(u16 *d, const u16 *s, int cap) {
    int n = putn(d, u16len(d), cap, s, u16len(s));
    if (cap > 0) d[n] = 0;
    return n;
}
int u16from_ascii(u16 *d, const char *s, int cap) {
    int n = 0;
    while (*s && n < cap - 1) d[n++] = (unsigned char)*s++;
    if (cap > 0) d[n] = 0;
    return n;
}
int u16from_utf8(u16 *d, const char *s, int cap) {
    const unsigned char *p = (const unsigned char *)s;
    int n = 0;
    while (*p) {
        unsigned long c;
        if (*p < 0x80) c = *p++;
        else if ((*p & 0xE0) == 0xC0) { c = (*p++ & 0x1F) << 6; c |= (*p++ & 0x3F); }
        else if ((*p & 0xF0) == 0xE0) { c = (*p++ & 0x0F) << 12; c |= (*p++ & 0x3F) << 6; c |= (*p++ & 0x3F); }
        else { c = (*p++ & 0x07) << 18; c |= (*p++ & 0x3F) << 12; c |= (*p++ & 0x3F) << 6; c |= (*p++ & 0x3F); }
        if (c >= 0x10000) {
            c -= 0x10000;
            n = put(d, n, cap, (u16)(0xD800 + (c >> 10)));
            n = put(d, n, cap, (u16)(0xDC00 + (c & 0x3FF)));
        } else n = put(d, n, cap, (u16)c);
    }
    if (cap > 0) d[n] = 0;
    return n;
}
static int is_rtl_char(u16 c) {
    return (c >= 0x0590 && c <= 0x08FF) || (c >= 0xFB1D && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF);
}
int u16_has_rtl(const u16 *s, int len) {
    int k;
    for (k = 0; k < len; k++) if (is_rtl_char(s[k])) return 1;
    return 0;
}
static void to_ascii(const u16 *s, int len, char *d, int cap) {
    int k, n = 0;
    for (k = 0; k < len && n < cap - 1; k++) d[n++] = s[k] < 128 ? (char)s[k] : '?';
    d[n] = 0;
}

/* ------------------------------------------------------------------ */
/* CLDR plural operands                                               */
/* ------------------------------------------------------------------ */
void plural_operands(const char *src, PluralOps *o) {
    char buf[96], digits[96], ip[64], fp[64];
    const char *s = src;
    int e = 0, len, dot, k, pos, w;
    char *ex;
    while (*s == ' ') s++;
    if (*s == '-' || *s == '+') s++;
    strncpy(buf, s, sizeof buf - 1); buf[sizeof buf - 1] = 0;
    len = (int)strlen(buf);
    while (len && buf[len - 1] == ' ') buf[--len] = 0;
    ex = strpbrk(buf, "ceCE");
    if (ex) {
        /* shift the decimal point right by e places, keeping trailing zeros */
        int nd = 0, intlen;
        e = atoi(ex + 1); *ex = 0;
        dot = -1;
        for (k = 0; buf[k]; k++) { if (buf[k] == '.') dot = k; else digits[nd++] = buf[k]; }
        digits[nd] = 0;
        intlen = (dot < 0 ? (int)strlen(buf) : dot) + e;
        while (nd < intlen) digits[nd++] = '0';
        digits[nd] = 0;
        if (intlen >= nd) strcpy(buf, digits);
        else { memcpy(buf, digits, intlen); buf[intlen] = '.'; strcpy(buf + intlen + 1, digits + intlen); }
    }
    dot = -1;
    for (k = 0; buf[k]; k++) if (buf[k] == '.') { dot = k; break; }
    if (dot < 0) { strcpy(ip, buf); fp[0] = 0; }
    else { memcpy(ip, buf, dot); ip[dot] = 0; strcpy(fp, buf + dot + 1); }
    pos = (int)strlen(fp);
    w = pos;
    while (w > 0 && fp[w - 1] == '0') w--;
    o->n = fabs(atof(buf));
    o->i = atof(ip[0] ? ip : "0");
    o->v = pos;
    o->w = w;
    o->f = pos ? atof(fp) : 0;
    { char t[64]; memcpy(t, fp, w); t[w] = 0; o->t = w ? atof(t) : 0; }
    o->c = o->e = e;
}

/* ------------------------------------------------------------------ */
/* CLDR plural rule compiler                                          */
/*   condition     = and_condition ('or' and_condition)*              */
/*   and_condition = relation ('and' relation)*                       */
/*   relation      = expr ('=' | '!=') range_list                     */
/*                 | expr 'is' 'not'? value                           */
/*                 | expr 'not'? ('in' | 'within') range_list         */
/*   expr          = operand ('%' | 'mod' value)?                     */
/* Sample lists ("@integer ...", "@decimal ...") are ignored.         */
/* ------------------------------------------------------------------ */
typedef struct { char tok[48][16]; int n, p; } Tokens;

static int tokenize(const char *src, Tokens *t) {
    const char *s = src;
    t->n = t->p = 0;
    while (*s && *s != '@') {
        int len = 0;
        if (*s == ' ' || *s == '\t') { s++; continue; }
        if (t->n >= 48) return -1;
        if (s[0] == '.' && s[1] == '.') len = 2;
        else if (s[0] == '!' && s[1] == '=') len = 2;
        else if (*s == '=' || *s == '%' || *s == ',') len = 1;
        else if (*s >= 'a' && *s <= 'z') { while (s[len] >= 'a' && s[len] <= 'z') len++; }
        else if (*s >= '0' && *s <= '9') {
            while (s[len] >= '0' && s[len] <= '9') len++;
            if (s[len] == '.' && s[len + 1] >= '0' && s[len + 1] <= '9') { len++; while (s[len] >= '0' && s[len] <= '9') len++; }
        } else return -1;
        if (len >= 16) return -1;
        memcpy(t->tok[t->n], s, len); t->tok[t->n][len] = 0; t->n++;
        s += len;
    }
    return 0;
}
static const char *peek(Tokens *t) { return t->p < t->n ? t->tok[t->p] : ""; }
static const char *next(Tokens *t) { return t->p < t->n ? t->tok[t->p++] : ""; }
static int num(Tokens *t, double *v) {
    const char *x = next(t);
    if (!(x[0] >= '0' && x[0] <= '9')) return -1;
    *v = atof(x);
    return 0;
}
static int range_list(Tokens *t, PluralRel *r) {
    r->nranges = 0;
    do {
        double lo, hi;
        if (num(t, &lo)) return -1;
        hi = lo;
        if (!strcmp(peek(t), "..")) { next(t); if (num(t, &hi)) return -1; }
        if (r->nranges >= PR_MAX_RANGES) return -1;
        r->lo[r->nranges] = lo; r->hi[r->nranges] = hi; r->nranges++;
    } while (!strcmp(peek(t), ",") && next(t));
    return 0;
}
static int relation(Tokens *t, PluralRel *r) {
    const char *op = next(t);
    memset(r, 0, sizeof *r);
    if (strlen(op) != 1 || !strchr("nivwftce", op[0])) return -1;
    r->op = op[0];
    r->integer_only = 1;
    if (!strcmp(peek(t), "%") || !strcmp(peek(t), "mod")) { next(t); if (num(t, &r->mod)) return -1; }
    op = next(t);
    if (!strcmp(op, "is")) {
        if (!strcmp(peek(t), "not")) { next(t); r->neg = 1; }
        if (num(t, &r->lo[0])) return -1;
        r->hi[0] = r->lo[0]; r->nranges = 1;
    } else if (!strcmp(op, "=") || !strcmp(op, "!=")) {
        r->neg = op[0] == '!';
        return range_list(t, r);
    } else {
        if (!strcmp(op, "not")) { r->neg = 1; op = next(t); }
        if (!strcmp(op, "within")) r->integer_only = 0;
        else if (strcmp(op, "in")) return -1;
        return range_list(t, r);
    }
    return 0;
}
int plural_compile(const char *src, PluralRule *r) {
    Tokens t;
    memset(r, 0, sizeof *r);
    if (tokenize(src, &t)) return -1;
    if (!t.n) return 0;  /* empty rule: always true (nor = 0 handled in match) */
    for (;;) {
        int a = 0;
        if (r->nor >= PR_MAX_OR) return -1;
        for (;;) {
            if (a >= PR_MAX_AND) return -1;
            if (relation(&t, &r->rel[r->nor][a])) return -1;
            a++;
            if (!strcmp(peek(&t), "and")) { next(&t); continue; }
            break;
        }
        r->nand[r->nor++] = a;
        if (!strcmp(peek(&t), "or")) { next(&t); continue; }
        break;
    }
    return t.p == t.n ? 0 : -1;
}
static double opval(const PluralOps *o, char op) {
    switch (op) {
    case 'n': return o->n; case 'i': return o->i; case 'v': return o->v; case 'w': return o->w;
    case 'f': return o->f; case 't': return o->t; case 'c': return o->c; default: return o->e;
    }
}
static int rel_match(const PluralRel *r, const PluralOps *o) {
    double x = opval(o, r->op);
    int k, hit = 0;
    if (r->mod) x = fmod(x, r->mod);
    for (k = 0; k < r->nranges && !hit; k++)
        hit = x >= r->lo[k] && x <= r->hi[k] && (!r->integer_only || x == floor(x));
    return r->neg ? !hit : hit;
}
int plural_match(const PluralRule *r, const PluralOps *o) {
    int a, b;
    if (!r->nor) return 1;
    for (a = 0; a < r->nor; a++) {
        int ok = 1;
        for (b = 0; b < r->nand[a] && ok; b++) ok = rel_match(&r->rel[a][b], o);
        if (ok) return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Catalog access                                                     */
/* ------------------------------------------------------------------ */
static unsigned slot_base(int slot) { return CAT_BASE + (unsigned)slot * CAT_SLOT; }

static const u16 *raw(const Locale *L, unsigned off, int *len) {
    *len = 0;
    return g_lookup(slot_base(L->slot) + off, len);
}
static int raw_copy(int slot, unsigned off, u16 *d, int cap) {
    int len = 0;
    const u16 *s = g_lookup(slot_base(slot) + off, &len);
    int n = 0;
    if (s) n = putn(d, 0, cap, s, len);
    if (cap > 0) d[n] = 0;
    return s ? n : -1;
}

int i18n_init(i18n_lookup_fn fn) {
    int slot, k;
    g_lookup = fn;
    g_count = 0;
    for (slot = 0; slot < I18N_MAX_LOCALES; slot++) {
        Locale *L = &g_loc[g_count];
        u16 tmp[96];
        if (raw_copy(slot, 0, tmp, 96) < 0) break;
        memset(L, 0, sizeof *L);
        L->slot = slot;
        to_ascii(tmp, u16len(tmp), L->code, sizeof L->code);
        if (raw_copy(slot, 1, L->label, 16) < 0) u16from_ascii(L->label, L->code, 16);
        if (raw_copy(slot, 2, L->name, 48) < 0) u16from_ascii(L->name, L->code, 48);
        raw_copy(slot, 3, tmp, 96);
        L->rtl = tmp[0] == 'r';
        raw_copy(slot, 4, L->font_ui, 96);
        raw_copy(slot, 5, L->font_hero, 96);
        if (raw_copy(slot, 6, tmp, 96) > 0) to_ascii(tmp, u16len(tmp), L->numsys, sizeof L->numsys);
        else strcpy(L->numsys, "latn");
        for (k = 0; k < 5; k++) {
            int len;
            const u16 *s = raw(L, 8 + k, &len);
            char rule[256];
            if (!s) continue;
            to_ascii(s, len, rule, sizeof rule);
            L->has_rule[k] = plural_compile(rule, &L->rules[k]) == 0;
        }
        L->fallback = -1;
        g_count++;
    }
    /* Fallback chains: the file's "fallback" code, else en. */
    for (k = 0; k < g_count; k++) {
        u16 tmp[32];
        char fb[32];
        int j;
        if (raw_copy(g_loc[k].slot, 7, tmp, 32) <= 0) continue;
        to_ascii(tmp, u16len(tmp), fb, sizeof fb);
        for (j = 0; j < g_count; j++) if (j != k && !strcmp(g_loc[j].code, fb)) g_loc[k].fallback = j;
    }
    return g_count;
}
int i18n_count(void) { return g_count; }
Locale *i18n_locale(int idx) { return idx >= 0 && idx < g_count ? &g_loc[idx] : 0; }

int i18n_resolve(const char *code) {
    char c[32];
    int k;
    strncpy(c, code ? code : "", sizeof c - 1); c[sizeof c - 1] = 0;
    for (k = 0; c[k]; k++) if (c[k] == '_') c[k] = '-';
    for (;;) {
        char *dash;
        for (k = 0; k < g_count; k++) if (!strcmp(g_loc[k].code, c)) return k;
        dash = strrchr(c, '-');
        if (!dash) break;
        *dash = 0;
    }
    for (k = 0; k < g_count; k++) if (!strcmp(g_loc[k].code, "en")) return k;
    return 0;
}

int i18n_select(const Locale *L, const char *numstr) {
    PluralOps o;
    int k, any = 0;
    for (k = 0; k < 5; k++) any |= L->has_rule[k];
    plural_operands(numstr, &o);
    if (!any) return o.n == 1 && o.v == 0 ? PC_ONE : PC_OTHER;
    for (k = 0; k < 5; k++) if (L->has_rule[k] && plural_match(&L->rules[k], &o)) return k;
    return PC_OTHER;
}

/* ------------------------------------------------------------------ */
/* Formatting                                                         */
/* ------------------------------------------------------------------ */
static u16 digit_zero(const char *ns) {
    if (!strcmp(ns, "arab")) return 0x0660;
    if (!strcmp(ns, "arabext")) return 0x06F0;
    if (!strcmp(ns, "deva")) return 0x0966;
    if (!strcmp(ns, "beng")) return 0x09E6;
    if (!strcmp(ns, "thai")) return 0x0E50;
    if (!strcmp(ns, "fullwide")) return 0xFF10;
    return '0';
}
int i18n_num(const Locale *L, double x, int decimals, u16 *out, int cap) {
    char b[64];
    int k, n = 0;
    u16 z = L ? digit_zero(L->numsys) : '0';
    if (decimals < 0) decimals = 0;
    /* round half away from zero, like Intl.NumberFormat */
    {
        double m = pow(10.0, decimals), r = floor(fabs(x) * m + 0.5 + 1e-9) / m;
        x = x < 0 ? -r : r;
    }
    snprintf(b, sizeof b, "%.*f", decimals, x);
    if (!strcmp(b, "-0") || (b[0] == '-' && strspn(b + 1, "0.") == strlen(b + 1))) memmove(b, b + 1, strlen(b));
    for (k = 0; b[k]; k++) {
        u16 c = (u16)(unsigned char)b[k];
        if (c >= '0' && c <= '9') c = (u16)(z + (c - '0'));
        else if (c == '.' && z == 0x0660) c = 0x066B;
        else if (c == '.' && z == 0x06F0) c = 0x066B;
        n = put(out, n, cap, c);
    }
    if (cap > 0) out[n] = 0;
    return n;
}

int i18n_format(const Locale *L, const u16 *tpl, int tlen, const I18nParam *p, int np, u16 *out, int cap) {
    int k = 0, n = 0;
    while (k < tlen) {
        if (tpl[k] == '{') {
            int e = k + 1;
            while (e < tlen && ((tpl[e] >= 'a' && tpl[e] <= 'z') || (tpl[e] >= 'A' && tpl[e] <= 'Z') || (tpl[e] >= '0' && tpl[e] <= '9') || tpl[e] == '_')) e++;
            if (e < tlen && tpl[e] == '}' && e > k + 1) {
                char nm[32];
                int j, found = -1;
                to_ascii(tpl + k + 1, e - k - 1, nm, sizeof nm);
                for (j = 0; j < np; j++) if (!strcmp(p[j].name, nm)) { found = j; break; }
                if (found >= 0) {
                    const u16 *v = p[found].value;
                    int vl = u16len(v), iso = (L && L->rtl) || u16_has_rtl(v, vl);
                    if (iso) n = put(out, n, cap, I18N_FSI);
                    n = putn(out, n, cap, v, vl);
                    if (iso) n = put(out, n, cap, I18N_PDI);
                    k = e + 1;
                    continue;
                }
            }
        }
        n = put(out, n, cap, tpl[k++]);
    }
    if (cap > 0) out[n] = 0;
    return n;
}

static int lookup_chain(const Locale *L, unsigned off, const u16 **s, int *len) {
    while (L) {
        *s = raw(L, off, len);
        if (*s) return 1;
        L = L->fallback >= 0 ? &g_loc[L->fallback] : 0;
    }
    return 0;
}
static int section(const Locale *L, unsigned base, int key, const char *name, const I18nParam *p, int np, u16 *out, int cap) {
    const u16 *s;
    int len;
    if (lookup_chain(L, base + key, &s, &len)) return i18n_format(L, s, len, p, np, out, cap);
    return u16from_ascii(out, name, cap);  /* the key itself, like the JS engine */
}
int i18n_t(const Locale *L, int key, const I18nParam *p, int np, u16 *out, int cap) {
    return section(L, CAT_STRINGS, key, key >= 0 && key < S__COUNT ? g_snames[key] : "?", p, np, out, cap);
}
int i18n_hw(const Locale *L, int key, const I18nParam *p, int np, u16 *out, int cap) {
    return section(L, CAT_HW, key, key >= 0 && key < H__COUNT ? g_hnames[key] : "?", p, np, out, cap);
}

static void numstr_of(double n, int decimals, char *b, int cap) {
    if (decimals >= 0) snprintf(b, cap, "%.*f", decimals, fabs(n));
    else snprintf(b, cap, "%.15g", fabs(n));
}

int i18n_plural(const Locale *L, int key, double n, int decimals, const I18nParam *p, int np, u16 *out, int cap) {
    unsigned off = CAT_PLURALS + (unsigned)key * 8;
    const u16 *tpl = 0;
    int tlen = 0, c, len;
    char ns[64];
    u16 nbuf[64];
    I18nParam pp[9];
    int npp = 0, has = 0;
    for (c = 0; c < 7; c++) if (raw(L, off + c, &len)) has = 1;
    if (!has) {
        if (L->fallback >= 0) return i18n_plural(&g_loc[L->fallback], key, n, decimals, p, np, out, cap);
        return u16from_ascii(out, key >= 0 && key < P__COUNT ? g_pnames[key] : "?", cap);
    }
    numstr_of(n, decimals, ns, sizeof ns);
    /* exact forms ("=0") win over CLDR categories */
    {
        const u16 *ex = raw(L, off + 6, &len);
        int k = 0;
        while (ex && k < len) {
            int ls = k, tab, le;
            char want[32];
            while (k < len && ex[k] != '\n') k++;
            le = k++;
            for (tab = ls; tab < le && ex[tab] != '\t'; tab++) {}
            to_ascii(ex + ls, tab - ls, want, sizeof want);
            if (tab < le && !strcmp(want, ns)) { tpl = ex + tab + 1; tlen = le - tab - 1; break; }
        }
    }
    if (!tpl) {
        int cat = i18n_select(L, ns);
        tpl = raw(L, off + cat, &tlen);
        if (!tpl) tpl = raw(L, off + PC_OTHER, &tlen);
    }
    i18n_num(L, n, decimals, nbuf, 64);
    pp[npp].name = "n"; pp[npp].value = nbuf; npp++;
    for (c = 0; c < np && npp < 9; c++) pp[npp++] = p[c];
    if (!tpl) { if (cap > 0) out[0] = 0; return 0; }
    return i18n_format(L, tpl, tlen, pp, npp, out, cap);
}

int i18n_join(const Locale *L, const u16 *const *items, int n, u16 *out, int cap) {
    const u16 *sep;
    int slen, k, pos = 0, first = 1;
    static const u16 dflt[] = { ' ', 0x00B7, ' ', 0 };
    if (!lookup_chain(L, CAT_STRINGS + S_listSep, &sep, &slen)) { sep = dflt; slen = 3; }
    for (k = 0; k < n; k++) {
        if (!items[k] || !items[k][0]) continue;
        if (!first) pos = putn(out, pos, cap, sep, slen);
        first = 0;
        if (L->rtl) pos = put(out, pos, cap, I18N_FSI);
        pos = putn(out, pos, cap, items[k], u16len(items[k]));
        if (L->rtl) pos = put(out, pos, cap, I18N_PDI);
    }
    if (cap > 0) out[pos] = 0;
    return pos;
}

int i18n_app(const Locale *L, const u16 *name, u16 *out, int cap) {
    int k, nl = u16len(name);
    for (k = 0; k < CAT_MAX_APPS; k++) {
        int len, tab;
        const u16 *s = raw(L, CAT_APPS + k, &len);
        if (!s) break;
        for (tab = 0; tab < len && s[tab] != '\t'; tab++) {}
        if (tab == nl && !memcmp(s, name, nl * sizeof(u16))) {
            int n = putn(out, 0, cap, s + tab + 1, len - tab - 1);
            if (cap > 0) out[n] = 0;
            return n;
        }
    }
    return u16cpy(out, name, cap);
}
