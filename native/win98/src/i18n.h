/*
 * Pulse locale engine, C89 port of i18n/pulse-i18n.js.
 * Strings are UTF-8. The catalog comes from tools/gen_catalog.py.
 */
#ifndef PULSE_I18N_H
#define PULSE_I18N_H

#include "plural.h"

typedef struct { const char *k; const char *v; } I18nKV;
typedef struct { const char *key; const char *form; const char *tpl; } I18nPluralForm;

typedef struct {
    const char *code;
    const char *label;
    const char *name;
    const char *fallback;       /* NULL: base locale "en" */
    int rtl;
    const char *numbering;      /* latn, arab, arabext, ... */
    const char *rules[PL_OTHER];/* CLDR source per category, NULL if absent */
    const I18nKV *strings;   int nstrings;   /* sorted by key */
    const I18nKV *hardware;  int nhardware;  /* sorted by key */
    const I18nPluralForm *plurals; int nplurals; /* sorted by key */
    const I18nKV *apps;      int napps;      /* sorted by key */
    const char *font_ui;        /* first face of fonts.platforms.w98.ui, or NULL */
    int codepage;               /* Windows ANSI code page that holds every string */
    int charset;                /* matching GDI charset */
    int order;
} I18nLocaleData;

extern const I18nLocaleData *const i18n_catalog[];
extern const int i18n_catalog_count;

typedef struct I18nLocale {
    const I18nLocaleData *d;
    const struct I18nLocale *fb;
    PlRules *rules;            /* from a small pool, compiled on first use */
    int ready;
} I18nLocale;

typedef struct { const char *name; const char *value; } I18nParam;

/* "ar-EG" resolves to "ar"; unknown codes resolve to the base locale. */
const I18nLocale *i18n_get(const char *code);
int i18n_count(void);
const I18nLocaleData *i18n_list(int index);   /* base locale first, then by order, code */

/* Every output function writes a NUL-terminated UTF-8 string and returns out. */
char *i18n_format(const I18nLocale *L, const char *tpl, const I18nParam *p, int np, char *out, int size);
char *i18n_t(const I18nLocale *L, const char *key, const I18nParam *p, int np, char *out, int size);
char *i18n_hw(const I18nLocale *L, const char *key, const I18nParam *p, int np, char *out, int size);
/* n as a decimal string ("84", "1.5"). Exact forms such as "=0" win. */
char *i18n_plural_s(const I18nLocale *L, const char *key, const char *n, int decimals,
                    const I18nParam *p, int np, char *out, int size);
char *i18n_plural(const I18nLocale *L, const char *key, long n, char *out, int size);
char *i18n_join(const I18nLocale *L, const char *const *items, int n, char *out, int size);
const char *i18n_app(const I18nLocale *L, const char *name);
char *i18n_num(const I18nLocale *L, double x, int decimals, char *out, int size);
int i18n_category(const I18nLocale *L, const char *n);

/* Raw lookup with fallback; NULL when missing everywhere. */
const char *i18n_lookup(const I18nLocale *L, int section, const char *key);
enum { I18N_STRINGS, I18N_HARDWARE, I18N_APPS };

/* Called once per missing key (may be NULL). */
extern void (*i18n_warn)(const char *msg);

#endif
