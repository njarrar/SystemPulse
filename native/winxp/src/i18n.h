/*
 * Pulse i18n core for the Windows XP target.
 *
 * A C port of i18n/pulse-i18n.js: CLDR plural rule compiler and operands,
 * {placeholder} fill with FSI/PDI isolation in RTL locales, list joins,
 * number formatting with numbering systems, and the locale fallback chain.
 *
 * Text is UTF-16 (u16). The catalog comes from a lookup callback so the
 * same code runs against the embedded STRINGTABLE (LoadStringW) and against
 * catalog.tsv in the host unit tests.
 */
#ifndef PULSE_I18N_H
#define PULSE_I18N_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned short u16;

/* Returns the catalog string for id and its length, or 0 when missing.
   The string does not need to be NUL terminated. */
typedef const u16 *(*i18n_lookup_fn)(unsigned id, int *len);

#define I18N_MAX_LOCALES 31
#define I18N_FSI 0x2068
#define I18N_PDI 0x2069

enum { PC_ZERO, PC_ONE, PC_TWO, PC_FEW, PC_MANY, PC_OTHER };

/* ---- plural rules ---------------------------------------------------- */
typedef struct {
    double n, i, v, w, f, t, c, e;
} PluralOps;

#define PR_MAX_RANGES 12
#define PR_MAX_AND 6
#define PR_MAX_OR 12
typedef struct {
    char op;              /* n i v w f t c e */
    double mod;           /* 0 = none */
    int neg, integer_only, nranges;
    double lo[PR_MAX_RANGES], hi[PR_MAX_RANGES];
} PluralRel;
typedef struct {
    int nor;
    int nand[PR_MAX_OR];
    PluralRel rel[PR_MAX_OR][PR_MAX_AND];
} PluralRule;

/* Parses a decimal string ("1", "1.0", "101.3", "1.2e3", "1.2c3"). */
void plural_operands(const char *s, PluralOps *o);
/* Compiles one CLDR rule. Returns 0 on success, -1 on a syntax error. */
int plural_compile(const char *src, PluralRule *r);
int plural_match(const PluralRule *r, const PluralOps *o);

/* ---- locales --------------------------------------------------------- */
typedef struct {
    int slot;                 /* catalog slot */
    char code[24];
    u16 label[16], name[48];
    int rtl;
    char numsys[16];
    u16 font_ui[96], font_hero[96];
    int fallback;             /* locale index or -1 */
    int has_rule[5];
    PluralRule rules[5];
} Locale;

typedef struct {
    const char *name;
    const u16 *value;
} I18nParam;

int i18n_init(i18n_lookup_fn fn);   /* returns the number of locales */
int i18n_count(void);
Locale *i18n_locale(int idx);
int i18n_resolve(const char *code);  /* "ar-EG" -> index of "ar"; unknown -> en */

int i18n_select(const Locale *L, const char *numstr);  /* PC_* */

/* All writers return the length written (output is always NUL terminated). */
int i18n_num(const Locale *L, double x, int decimals, u16 *out, int cap);
int i18n_format(const Locale *L, const u16 *tpl, int tlen, const I18nParam *p, int np, u16 *out, int cap);
int i18n_t(const Locale *L, int key, const I18nParam *p, int np, u16 *out, int cap);
int i18n_hw(const Locale *L, int key, const I18nParam *p, int np, u16 *out, int cap);
/* decimals < 0 means "as written" (integers print without a fraction). */
int i18n_plural(const Locale *L, int key, double n, int decimals, const I18nParam *p, int np, u16 *out, int cap);
int i18n_join(const Locale *L, const u16 *const *items, int n, u16 *out, int cap);
int i18n_app(const Locale *L, const u16 *name, u16 *out, int cap);

/* Small UTF-16 helpers. */
int u16len(const u16 *s);
int u16cpy(u16 *d, const u16 *s, int cap);
int u16cat(u16 *d, const u16 *s, int cap);
int u16from_ascii(u16 *d, const char *s, int cap);
int u16from_utf8(u16 *d, const char *s, int cap);
int u16_has_rtl(const u16 *s, int len);


#ifdef __cplusplus
}
#endif
#endif
