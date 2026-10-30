#include "arcade_text.h"
#include <string.h>

/* Bounded UTF-8 prefix with safe handling of truncated/invalid metadata.
 * Output never contains a dangling continuation byte. We use ASCII "..."
 * so a missing glyph cannot blank the suffix on older player fonts. */
void pa_utf8_preview(char *dst, size_t cap, const char *src, size_t max_chars) {
    if (!dst || !cap) return;
    dst[0] = 0;
    if (!src || max_chars == 0) return;
    size_t i = 0, o = 0, chars = 0;
    while (src[i] && chars < max_chars && o + 1 < cap) {
        unsigned char c = (unsigned char)src[i];
        size_t len = 1;
        if (c >= 0xC2 && c <= 0xDF) len = 2;
        else if (c >= 0xE0 && c <= 0xEF) len = 3;
        else if (c >= 0xF0 && c <= 0xF4) len = 4;
        else if (c >= 0x80) { dst[o++] = '?'; i++; chars++; continue; }
        if (len > 1) {
            int good = 1;
            for (size_t j = 1; j < len; j++) {
                unsigned char v = (unsigned char)src[i+j];
                if (!v || v < 0x80 || v > 0xBF) { good = 0; break; }
            }
            if (good && len == 3) {
                unsigned char second = (unsigned char)src[i+1];
                if ((c == 0xE0 && second < 0xA0) || (c == 0xED && second > 0x9F)) good=0;
            }
            if (good && len == 4) {
                unsigned char second=(unsigned char)src[i+1];
                if ((c == 0xF0 && second < 0x90) || (c == 0xF4 && second > 0x8F)) good=0;
            }
            if (!good) { dst[o++] = '?'; i++; chars++; continue; }
        }
        if (o + len >= cap) break;
        for(size_t j=0;j<len;j++) dst[o++] = src[i+j];
        i += len;
        chars++;
    }
    /* Do not place an ellipsis mid UTF-8 sequence; cap may be very small. */
    if (src[i] && o + 3 < cap) {
        dst[o++]='.';dst[o++]='.';dst[o++]='.';
    }
    dst[o] = 0;
}
