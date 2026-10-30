/* UI-safe UTF-8 shortening (GPL-3.0-or-later).
 * Never split a multibyte code point when displaying music tags. */
#ifndef POCKET_ARCADE_TEXT_H
#define POCKET_ARCADE_TEXT_H
#include <stddef.h>
void pa_utf8_preview(char *dst, size_t cap, const char *src, size_t max_chars);
#endif
