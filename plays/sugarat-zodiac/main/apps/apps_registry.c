#include "apps_registry.h"
#include "sdgoods_board.h"
#include "zd_app.h"

static const sdgoods_app_t apps[] = {
    {.label_zh = "星运表盘", .label_en = "Zodiac Ring", .icon = "app",
     .show = zd_app_show, .poll = zd_app_poll},
};

const sdgoods_app_t *const g_sdgoods_app = apps;
const int g_sdgoods_app_count = sizeof(apps) / sizeof(apps[0]);

void apps_register(void)
{
    static const sdgoods_nav_t navigation = {
        .home_create_show = zd_app_show,
        .home_show = zd_app_show,
        .apps_show = zd_app_show,
    };
    sdgoods_apps_set_poll(zd_app_poll);
    sdgoods_ui_set_nav(&navigation);
}
