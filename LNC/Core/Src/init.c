/*
 * init.c - LNC Init module
 * See init.h for the public API and the class-level overview.
 */
#include "init.h"
#include "main.h"
#include "event.h"
#include "log.h"
#include "config.h"
#include "monitor.h"
#include "objectdetection.h"
#include "keepalive.h"
#include "watchdog.h"
#include "tlv.h"
#include "RTC_ds1307_I2C.h"
#include <stdbool.h>

struct Init {
    Communication *comm;
};

static struct Init g_init;

/* ===============================================================
 * Wire format for the time-sync exchange (TLV_TAG_TIME_REPLY,
 * TLV_TAG_TIME_SYNC_REPLY) -- provisional, same as event.c's
 * mode_change_payload_t: nothing on the Central Computer side parses
 * this yet, so this is a placeholder Management Command will need to
 * match once it's built.
 * =============================================================== */

typedef struct __attribute__((packed)) {
    uint8_t year;  /* 0-99, offset from 2000 */
    uint8_t month; /* 1-12 */
    uint8_t date;  /* 1-31 */
    uint8_t hour;  /* 0-23 */
    uint8_t min;   /* 0-59 */
    uint8_t sec;   /* 0-59 */
    uint8_t dow;   /* 1-7, Monday=1..Sunday=7 (HAL's convention) */
} time_payload_t;

/* ===============================================================
 * Boot-time DS1307 -> internal RTC sync
 *
 * CLAUDE.md's RTC design: DS1307 is the durable fallback, read once
 * at boot, before Communication (and any CC correction) is even up.
 * A DS1307 read failure just leaves the internal RTC at whatever
 * CubeMX's placeholder set it to -- not fatal to boot.
 * =============================================================== */

static void sync_internal_rtc_from_ds1307(void)
{
    RTC_Time_t ds;
    RTC_TimeTypeDef t = {0};
    RTC_DateTypeDef d = {0};

    if (RTC_GetTime(&hi2c3, &ds) != HAL_OK) {
        return;
    }

    t.Hours = ds.hour;
    t.Minutes = ds.min;
    t.Seconds = ds.sec;
    t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    t.StoreOperation = RTC_STOREOPERATION_RESET;
    (void)HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BIN);

    d.WeekDay = ds.dow;
    d.Month = ds.month;
    d.Date = ds.date;
    d.Year = ds.year;
    (void)HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BIN);
}

/* ===============================================================
 * Watchdog-reset report (section 2.9)
 * =============================================================== */

static bool check_and_clear_watchdog_reset(void)
{
    bool was_wd_reset = __HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != 0;
    __HAL_RCC_CLEAR_RESET_FLAGS();
    return was_wd_reset;
}

/* ===============================================================
 * CC <-> Init time exchange
 * =============================================================== */

/* CC asked (TLV_TAG_GET_TIME) what time the LNC has -- answer with
 * the internal RTC's current reading. */
static void reply_with_current_time(void)
{
    RTC_TimeTypeDef t;
    RTC_DateTypeDef d;
    time_payload_t payload;

    HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN);

    payload.year = d.Year;
    payload.month = d.Month;
    payload.date = d.Date;
    payload.hour = t.Hours;
    payload.min = t.Minutes;
    payload.sec = t.Seconds;
    payload.dow = d.WeekDay;

    (void)comm_send(g_init.comm, TLV_TAG_TIME_REPLY,
                     (const uint8_t *)&payload, sizeof(payload));
}

/* CC answered (TLV_TAG_TIME_SYNC_REPLY) Init's own boot-time sync
 * request -- the unified set_time() operation from CLAUDE.md's RTC
 * design: write the CC-provided value to internal RTC and DS1307,
 * both directly from this value, no read-back through either. */
static void apply_cc_time(const time_payload_t *p)
{
    RTC_TimeTypeDef t = {0};
    RTC_DateTypeDef d = {0};
    RTC_Time_t ds;

    t.Hours = p->hour;
    t.Minutes = p->min;
    t.Seconds = p->sec;
    t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    t.StoreOperation = RTC_STOREOPERATION_RESET;
    (void)HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BIN);

    d.WeekDay = p->dow;
    d.Month = p->month;
    d.Date = p->date;
    d.Year = p->year;
    (void)HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BIN);

    ds.sec = p->sec;
    ds.min = p->min;
    ds.hour = p->hour;
    ds.dow = p->dow;
    ds.date = p->date;
    ds.month = p->month;
    ds.year = p->year;
    (void)RTC_SetTime(&hi2c3, &ds);
}

/* Communication -> Init: dispatched by tag (communication.c's
 * comm_route_frame()). Overrides communication.c's weak stub. */
void init_on_frame(const tlv_frame_t *f)
{
    if (f == NULL) {
        return;
    }

    switch (f->tag) {
    case TLV_TAG_GET_TIME:
        reply_with_current_time();
        break;

    case TLV_TAG_TIME_SYNC_REPLY:
        if (f->value != NULL && f->len == sizeof(time_payload_t)) {
            apply_cc_time((const time_payload_t *)f->value);
        }
        break;

    default:
        break;
    }
}

/* ===============================================================
 * Public API: create / destroy
 * =============================================================== */

/* Section 2.7: "starts all system activities" -- Event first (Init's
 * own startup report needs it to already exist), then everything
 * else. Doesn't block waiting for the CC's time-sync reply (see
 * CLAUDE.md's RTC design) -- it fires the request and moves on, same
 * as every other module here just gets started, not waited on. */
Init *init_create(Communication *comm)
{
    bool was_wd_reset;

    g_init.comm = comm;

    if (event_create(comm) == NULL) {
        return NULL;
    }

    was_wd_reset = check_and_clear_watchdog_reset();
    sync_internal_rtc_from_ds1307();
    event_startup(was_wd_reset);
    (void)comm_send(g_init.comm, TLV_TAG_TIME_SYNC_REQUEST, NULL, 0);

    if (log_create(comm) == NULL) {
        return NULL;
    }
    if (config_create(comm) == NULL) {
        return NULL;
    }
    if (monitor_create() == NULL) {
        return NULL;
    }
    if (objdet_create() == NULL) {
        return NULL;
    }
    if (keepalive_create(comm) == NULL) {
        return NULL;
    }
    if (watchdog_create() == NULL) {
        return NULL;
    }

    return &g_init;
}

void init_destroy(Init *i)
{
    (void)i;
}
