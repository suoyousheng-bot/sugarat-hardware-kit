/* Host check for the Zodiac Ring screens: drives zd_ui through a scripted session, renders frames the
 * way the firmware does (12-row strips, picker cross-fade) and writes PPM files.
 *
 *   cc -std=c11 -O2 -Imain/zodiac tests/host_render.c main/zodiac/zd_*.c -lm -o /tmp/zd_host && /tmp/zd_host OUTDIR
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "zd_ui.h"

enum { ROWS = 12 };
static zd_scene_t face, picker;
static uint16_t frame[ZD_SCREEN * ZD_SCREEN], over[ZD_SCREEN * ROWS];

static double render(zd_ui_t *u, double now)
{
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    int mix;
    zd_ui_build(u, now, &face, &picker, &mix);
    for (int y = 0; y < ZD_SCREEN; y += ROWS) {
        uint16_t *strip = frame + y * ZD_SCREEN;
        if (mix < 256) zd_render_scene(&face, strip, ZD_SCREEN, 0, y, ROWS, 0, ZD_SCREEN);
        if (mix >= 256) zd_render_scene(&picker, strip, ZD_SCREEN, 0, y, ROWS, 0, ZD_SCREEN);
        else if (mix > 0) {
            zd_render_scene(&picker, over, ZD_SCREEN, 0, y, ROWS, 0, ZD_SCREEN);
            zd_blend_region(strip, over, ZD_SCREEN, 0, ROWS, 0, ZD_SCREEN, mix);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &b);
    if (face.overflow || picker.overflow) fprintf(stderr, "scene overflow at %.2f\n", now);
    return (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6;
}

static void save(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fprintf(f, "P6 %d %d 255\n", ZD_SCREEN, ZD_SCREEN);
    for (int i = 0; i < ZD_SCREEN * ZD_SCREEN; i++) {
        const uint16_t v = (uint16_t)(frame[i] >> 8 | frame[i] << 8);
        const unsigned char rgb[3] = {(unsigned char)((v >> 11) << 3), (unsigned char)(((v >> 5) & 63) << 2), (unsigned char)((v & 31) << 3)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static zd_ui_t ui;
static double clock_s;

static void run_to(double t)
{
    while (clock_s < t) {
        const double dt = 1.0 / 60;
        clock_s += dt;
        zd_ui_update(&ui, clock_s, (float)dt);
    }
}

static void shot(const char *dir, const char *name, double t)
{
    run_to(t);
    const double ms = render(&ui, clock_s);
    save(dir, name);
    printf("%-18s t=%6.2f render %.2f ms  face ops %d picker ops %d\n", name, clock_s, ms, face.count, picker.count);
}

static void save_cb(int sign, int lang, void *arg)
{
    (void)arg;
    printf("save sign=%d lang=%d\n", sign, lang);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    const zd_date_t date = {2026, 11, 26};
    for (int s = 0; s < 12; s++) {
        const zd_reading_t r = zd_fortune(s, date);
        printf("sign %2d score %d love %d work %d money %d line %d\n", s, r.score, r.love, r.work, r.money, r.line);
    }
    if (!zd_ui_init(&ui, 6, ZD_LANG_ZH, false, date, save_cb, NULL, 0)) return 1;
    shot(dir, "01-reveal", 0.6);
    shot(dir, "02-dial", 3.0);
    shot(dir, "03-idle", 25.0);
    zd_ui_touch(&ui, ZD_TOUCH_DOWN, 180, 200, clock_s);
    shot(dir, "04-hold", 25.6);
    shot(dir, "05-fade", 26.3);
    shot(dir, "06-picker", 27.5);
    zd_ui_touch(&ui, ZD_TOUCH_DOWN, 240, 200, clock_s);
    for (int i = 1; i <= 10; i++) {
        run_to(clock_s + .02);
        zd_ui_touch(&ui, ZD_TOUCH_MOVE, 240 - i * 7, 200, clock_s);
    }
    shot(dir, "07-drag", clock_s);
    zd_ui_touch(&ui, ZD_TOUCH_UP, 170, 200, clock_s);
    shot(dir, "08-settled", clock_s + 1.2);
    zd_ui_touch(&ui, ZD_TOUCH_DOWN, 201, 252, clock_s);
    zd_ui_touch(&ui, ZD_TOUCH_UP, 201, 252, clock_s);
    shot(dir, "09-english", clock_s + .8);
    zd_ui_touch(&ui, ZD_TOUCH_DOWN, 180, 150, clock_s);
    zd_ui_touch(&ui, ZD_TOUCH_UP, 180, 150, clock_s);
    shot(dir, "10-saved", clock_s + .5);
    shot(dir, "11-back", clock_s + 1.2);
    shot(dir, "12-dial-en", clock_s + 2.5);
    run_to(clock_s + 25);
    shot(dir, "13-idle-en", clock_s);
    return 0;
}
