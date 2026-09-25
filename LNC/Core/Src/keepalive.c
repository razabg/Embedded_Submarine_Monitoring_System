/*
 * keepalive.c - LNC Keep-Alive module
 * See keepalive.h for the public API and the class-level overview.
 */
#include "keepalive.h"
#include "main.h"
#include "cmsis_os.h"
#include "monitor.h"
#include "tlv.h"

#define KEEPALIVE_PERIOD_MS 6000U

struct KeepAlive {
    Communication *comm;
    osThreadId_t   task_handle;
};

static struct KeepAlive g_keepalive;

/* ===============================================================
 * Wire format for TLV_TAG_KEEP_ALIVE -- provisional, same as
 * event.c's mode_change_payload_t / init.c's time_payload_t: nothing
 * on the Central Computer side parses this tag yet, so this is a
 * placeholder Management Command will need to match once it's built.
 * Timestamp fields match time_payload_t's layout/units (year is an
 * offset from 2000).
 * =============================================================== */

typedef struct __attribute__((packed)) {
    uint8_t year;
    uint8_t month;
    uint8_t date;
    uint8_t hour;
    uint8_t min;
    uint8_t sec;
    int16_t temp_c;
    uint8_t humidity_pct;
    uint8_t light_pct;
    uint8_t battery_pct;
    uint8_t mode;
} keepalive_payload_t;

/* ===============================================================
 * Task
 * =============================================================== */

static void keepalive_task(void *argument)
{
    struct KeepAlive *self = (struct KeepAlive *)argument;
    uint32_t tick = osKernelGetTickCount();

    for (;;) {
        RTC_TimeTypeDef t;
        RTC_DateTypeDef d;
        monitor_measurement_t data;
        monitor_mode_t mode;
        keepalive_payload_t payload;

        /* HAL quirk (same as event.c's format_timestamp()): GetTime must
         * be immediately followed by GetDate for the shadow registers to
         * latch correctly, even though only the time half is new here. */
        HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN);
        HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN);
        monitor_get_latest(&data, &mode);

        payload.year = d.Year;
        payload.month = d.Month;
        payload.date = d.Date;
        payload.hour = t.Hours;
        payload.min = t.Minutes;
        payload.sec = t.Seconds;
        payload.temp_c = data.temp_c;
        payload.humidity_pct = data.humidity_pct;
        payload.light_pct = data.light_pct;
        payload.battery_pct = data.battery_pct;
        payload.mode = (uint8_t)mode;

        (void)comm_send(self->comm, TLV_TAG_KEEP_ALIVE,
                         (const uint8_t *)&payload, sizeof(payload));

        tick += KEEPALIVE_PERIOD_MS;
        osDelayUntil(tick); /* == FreeRTOS vTaskDelayUntil() */
    }
}

/* ===============================================================
 * Public API: create / destroy
 * =============================================================== */

KeepAlive *keepalive_create(Communication *comm)
{
    /* No SD-card write on this task (unlike Monitor/ObjectDetection),
     * but comm_send()'s comm_tx_item_t is still 98 bytes now (grown
     * from 34 when COMM_MAX_VALUE went 32->96) -- bumped as a cheap
     * precaution, same reasoning as monitor.c's stack fix. */
    const osThreadAttr_t task_attr = {
        .name = "keepAliveTask",
        .stack_size = 256 * 8,
        .priority = osPriorityNormal,
    };

    g_keepalive.comm = comm;

    g_keepalive.task_handle = osThreadNew(keepalive_task, &g_keepalive, &task_attr);
    if (g_keepalive.task_handle == NULL) {
        return NULL;
    }

    return &g_keepalive;
}

void keepalive_destroy(KeepAlive *k)
{
    if (k == NULL) {
        return;
    }
    if (k->task_handle != NULL) {
        osThreadTerminate(k->task_handle);
    }
}
