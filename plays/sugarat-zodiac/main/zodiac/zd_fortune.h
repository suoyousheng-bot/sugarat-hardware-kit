/* Offline daily reading: the same sign and local date give the same numbers on every badge and in
 * the web preview (zodiac-dials.html), which uses this exact hash and formula. */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { int year, month, day; } zd_date_t;

typedef struct {
    int score, love, work, money;   /**< 0..100 */
    int line;                       /**< index into zd_line */
} zd_reading_t;

/** FNV-1a over the bytes, then the MurmurHash3 finalizer. */
uint32_t zd_hash32(const char *s);
zd_reading_t zd_fortune(int sign, zd_date_t date);

#ifdef __cplusplus
}
#endif
