/*
 * CLDR plural rule compiler (C89 port of i18n/pulse-i18n.js).
 *
 *   condition     = and_condition ('or' and_condition)*
 *   and_condition = relation ('and' relation)*
 *   relation      = expr ('=' | '!=') range_list
 *                 | expr 'is' 'not'? value
 *                 | expr 'not'? ('in' | 'within') range_list
 *   expr          = operand ('%' | 'mod' value)?
 *   range_list    = (value | value '..' value) (',' range_list)*
 *
 * Sample lists ("@integer ...", "@decimal ...") are ignored.
 */
#include "plural.h"

const char *const pl_category_names[PL_NCAT] = { "zero", "one", "two", "few", "many", "other" };

#define TOK_MAX 96
#define TOK_LEN 16

typedef struct {
    char tok[TOK_MAX][TOK_LEN];
    int count;
    int pos;
    int err;
} Lexer;

static int is_digit(char c) { return c >= '0' && c <= '9'; }
static int is_lower(char c) { return c >= 'a' && c <= 'z'; }
static int is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void push_tok(Lexer *lx, const char *s, int len)
{
    int k;
    if (lx->count >= TOK_MAX || len >= TOK_LEN) { lx->err = 1; return; }
    for (k = 0; k < len; k++) lx->tok[lx->count][k] = s[k];
    lx->tok[lx->count][len] = 0;
    lx->count++;
}

static void tokenize(const char *src, Lexer *lx)
{
    const char *p = src;
    lx->count = 0; lx->pos = 0; lx->err = 0;
    while (*p && *p != '@' && !lx->err) {
        if (is_space(*p)) { p++; continue; }
        if (p[0] == '.' && p[1] == '.') { push_tok(lx, p, 2); p += 2; }
        else if (p[0] == '!' && p[1] == '=') { push_tok(lx, p, 2); p += 2; }
        else if (*p == '=' || *p == '%' || *p == ',') { push_tok(lx, p, 1); p++; }
        else if (is_lower(*p)) {
            const char *q = p;
            while (is_lower(*q)) q++;
            push_tok(lx, p, (int)(q - p)); p = q;
        } else if (is_digit(*p)) {
            const char *q = p;
            while (is_digit(*q)) q++;
            if (q[0] == '.' && is_digit(q[1])) { q++; while (is_digit(*q)) q++; }
            push_tok(lx, p, (int)(q - p)); p = q;
        } else lx->err = 1;
    }
}

static const char *peek(Lexer *lx) { return lx->pos < lx->count ? lx->tok[lx->pos] : ""; }
static const char *next(Lexer *lx) { return lx->pos < lx->count ? lx->tok[lx->pos++] : ""; }

static double parse_num(const char *s, int *ok)
{
    double v = 0, scale = 1;
    int seen = 0;
    while (is_digit(*s)) { v = v * 10 + (*s - '0'); s++; seen = 1; }
    if (*s == '.') {
        s++;
        while (is_digit(*s)) { scale /= 10; v += (*s - '0') * scale; s++; }
    }
    if (!seen || *s) *ok = 0;
    return v;
}

static double num(Lexer *lx)
{
    int ok = 1;
    double v = parse_num(next(lx), &ok);
    if (!ok) lx->err = 1;
    return v;
}

static void range_list(Lexer *lx, PlRelation *r)
{
    r->nranges = 0;
    do {
        double lo = num(lx), hi = lo;
        if (str_eq(peek(lx), "..")) { next(lx); hi = num(lx); }
        if (r->nranges >= PL_MAX_RANGES) { lx->err = 1; return; }
        r->lo[(int)r->nranges] = lo;
        r->hi[(int)r->nranges] = hi;
        r->nranges++;
        if (!str_eq(peek(lx), ",")) break;
        next(lx);
    } while (!lx->err);
}

static void relation(Lexer *lx, PlRelation *r)
{
    const char *op = next(lx);
    if (!(op[0] && !op[1] && (op[0] == 'n' || op[0] == 'i' || op[0] == 'v' || op[0] == 'w' ||
                              op[0] == 'f' || op[0] == 't' || op[0] == 'c' || op[0] == 'e'))) {
        lx->err = 1; return;
    }
    r->op = op[0];
    r->mod = 0;
    r->neg = 0;
    r->integer_only = 1;
    if (str_eq(peek(lx), "%") || str_eq(peek(lx), "mod")) { next(lx); r->mod = num(lx); }
    op = next(lx);
    if (str_eq(op, "is")) {
        if (str_eq(peek(lx), "not")) { next(lx); r->neg = 1; }
        r->nranges = 1;
        r->lo[0] = r->hi[0] = num(lx);
    } else if (str_eq(op, "=") || str_eq(op, "!=")) {
        r->neg = (char)str_eq(op, "!=");
        range_list(lx, r);
    } else {
        if (str_eq(op, "not")) { r->neg = 1; op = next(lx); }
        if (str_eq(op, "within")) r->integer_only = 0;
        else if (!str_eq(op, "in")) { lx->err = 1; return; }
        range_list(lx, r);
    }
}

