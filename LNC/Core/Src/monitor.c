/*
 * monitor.c - LNC Monitor module
 * See monitor.h for the public API and the class-level overview.
 *
 * Flow: monitor_task() wakes every 5 s (osDelayUntil) -> monitor_sample()
 * reads the sensors -> classify_mode() turns the reading into a mode
 * (section 2.10's rule) -> log_write() runs every round, event_mode_changed()
 * only when the mode actually differs from last round.
 */
#include "monitor.h"
#include "main.h"
#include "cmsis_os.h"
#include "DHT11.h"
#include "config.h"

/* ===============================================================
 * Module state
 * =============================================================== */

struct Monitor {
    monitor_mode_t mode;      /* mode as of the last completed round */
    osThreadId_t   task_handle;
    DHT_Handle    *dht;
    int16_t        last_temp_c;
    uint8_t        last_humidity_pct;
    monitor_measurement_t last_data; /* full last round, for monitor_get_latest() */
    osMutexId_t    latest_lock;      /* guards mode + last_data as a pair */
};

static struct Monitor g_monitor;

/* ===============================================================
 * Public API: create / destroy
 * =============================================================== */

static void monitor_task(void *argument);

Monitor *monitor_create(void)
{
    /* 256*4 (1024 bytes) was enough before COMM_MAX_VALUE grew 32->96
     * for the query feature (communication.c) -- comm_send()'s
     * comm_tx_item_t is now 98 bytes, and this task calls it (via
     * event_mode_changed()) nested underneath its own SD-card write
     * (log_write()) on every mode-change round, on top of DHT11's
     * bit-banging locals, two ADC reads, and several snprintf line
     * buffers. Same bug class, same fix as comm_rx_task's stack
     * overflow (communication.c) -- see CLAUDE.md. */
    const osThreadAttr_t task_attr = {
        .name = "monitorTask",
        .stack_size = 256 * 16,
        .priority = osPriorityNormal,
    };

    g_monitor.mode = MODE_UNKNOWN;
    g_monitor.last_temp_c = 0;
    g_monitor.last_humidity_pct = 0;
    g_monitor.last_data = (monitor_measurement_t){0};

    g_monitor.latest_lock = osMutexNew(NULL);
    if (g_monitor.latest_lock == NULL) {
        return NULL;
    }

    g_monitor.dht = DHT_Create(DHT_GPIO_Port, DHT_Pin, &htim2);
    if (g_monitor.dht == NULL) {
        return NULL;
    }

    g_monitor.task_handle = osThreadNew(monitor_task, &g_monitor, &task_attr);
    if (g_monitor.task_handle == NULL) {
        DHT_Destroy(g_monitor.dht);
        return NULL;
    }

    return &g_monitor;
}

void monitor_destroy(Monitor *m)
{
    if (m == NULL) {
        return;
    }
    if (m->task_handle != NULL) {
        osThreadTerminate(m->task_handle);
    }
    if (m->dht != NULL) {
        DHT_Destroy(m->dht);
    }
}

/* ===============================================================
 * Classification
 *
 * Per-sensor tier first, then aggregated per section 2.10's exact rule:
 * Error if *any* sensor is in its own Error tier; else Warning if *any*
 * sensor is in its own Warning tier; else every sensor was Normal.
 * =============================================================== */

typedef enum { TIER_NORMAL, TIER_WARNING, TIER_ERROR } sensor_tier_t;

static sensor_tier_t classify_range(int32_t value,
                                     int32_t normal_min, int32_t normal_max,
                                     int32_t warning_min, int32_t warning_max)
{
    if (value >= normal_min && value <= normal_max) {
        return TIER_NORMAL;
    }
    if (value >= warning_min && value <= warning_max) {
        return TIER_WARNING;
    }
    return TIER_ERROR;
}

static sensor_tier_t classify_lower_bound(int32_t value, int32_t normal_min, int32_t warning_min)
{
    if (value >= normal_min) {
        return TIER_NORMAL;
    }
    if (value >= warning_min) {
        return TIER_WARNING;
    }
    return TIER_ERROR;
}

static monitor_mode_t classify_mode(const monitor_measurement_t *data)
{
//    /* By value, not a pointer to live state -- config_get_limits()
//     * copies out under Configuration's own lock, so this snapshot
//     * can't be torn by a SET_*///GET_CONFIG landing mid-read (see
//     //* config.h).

    config_limits_t limits = config_get_limits();
    sensor_tier_t temp  = classify_range(data->temp_c, limits.temp_normal_min, limits.temp_normal_max,
                                          limits.temp_warning_min, limits.temp_warning_max);
    sensor_tier_t hum   = classify_lower_bound(data->humidity_pct, limits.humidity_normal_min, limits.humidity_warning_min);
    sensor_tier_t light = classify_lower_bound(data->light_pct, limits.light_normal_min, limits.light_warning_min);
    sensor_tier_t batt  = classify_lower_bound(data->battery_pct, limits.battery_normal_min, limits.battery_warning_min);

    if (temp == TIER_ERROR || hum == TIER_ERROR || light == TIER_ERROR || batt == TIER_ERROR) {
        return MODE_ERROR;
    }
    if (temp == TIER_WARNING || hum == TIER_WARNING || light == TIER_WARNING || batt == TIER_WARNING) {
        return MODE_WARNING;
    }
    return MODE_NORMAL;
}

