/* Host unit tests for the i18n port. Reads build/catalog.tsv (made by
   tools/gen_catalog.py) and a TSV made from vectors.json by vectors2tsv.py.
   Also runs inside pulse.exe as --selftest, where the catalog comes from the
   embedded STRINGTABLE instead of the TSV. */
#include "../src/i18n.h"
#include "catalog_keys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SELFTEST_IN_APP
#define MAXID 70000
static u16 *g_tab[MAXID];
static int g_len[MAXID];
static unsigned g_hide, g_hide2;  /* ids to hide, for fallback tests */

static const u16 *tsv_lookup(unsigned id, int *len) {
    if (id >= MAXID || !g_tab[id] || id == g_hide || id == g_hide2) { *len = 0; return 0; }
    *len = g_len[id];
    return g_tab[id];
}
static void unescape(char *s) {
    char *d = s;
    while (*s) {
        if (*s == '\\' && s[1]) { s++; *d++ = *s == 't' ? '\t' : *s == 'n' ? '\n' : *s; s++; }
        else *d++ = *s++;
    }
    *d = 0;
}
static int load_tsv(const char *path) {
    static char line[8192];
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    while (fgets(line, sizeof line, f)) {
        char *tab = strchr(line, '\t');
        unsigned id;
        u16 buf[4096];
        int n;
        if (!tab) continue;
        line[strcspn(line, "\r\n")] = 0;
        *tab = 0; id = (unsigned)atoi(line);
        unescape(tab + 1);
        n = u16from_utf8(buf, tab + 1, 4096);
        g_tab[id] = (u16 *)malloc((n + 1) * sizeof(u16));
        memcpy(g_tab[id], buf, (n + 1) * sizeof(u16));
        g_len[id] = n;
    }
    fclose(f);
    return 0;
}
#endif

int g_fail, g_pass;
static const char *CATN[] = { "zero", "one", "two", "few", "many", "other" };

