/*
 * Unit tests for the C89 plural and locale port against
 * i18n/test/fixtures/vectors.json (turned into vectors.h at build time).
 * Builds with any C89 compiler; also runs as a Win32 console exe under Wine.
 */
#include <stdio.h>
#include <string.h>
#include "i18n.h"
#include "vectors.h"

static int fails = 0, passes = 0;

static void check(int ok, const char *what, const char *got, const char *want)
{
    if (ok) { passes++; return; }
    fails++;
    printf("FAIL %s\n  got:  %s\n  want: %s\n", what, got ? got : "(null)", want ? want : "(null)");
}

static void check_str(const char *what, const char *got, const char *want)
{
    check(strcmp(got, want) == 0, what, got, want);
}

/* Spot checks for other languages, rules copied from the CLDR table. */
typedef struct { const char *rules[5]; const char *n; const char *cat; } Spot;

static void test_other_languages(void)
{
    static const Spot spots[] = {
        /* Russian */
        { { 0, "v = 0 and i % 10 = 1 and i % 100 != 11", 0,
            "v = 0 and i % 10 = 2..4 and i % 100 != 12..14",
            "v = 0 and i % 10 = 0 or v = 0 and i % 10 = 5..9 or v = 0 and i % 100 = 11..14" }, "21", "one" },
        { { 0, "v = 0 and i % 10 = 1 and i % 100 != 11", 0,
            "v = 0 and i % 10 = 2..4 and i % 100 != 12..14",
            "v = 0 and i % 10 = 0 or v = 0 and i % 10 = 5..9 or v = 0 and i % 100 = 11..14" }, "112", "many" },
        { { 0, "v = 0 and i % 10 = 1 and i % 100 != 11", 0,
            "v = 0 and i % 10 = 2..4 and i % 100 != 12..14",
            "v = 0 and i % 10 = 0 or v = 0 and i % 10 = 5..9 or v = 0 and i % 100 = 11..14" }, "1.5", "other" },
        /* Welsh */
        { { "n = 0", "n = 1", "n = 2", "n = 3", "n = 6" }, "6", "many" },
        { { "n = 0", "n = 1", "n = 2", "n = 3", "n = 6" }, "6.0", "many" },
        /* French: i = 0,1 ; many: e = 0 and i != 0 and i % 1000000 = 0 and v = 0 or e != 0..5 */
        { { 0, "i = 0,1", 0, 0, "e = 0 and i != 0 and i % 1000000 = 0 and v = 0 or e != 0..5" }, "1.7", "one" },
        { { 0, "i = 0,1", 0, 0, "e = 0 and i != 0 and i % 1000000 = 0 and v = 0 or e != 0..5" }, "1000000", "many" },
        { { 0, "i = 0,1", 0, 0, "e = 0 and i != 0 and i % 1000000 = 0 and v = 0 or e != 0..5" }, "1c6", "many" },
        { { 0, "i = 0,1", 0, 0, "e = 0 and i != 0 and i % 1000000 = 0 and v = 0 or e != 0..5" }, "1c3", "other" },
        /* Latvian */
        { { "n % 10 = 0 or n % 100 = 11..19 or v = 2 and f % 100 = 11..19",
            "n % 10 = 1 and n % 100 != 11 or v = 2 and f % 10 = 1 and f % 100 != 11 or v != 2 and f % 10 = 1",
            0, 0, 0 }, "0.1", "one" },
        { { "n % 10 = 0 or n % 100 = 11..19 or v = 2 and f % 100 = 11..19",
            "n % 10 = 1 and n % 100 != 11 or v = 2 and f % 10 = 1 and f % 100 != 11 or v != 2 and f % 10 = 1",
            0, 0, 0 }, "0.11", "zero" },
        /* "within" accepts fractions, "in" does not */
        { { 0, "n within 0..2", 0, 0, 0 }, "1.5", "one" },
        { { 0, "n in 0..2", 0, 0, 0 }, "1.5", "other" },
        { { 0, "n is not 3", 0, 0, 0 }, "4", "one" },
        { { 0, "n mod 10 is 1 @integer 1, 11", 0, 0, 0 }, "21", "one" }
    };
    int k, c;
    char what[96];
    for (k = 0; k < (int)(sizeof(spots) / sizeof(spots[0])); k++) {
        PlRules r;
        int cat;
        for (c = 0; c < PL_OTHER; c++) {
            r.has[c] = 0;
            if (spots[k].rules[c]) {
                int ok = pl_compile(spots[k].rules[c], &r.rule[c]);
                sprintf(what, "compile spot %d cat %d", k, c);
                check(ok, what, spots[k].rules[c], "compiles");
                r.has[c] = ok;
            }
        }
        cat = pl_select_str(&r, spots[k].n);
        sprintf(what, "spot %d n=%s", k, spots[k].n);
        check_str(what, pl_category_names[cat], spots[k].cat);
    }
    {
        PlRule bad;
        check(!pl_compile("n = ", &bad), "reject 'n = '", "compiled", "error");
        check(!pl_compile("q = 1", &bad), "reject unknown operand", "compiled", "error");
        check(!pl_compile("n = 1 and", &bad), "reject dangling and", "compiled", "error");
        check(!pl_compile("n = 1 2", &bad), "reject trailing token", "compiled", "error");
    }
}