/* ===============================================================
 * Mode <-> string
 * =============================================================== */

const char *monitor_mode_name(monitor_mode_t mode)
{
    switch (mode) {
    case MODE_UNKNOWN: return "UNKNOWN";
    case MODE_NORMAL:  return "NORMAL";
    case MODE_WARNING: return "WARNING";
    case MODE_ERROR:   return "ERROR";
    default:           return "?";
    }
}

/* ===============================================================
 * Dispatch stubs
 *
 * Empty placeholders so the firmware links before Log/Event exist --
 * same pattern communication.c already uses for Configuration/Init/Log.
 * Each real module later defines the same-named non-weak function to
 * take over; no edits needed here when that happens.
 * =============================================================== */

__attribute__((weak)) void log_write(const monitor_measurement_t *data, monitor_mode_t mode)
{
    (void)data;
    (void)mode;
}

__attribute__((weak)) void event_mode_changed(const monitor_measurement_t *data,
                                               monitor_mode_t old_mode, monitor_mode_t new_mode)
{
    (void)data;
    (void)old_mode;
    (void)new_mode;
}

/* ===============================================================
 * Sampling
 * =============================================================== */

static uint8_t read_adc_pct(ADC_HandleTypeDef *hadc, uint32_t max_value)
{
    uint32_t raw;

    HAL_ADC_Start(hadc);
    HAL_ADC_PollForConversion(hadc, HAL_MAX_DELAY);
    raw = HAL_ADC_GetValue(hadc);
    HAL_ADC_Stop(hadc);

    return (uint8_t)((raw * 100u) / max_value);
}

static void monitor_sample(Monitor *self, monitor_measurement_t *data)
{
    DHT_Data dht_data;

    data->battery_pct = read_adc_pct(&hadc1, 4095u);
    data->light_pct   = read_adc_pct(&hadc2, 255u);

    if (DHT_Read(self->dht, &dht_data) == DHT_OK) {
        self->last_temp_c = (int16_t)dht_data.temperature_int;
        self->last_humidity_pct = dht_data.humidity_int;
    }
    /* else: DHT11 read failed (no response / bad checksum) -- keep the
     * last known-good reading rather than report garbage, or force a
     * spurious mode transition off one noisy sample. */

    data->temp_c = self->last_temp_c;
    data->humidity_pct = self->last_humidity_pct;
}

/* ===============================================================
 * Task
 * =============================================================== */

static void monitor_task(void *argument)
{
    Monitor *self = (Monitor *)argument;
    uint32_t tick = osKernelGetTickCount();

    for (;;) {
        monitor_measurement_t data;
        monitor_mode_t new_mode;

        monitor_sample(self, &data);
        new_mode = classify_mode(&data);

        if (new_mode != self->mode) {
            event_mode_changed(&data, self->mode, new_mode);
        }
        log_write(&data, new_mode);

        osMutexAcquire(self->latest_lock, osWaitForever);
        self->mode = new_mode;
        self->last_data = data;
        osMutexRelease(self->latest_lock);

        tick += 5000U;
        osDelayUntil(tick); /* == FreeRTOS vTaskDelayUntil() */
    }
}

/* ===============================================================
 * Queries
 * =============================================================== */

void monitor_get_latest(monitor_measurement_t *out_data, monitor_mode_t *out_mode)
{
    osMutexAcquire(g_monitor.latest_lock, osWaitForever);
    *out_data = g_monitor.last_data;
    *out_mode = g_monitor.mode;
    osMutexRelease(g_monitor.latest_lock);
}

/* Kept for reference -- this was main.c's TEMPORARY debug log_write(),
 * removed once the real Log module took over the strong definition
 * (having both would be a duplicate-symbol link error, same class as
 * the EXTI3/USART2 IRQHandler collisions). Useful again only if Log
 * needs a UART-visible fallback for on-hardware debugging.
 *
 * void log_write(const monitor_measurement_t *data, monitor_mode_t mode)
 * {
 *     printf("temp=%dC hum=%u%% light=%u%% batt=%u%% mode=%s\r\n",
 *            data->temp_c, data->humidity_pct, data->light_pct,
 *            data->battery_pct, monitor_mode_name(mode));
 * }
 */