int pl_compile(const char *src, PlRule *out)
{
    static Lexer lx; /* large; keep it off the stack */
    tokenize(src, &lx);
    out->nrel = 0; out->nor = 0; out->always = 0;
    if (lx.err) return 0;
    if (lx.count == 0) { out->always = 1; return 1; }
    for (;;) {
        for (;;) {
            if (out->nrel >= PL_MAX_RELS) return 0;
            relation(&lx, &out->rel[out->nrel++]);
            if (lx.err) return 0;
            if (!str_eq(peek(&lx), "and")) break;
            next(&lx);
        }
        if (out->nor >= PL_MAX_ORS) return 0;
        out->or_end[out->nor++] = out->nrel;
        if (!str_eq(peek(&lx), "or")) break;
        next(&lx);
    }
    return lx.pos == lx.count && !lx.err;
}

/* x mod m for non-negative x, matching JavaScript's % on doubles. */
static double dmod(double x, double m)
{
    double q;
    if (m <= 0) return x;
    q = x / m;
    if (q < 2147483647.0) q = (double)(long)q;
    else { /* very large: reduce in steps */
        double r = x;
        while (r >= m * 2147483647.0) r -= m * 2147483647.0;
        return dmod(r, m);
    }
    return x - q * m;
}

static double operand(const PlOperands *o, char op)
{
    switch (op) {
    case 'n': return o->n;
    case 'i': return o->i;
    case 'v': return o->v;
    case 'w': return o->w;
    case 'f': return o->f;
    case 't': return o->t;
    default:  return o->c;
    }
}

static int is_integer(double x)
{
    if (x >= 2147483647.0) return 1;
    return x == (double)(long)x;
}

static int rel_eval(const PlRelation *r, const PlOperands *o)
{
    double x = operand(o, r->op);
    int k, hit = 0;
    if (r->mod) x = dmod(x, r->mod);
    for (k = 0; k < r->nranges && !hit; k++)
        hit = x >= r->lo[k] && x <= r->hi[k] && (!r->integer_only || is_integer(x));
    return r->neg ? !hit : hit;
}

static int rule_eval(const PlRule *r, const PlOperands *o)
{
    int b, k, start = 0;
    if (r->always) return 1;
    for (b = 0; b < r->nor; b++) {
        int ok = 1;
        for (k = start; k < r->or_end[b] && ok; k++) ok = rel_eval(&r->rel[k], o);
        if (ok) return 1;
        start = r->or_end[b];
    }
    return 0;
}

int pl_select(const PlRules *r, const PlOperands *o)
{
    int c;
    for (c = 0; c < PL_OTHER; c++)
        if (r->has[c] && rule_eval(&r->rule[c], o)) return c;
    return PL_OTHER;
}

/* Digits string to double. */
static double digits_val(const char *s, int len)
{
    double v = 0;
    int k;
    for (k = 0; k < len; k++) v = v * 10 + (s[k] - '0');
    return v;
}

void pl_operands(const char *src, PlOperands *o)
{
    char buf[80];
    int len = 0, k, dot = -1, e = 0, epos = -1, flen, tlen;
    const char *s = src;

    while (is_space(*s)) s++;
    if (*s == '-' || *s == '+') s++;
    while (*s && !is_space(*s) && len < (int)sizeof(buf) - 24) buf[len++] = *s++;
    buf[len] = 0;

    /* Exponent: 1.2c3 or 1.2e3 */
    for (k = 0; k < len; k++) if (buf[k] == 'c' || buf[k] == 'e' || buf[k] == 'C' || buf[k] == 'E') { epos = k; break; }
    if (epos >= 0) {
        char digits[80];
        int nd = 0, pos;
        for (k = epos + 1; k < len; k++) e = e * 10 + (buf[k] - '0');
        len = epos; buf[len] = 0;
        for (k = 0; k < len; k++) { if (buf[k] == '.') dot = k; else digits[nd++] = buf[k]; }
        pos = (dot < 0 ? len : dot) + e;
        while (nd < pos && nd < (int)sizeof(digits) - 2) digits[nd++] = '0';
        len = 0;
        for (k = 0; k < nd; k++) {
            if (k == pos && pos < nd) buf[len++] = '.';
            buf[len++] = digits[k];
        }
        buf[len] = 0;
        dot = -1;
    }
    for (k = 0; k < len; k++) if (buf[k] == '.') { dot = k; break; }

    if (dot < 0) {
        o->i = digits_val(buf, len);
        o->v = o->w = o->f = o->t = 0;
        o->n = o->i;
    } else {
        o->i = digits_val(buf, dot);
        flen = len - dot - 1;
        tlen = flen;
        while (tlen > 0 && buf[dot + tlen] == '0') tlen--;
        o->v = flen;
        o->w = tlen;
        o->f = digits_val(buf + dot + 1, flen);
        o->t = digits_val(buf + dot + 1, tlen);
        {
            double scale = 1;
            for (k = 0; k < flen; k++) scale *= 10;
            o->n = o->i + o->f / scale;
        }
    }
    o->c = e;
}

int pl_select_str(const PlRules *r, const char *s)
{
    PlOperands o;
    pl_operands(s, &o);
    return pl_select(r, &o);
}

int pl_category_index(const char *name)
{
    int c;
    for (c = 0; c < PL_NCAT; c++) if (str_eq(name, pl_category_names[c])) return c;
    return -1;
}
