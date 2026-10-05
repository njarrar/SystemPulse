/*
 * Text output for Win9x and NT. Catalog strings are UTF-8.
 *  - ANSI mode (Win9x): UTF-16 -> locale code page (1252, 1256, ...) and
 *    ExtTextOutA with a font of the matching charset.
 *  - Unicode mode (NT): ExtTextOutW.
 * FSI/PDI isolates have no glyph or code page slot on these systems, so
 * they become LRM or RLM marks at draw time, picked from the isolated text.
 */
#ifndef PULSE_TEXT_H
#define PULSE_TEXT_H

#include <windows.h>

enum { TA_START_ = 0, TA_END_ = 1, TA_CENTER_ = 2 };

void txt_mode(int ansi, int codepage, int rtl);
int  txt_is_ansi(void);
int  txt_width(HDC dc, const char *utf8);
/* Draws inside r, vertically centred. align: TA_START_/TA_END_/TA_CENTER_
 * relative to reading direction. Adds an ellipsis when the text is too wide.
 * Returns the drawn width. */
int  txt_draw(HDC dc, const RECT *r, const char *utf8, int align, COLORREF color);

/* Converters. Return the output length (characters, no NUL). */
int  utf8_to_w(const char *s, WCHAR *out, int cap);
int  w_to_utf8(const WCHAR *s, int len, char *out, int cap);
int  acp_to_utf8(const char *s, char *out, int cap);
/* For window and menu text: ANSI in the active code page or UTF-16. */
int  txt_to_ansi(const char *utf8, char *out, int cap);

#endif