static void test_vectors(void)
{
    const CatVec *cv;
    const SampleVec *sv;
    char what[128], out[512];
    for (cv = cat_vectors; cv->loc; cv++) {
        const I18nLocale *L = i18n_get(cv->loc);
        sprintf(what, "%s category %s", cv->loc, cv->n);
        check_str(what, pl_category_names[i18n_category(L, cv->n)], cv->cat);
    }
    for (sv = sample_vectors; sv->loc; sv++) {
        const I18nLocale *L = i18n_get(sv->loc);
        if (sv->n) {
            i18n_plural_s(L, sv->key, sv->n, -1, 0, 0, out, sizeof(out));
            sprintf(what, "%s plural %s %s", sv->loc, sv->key, sv->n);
        } else {
            /* params: "a=1,b=2" */
            char buf[256], *names[8], *vals[8];
            I18nParam p[8];
            int np = 0, k;
            char *s;
            strncpy(buf, sv->params, sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = 0;
            s = buf;
            while (*s && np < 8) {
                names[np] = s;
                while (*s && *s != '=') s++;
                if (*s) *s++ = 0;
                vals[np] = s;
                while (*s && *s != ',') s++;
                if (*s) *s++ = 0;
                np++;
            }
            for (k = 0; k < np; k++) { p[k].name = names[k]; p[k].value = vals[k]; }
            i18n_t(L, sv->key, p, np, out, sizeof(out));
            sprintf(what, "%s string %s", sv->loc, sv->key);
        }
        check_str(what, out, sv->expect);
    }
}

static void test_engine(void)
{
    char out[256];
    I18nParam p[1];
    const I18nLocale *ar = i18n_get("ar"), *en = i18n_get("en");
    check_str("ar-EG resolves to ar", i18n_get("ar-EG")->d->code, "ar");
    check_str("ar_SA resolves to ar", i18n_get("ar_SA")->d->code, "ar");
    check_str("unknown resolves to en", i18n_get("zz-QQ")->d->code, "en");
    check_str("missing key returns key", i18n_t(en, "noSuchKey", 0, 0, out, sizeof(out)), "noSuchKey");
    check_str("exact =0 wins in en", i18n_plural(en, "ended", 0, out, sizeof(out)), "No apps ended yet");
    check_str("en other", i18n_plural(en, "ended", 3, out, sizeof(out)), "3 apps ended this session");
    p[0].name = "app"; p[0].value = "Paint";
    check_str("en no isolation for Latin", i18n_t(en, "endNamed", p, 1, out, sizeof(out)), "End Paint");
    p[0].value = "\xD8\xB9";
    check_str("en isolates RTL value", i18n_t(en, "endNamed", p, 1, out, sizeof(out)),
              "End \xE2\x81\xA8\xD8\xB9\xE2\x81\xA9");
    p[0].name = "nope";
    check_str("unknown placeholder kept", i18n_t(en, "endNamed", p, 1, out, sizeof(out)), "End {app}");
    {
        const char *items[3];
        items[0] = "1 core"; items[1] = ""; items[2] = "Pentium II 400";
        check_str("en join skips empty", i18n_join(en, items, 3, out, sizeof(out)), "1 core \xC2\xB7 Pentium II 400");
        items[0] = "A"; items[1] = "B";
        check(strstr(i18n_join(ar, items, 2, out, sizeof(out)), "\xE2\x81\xA8" "A" "\xE2\x81\xA9") != 0,
              "ar join isolates", out, "FSI A PDI");
    }
    check_str("num 1 decimal", i18n_num(en, 5.25, 1, out, sizeof(out)), "5.3");
    check_str("num 0 decimals", i18n_num(en, 63.5, 0, out, sizeof(out)), "64");
    check_str("num negative", i18n_num(en, -14.44, 1, out, sizeof(out)), "-14.4");
    check_str("plural with decimals", i18n_plural_s(en, "thread", "1", 1, 0, 0, out, sizeof(out)), "1.0 threads");
    check(i18n_count() >= 2, "catalog has en and ar", "", "");
    check_str("list order: en first", i18n_list(0)->code, "en");
}

int main(void)
{
    test_vectors();
    test_engine();
    test_other_languages();
    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
