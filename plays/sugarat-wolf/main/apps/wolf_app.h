/*
 * Wolf Companion application (the only application of this firmware; boots straight into it).
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

/* Enter or show the application; registered in main/apps/apps_registry.c. */
void wolf_app_start(void);

/* Advance one main-loop pass; returns at once when the application is not in front. */
void wolf_app_poll(void);
