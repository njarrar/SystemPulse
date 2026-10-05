/*
 * CLDR plural rules, C89 port of the compiler in i18n/pulse-i18n.js.
 * No CRT use: safe for the Win98 build that links without msvcrt.
 */
#ifndef PULSE_PLURAL_H
#define PULSE_PLURAL_H

#define PL_MAX_RANGES 6
#define PL_MAX_RELS   12
#define PL_MAX_ORS    8

/* Category indexes, in the order the engine tests them. */
enum { PL_ZERO, PL_ONE, PL_TWO, PL_FEW, PL_MANY, PL_OTHER, PL_NCAT };

typedef struct {
    double n;            /* absolute value */
    double i;            /* integer digits */
    double v;            /* visible fraction digits */
    double w;            /* visible fraction digits without trailing zeros */
    double f;            /* visible fraction digits as a number */
    double t;            /* f without trailing zeros */
    double c;            /* compact exponent (c and e are the same here) */
} PlOperands;

typedef struct {
    char op;             /* one of n i v w f t c e */
    char neg;
    char integer_only;   /* "in" and "=" need integers, "within" does not */
    char nranges;
    double mod;          /* 0 means no modulus */
    double lo[PL_MAX_RANGES];
    double hi[PL_MAX_RANGES];
} PlRelation;

typedef struct {
    int nrel;
    int nor;
    int or_end[PL_MAX_ORS];    /* index one past the last relation of each "or" branch */
    PlRelation rel[PL_MAX_RELS];
    int always;                /* empty rule matches everything */
} PlRule;

typedef struct {
    int has[PL_NCAT];          /* category has a rule (never set for "other") */
    PlRule rule[PL_NCAT];
} PlRules;

extern const char *const pl_category_names[PL_NCAT];

/* Parses one rule source. Returns 1 on success, 0 on a syntax error. */
int pl_compile(const char *src, PlRule *out);

/* Fills operands from a decimal string such as "1", "1.50", "1.2e3", "-4". */
void pl_operands(const char *s, PlOperands *o);

/* Returns the category index for the operands. */
int pl_select(const PlRules *r, const PlOperands *o);

/* Convenience: category for a decimal string. */
int pl_select_str(const PlRules *r, const char *s);

/* Category name to index, or -1. */
int pl_category_index(const char *name);

#endif
