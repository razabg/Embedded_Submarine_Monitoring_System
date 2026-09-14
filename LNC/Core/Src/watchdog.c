/*
 * watchdog.c - LNC Watchdog module
 * See watchdog.h for the public API and the class-level overview.
 *
 * IWDG timeout is configured in MX_IWDG_Init() (CubeMX-generated, in
 * main.c) to 4000ms -- HAL_IWDG_Init() both configures *and* starts
 * the countdown immediately, before osKernelStart() even runs, so the
 * 4s margin also has to comfortably cover the rest of main()'s boot
 * sequence (the other module create() calls, SD card mounts, Flash
 * reads) before this task's first refresh actually executes. Refreshing
 * every 1000ms gives a 4x margin over that timeout -- generous against
 * both LSI's ~5% inaccuracy and normal scheduling jitter.
 */
#include "watchdog.h"
#include "main.h"
#include "cmsis_os.h"

#define WATCHDOG_REFRESH_MS 1000U

struct Watchdog {
    osThreadId_t task_handle;
};

static struct Watchdog g_watchdog;

/* ===============================================================
 * Task
 * =============================================================== */

static void watchdog_task(void *argument)
{
    (void)argument;
    uint32_t tick = osKernelGetTickCount();

    for (;;) {
        HAL_IWDG_Refresh(&hiwdg);//refersh every 1 sec the 4 sec coountdown

        tick += WATCHDOG_REFRESH_MS;
        osDelayUntil(tick); /* == FreeRTOS vTaskDelayUntil() */
    }
}

/* ===============================================================
 * Public API: create / destroy
 * =============================================================== */

Watchdog *watchdog_create(void)
{
    /* Above every other task in the firmware (Communication's TX/RX
     * tasks included, at osPriorityAboveNormal) -- Watchdog's only job
     * is to never miss a refresh, so it must never be starved by
     * anything else, including a long-running SD-card scan (e.g.
     * QUERY_DATA/QUERY_EVENTS in comm_rx_task -- see log.c/event.c and
     * CLAUDE.md). */
    const osThreadAttr_t task_attr = {
        .name = "watchdogTask",
        .stack_size = 256 * 4,
        .priority = osPriorityHigh,
    };

    g_watchdog.task_handle = osThreadNew(watchdog_task, NULL, &task_attr);
    if (g_watchdog.task_handle == NULL) {
        return NULL;
    }

    return &g_watchdog;
}

void watchdog_destroy(Watchdog *w)
{
    if (w == NULL) {
        return;
    }
    if (w->task_handle != NULL) {
        osThreadTerminate(w->task_handle);
    }
}
