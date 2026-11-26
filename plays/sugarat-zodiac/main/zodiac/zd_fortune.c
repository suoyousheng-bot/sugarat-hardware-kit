#include "zd_fortune.h"

#include <math.h>
#include <stdio.h>
#include "zd_assets.h"

uint32_t zd_hash32(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) {
        h ^= (uint8_t)*s;
        h *= 16777619u;
    }
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}

static double unit(zd_date_t d, int sign, const char *key)
{
    char text[48];
    snprintf(text, sizeof(text), "%d-%d-%d|%d|%s", d.year, d.month, d.day, sign, key);
    return zd_hash32(text) / 4294967296.0;
}

static int sub_score(int score, double r)
{
    double v = score + (r - .5) * 34;
    if (v < 38) v = 38;
    if (v > 99) v = 99;
    return (int)floor(v + .5);
}

zd_reading_t zd_fortune(int sign, zd_date_t d)
{
    zd_reading_t r;
    r.score = (int)floor(56 + 42 * (unit(d, sign, "score-a") + unit(d, sign, "score-b")) / 2 + .5);
    r.love = sub_score(r.score, unit(d, sign, "love"));
    r.work = sub_score(r.score, unit(d, sign, "work"));
    r.money = sub_score(r.score, unit(d, sign, "money"));
    r.line = (int)(unit(d, sign, "line") * ZD_LINES);
    return r;
}