static void utf8_of(const u16 *s, char *o, int cap) {
    int n = 0;
    while (*s && n < cap - 4) {
        unsigned c = *s++;
        if (c >= 0xD800 && c < 0xDC00 && *s) { c = 0x10000 + ((c - 0xD800) << 10) + (*s++ - 0xDC00); }
        if (c < 0x80) o[n++] = (char)c;
        else if (c < 0x800) { o[n++] = (char)(0xC0 | c >> 6); o[n++] = (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) { o[n++] = (char)(0xE0 | c >> 12); o[n++] = (char)(0x80 | ((c >> 6) & 63)); o[n++] = (char)(0x80 | (c & 63)); }
        else { o[n++] = (char)(0xF0 | c >> 18); o[n++] = (char)(0x80 | ((c >> 12) & 63)); o[n++] = (char)(0x80 | ((c >> 6) & 63)); o[n++] = (char)(0x80 | (c & 63)); }
    }
    o[n] = 0;
}
static void check(int ok, const char *what, const char *got, const char *want) {
    if (ok) { g_pass++; return; }
    g_fail++;
    printf("FAIL %s\n  got:  %s\n  want: %s\n", what, got, want);
}
static int key_index(const char **names, int n, const char *k) {
    int i;
    for (i = 0; i < n; i++) if (!strcmp(names[i], k)) return i;
    return -1;
}

int run_vectors(const char *vec_path) {
    static const char *SN[] = CAT_S_NAMES, *PN[] = CAT_P_NAMES;
    char line[4096];
    FILE *f = fopen(vec_path, "rb");
    if (!f) { printf("cannot open %s\n", vec_path); return 2; }
    while (fgets(line, sizeof line, f)) {
        char *a[4], *p = line;
        int k;
        line[strcspn(line, "\r\n")] = 0;
        for (k = 0; k < 4; k++) { a[k] = p; p = strchr(p, '\t'); if (!p) break; *p++ = 0; }
        if (k < 3) continue;
        {
            Locale *L = i18n_locale(i18n_resolve(a[1]));
            char what[256];
            if (!strcmp(a[0], "cat")) {
                int c = i18n_select(L, a[2]);
                snprintf(what, sizeof what, "plural %s %s", a[1], a[2]);
                check(!strcmp(CATN[c], a[3]), what, CATN[c], a[3]);
            } else {
                char key[64], *colon = strchr(a[2], ':'), got[2048];
                u16 out[1024], want16[1024];
                memcpy(key, a[2], colon - a[2]); key[colon - a[2]] = 0;
                if (!strchr(colon + 1, '=')) {
                    int pk = key_index(PN, P__COUNT, key);
                    i18n_plural(L, pk, atof(colon + 1), -1, 0, 0, out, 1024);
                } else {
                    I18nParam pr[8];
                    static u16 vals[8][128];
                    static char names[8][32];
                    char spec[512], *tok;
                    int np = 0;
                    strcpy(spec, colon + 1);
                    tok = spec;
                    while (tok && *tok && np < 8) {
                        char *comma = strchr(tok, ','), *eq;
                        if (comma) *comma = 0;
                        eq = strchr(tok, '=');
                        if (eq) {
                            *eq = 0;
                            strcpy(names[np], tok);
                            u16from_utf8(vals[np], eq + 1, 128);
                            pr[np].name = names[np]; pr[np].value = vals[np]; np++;
                        }
                        tok = comma ? comma + 1 : 0;
                    }
                    i18n_t(L, key_index(SN, S__COUNT, key), pr, np, out, 1024);
                }
                u16from_utf8(want16, a[3], 1024);
                utf8_of(out, got, sizeof got);
                snprintf(what, sizeof what, "sample %s %s", a[1], a[2]);
                check(!strcmp(got, a[3]), what, got, a[3]);
            }
        }
    }
    fclose(f);
    return 0;
}

int run_extra(void) {
    char got[512];
    u16 out[512];
    Locale *ar = i18n_locale(i18n_resolve("ar")), *en = i18n_locale(i18n_resolve("en"));
    check(!strcmp(i18n_locale(i18n_resolve("ar-EG"))->code, "ar"), "resolve ar-EG", i18n_locale(i18n_resolve("ar-EG"))->code, "ar");
    check(!strcmp(i18n_locale(i18n_resolve("xx-YY"))->code, "en"), "resolve unknown", i18n_locale(i18n_resolve("xx-YY"))->code, "en");
    check(ar->rtl && !en->rtl, "dir", ar->rtl ? "rtl" : "ltr", "rtl");
    /* exact "=0" wins in en */
    i18n_plural(en, P_ended, 0, -1, 0, 0, out, 512); utf8_of(out, got, sizeof got);
    check(!strcmp(got, "No apps ended yet"), "en ended =0", got, "No apps ended yet");
    i18n_plural(en, P_process, 14, -1, 0, 0, out, 512); utf8_of(out, got, sizeof got);
    check(!strcmp(got, "14 processes"), "en process 14", got, "14 processes");
    /* RTL isolation of an inserted number */
    {
        I18nParam p[1]; u16 v[16];
        u16from_utf8(v, "54%", 16); p[0].name = "pct"; p[0].value = v;
        i18n_t(ar, S_used, p, 1, out, 512); utf8_of(out, got, sizeof got);
        check(strstr(got, "\xE2\x81\xA8" "54%" "\xE2\x81\xA9") != 0, "ar isolates {pct}", got, "...\\u206854%\\u2069...");
    }
    /* join isolates items in RTL */
    {
        u16 a1[8], a2[8]; const u16 *it[2];
        u16from_utf8(a1, "A", 8); u16from_utf8(a2, "B", 8); it[0] = a1; it[1] = a2;
        i18n_join(ar, it, 2, out, 512); utf8_of(out, got, sizeof got);
        check(!strcmp(got, "\xE2\x81\xA8" "A" "\xE2\x81\xA9 \xC2\xB7 \xE2\x81\xA8" "B" "\xE2\x81\xA9"), "ar join", got, "FSI A PDI · FSI B PDI");
    }
    /* apps section */
    {
        u16 nm[32]; u16from_utf8(nm, "explorer.exe", 32);
        i18n_app(ar, nm, out, 512); utf8_of(out, got, sizeof got);
        check(strcmp(got, "explorer.exe") != 0, "ar app name", got, "(Arabic)");
        i18n_app(en, nm, out, 512); utf8_of(out, got, sizeof got);
        check(!strcmp(got, "explorer.exe"), "en app name", got, "explorer.exe");
    }
    /* rule compiler on harder CLDR rules */
    {
        PluralRule r; PluralOps o;
        const char *ru = "v = 0 and i % 10 = 2..4 and i % 100 != 12..14 or f % 10 = 2..4 and f % 100 != 12..14";
        check(plural_compile(ru, &r) == 0, "compile bs few", "error", "ok");
        plural_operands("22", &o); check(plural_match(&r, &o), "bs few 22", "no", "yes");
        plural_operands("12", &o); check(!plural_match(&r, &o), "bs few 12", "yes", "no");
        plural_operands("1.2", &o); check(plural_match(&r, &o), "bs few 1.2", "no", "yes");
        check(plural_compile("n = 0 or n != 1 and n % 100 = 1..19 @integer 0, 2~16", &r) == 0, "compile ro few", "error", "ok");
        plural_operands("1.5c3", &o); check(o.i == 1500 && o.e == 3, "operands 1.5c3", "?", "i=1500 e=3");
        check(plural_compile("n is not 1", &r) == 0, "compile is not", "error", "ok");
        check(plural_compile("n = ", &r) != 0, "reject bad rule", "accepted", "error");
        check(plural_compile("n within 0..2", &r) == 0, "compile within", "error", "ok");
        plural_operands("1.5", &o); check(plural_match(&r, &o), "within 1.5", "no", "yes");
    }
    return 0;
}

#ifndef SELFTEST_IN_APP
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: test_i18n catalog.tsv vectors.tsv\n"); return 2; }
    if (load_tsv(argv[1])) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
    printf("locales: %d\n", i18n_init(tsv_lookup));
    run_vectors(argv[2]);
    run_extra();
    /* fallback: hide ar strings.calm, expect the English text */
    {
        Locale *ar = i18n_locale(i18n_resolve("ar"));
        u16 out[128]; char got[256];
        g_hide = CAT_BASE + ar->slot * CAT_SLOT + CAT_STRINGS + S_calm;
        i18n_t(ar, S_calm, 0, 0, out, 128); utf8_of(out, got, sizeof got);
        check(!strcmp(got, "System Calm"), "fallback to en", got, "System Calm");
        g_hide2 = CAT_BASE + 0 * CAT_SLOT + CAT_STRINGS + S_calm;  /* hide en too: ar -> en -> key */
        i18n_t(ar, S_calm, 0, 0, out, 128); utf8_of(out, got, sizeof got);
        check(!strcmp(got, "calm"), "fallback to key", got, "calm");
        g_hide = g_hide2 = 0;
    }
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
#endif
