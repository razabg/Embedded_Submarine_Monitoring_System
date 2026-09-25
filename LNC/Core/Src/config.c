/*
 * config.c - LNC Configuration module
 * See config.h for the public API and the class-level overview.
 */
#include "config.h"
#include "main.h"
#include "event.h"
#include "tlv.h"
#include "cmsis_os.h"
#include <stdbool.h>
#include <stddef.h>

#define CONFIG_MAGIC 0x434F4E46u /* "CONF" */

/* Last page of the last bank -- firmware code lives in Bank 1, so
 * writes here don't stall code fetch on this dual-bank part
 * (read-while-write across banks). Computed from the HAL's own
 * FLASH_SIZE (read live from the chip's factory size register) rather
 * than a hardcoded address, so this stays correct across the L476's
 * C/E/G flash-size variants. */
#define CONFIG_FLASH_BANK FLASH_BANK_2
#define CONFIG_FLASH_PAGE ((FLASH_BANK_SIZE / FLASH_PAGE_SIZE) - 1U)
#define CONFIG_FLASH_ADDR (FLASH_BASE + FLASH_BANK_SIZE + (CONFIG_FLASH_PAGE * FLASH_PAGE_SIZE))

typedef struct __attribute__((packed)) {
    uint32_t magic;
    config_limits_t limits;
} config_flash_data_t;

struct Config {
    config_limits_t limits;
    Communication *comm;  /* to reply to TLV_TAG_GET_CONFIG */
    osMutexId_t lock;      //* guards limits: comm_rx_task writes (SET_*/GET_CONFIG), monitor_task reads
};

static struct Config g_config;

/* Same defaults monitor.c used to hardcode before Configuration
 * existed. */
static const config_limits_t DEFAULT_LIMITS = {
    .temp_normal_min = 15,       .temp_normal_max = 27,
    .temp_warning_min = 10,      .temp_warning_max = 30,
    .humidity_normal_min = 30,   .humidity_warning_min = 20,
    .light_normal_min = 40,      .light_warning_min = 20,
    .battery_normal_min = 40,    .battery_warning_min = 20,
};

/* ===============================================================
 * Flash I/O -- erase + double-word program, proven against real
 * hardware before this module was built on top of it. Bank/page are
 * computed above; Flash is memory-mapped for reads, so loading never
 * needs a HAL call at all.
 * =============================================================== */

/* Programs `len` bytes at `addr`, 8 bytes (one double-word) at a
 * time -- flash only accepts double-word writes. Returns false (and
 * stops) on the first program failure. Caller must have already
 * erased the page and called HAL_FLASH_Unlock(). */
static bool flash_write_bytes(uint32_t addr, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i += 8) {
        union {
            uint64_t dword; //64 bit
            uint8_t bytes[8];
        } u;

        u.dword = 0xFFFFFFFFFFFFFFFFu;
        for (size_t j = 0; j < 8 && (i + j) < len; j++) {
            u.bytes[j] = data[i + j];
        }

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, u.dword) != HAL_OK) {
            return false;
        }
        addr += 8;
    }
    return true;
}

static bool config_flash_save(const config_limits_t *limits)
{
    config_flash_data_t data;
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0;
    bool ok;

    data.magic = CONFIG_MAGIC;
    data.limits = *limits;

    HAL_FLASH_Unlock();

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Banks = CONFIG_FLASH_BANK;
    erase.Page = CONFIG_FLASH_PAGE;
    erase.NbPages = 1;

    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK) {
        HAL_FLASH_Lock();
        return false;
    }

    ok = flash_write_bytes(CONFIG_FLASH_ADDR, (const uint8_t *)&data, sizeof(data));

    HAL_FLASH_Lock();
    return ok;
}

static void config_flash_load(config_limits_t *out_limits)
{
    const config_flash_data_t *stored = (const config_flash_data_t *)CONFIG_FLASH_ADDR;

    if (stored->magic == CONFIG_MAGIC) {
        *out_limits = stored->limits;
        return;
    }

    /* Empty/first-boot Flash (erased == all 0xFF, magic won't match)
     * -- section 2.6: load defaults, and write them to Flash. */
    *out_limits = DEFAULT_LIMITS;
    (void)config_flash_save(&DEFAULT_LIMITS);
}

/* ===============================================================
 * Dispatch: Communication -> Configuration
 *
 * Two wire payload shapes -- provisional, same as event.c's/init.c's
 * payload structs: nothing on the Central Computer side parses these
 * yet. Temperature needs a min+max pair; humidity/light/battery are
 * lower-bound-only (section 2.5), matching monitor.c's existing
 * classify_lower_bound()/classify_range() split.
 * =============================================================== */

typedef struct __attribute__((packed)) {
    int8_t min;
    int8_t max;
} temp_range_payload_t;

typedef struct __attribute__((packed)) {
    uint8_t min;
} bound_payload_t;

/* Reply to TLV_TAG_GET_CONFIG -- field-for-field copy of
 * config_limits_t, but explicitly packed for the wire (config_limits_t
 * itself isn't) and duplicated here rather than reused, matching this
 * codebase's convention for wire payloads (see CLAUDE.md's
 * query_range_payload_t note). */
typedef struct __attribute__((packed)) {
    int16_t temp_normal_min;
    int16_t temp_normal_max;
    int16_t temp_warning_min;
    int16_t temp_warning_max;
    uint8_t humidity_normal_min;
    uint8_t humidity_warning_min;
    uint8_t light_normal_min;
    uint8_t light_warning_min;
    uint8_t battery_normal_min;
    uint8_t battery_warning_min;
} config_reply_payload_t;

/* Every SET_* command ends the same way: persist the whole limits
 * struct (Flash can't rewrite just the one changed field -- see the
 * Flash I/O section above) and tell Event (section 2.3: events file
 * only, no message to the Central Computer). */
static void commit(const char *description)
{
    (void)config_flash_save(&g_config.limits);
    event_config_changed(description); //send an event
}

void configuration_on_frame(const tlv_frame_t *f)
{
    if (f == NULL) {
        return;
    }

    /* GET_CONFIG carries no payload (f->value is NULL for it, same as
     * any zero-length frame) -- checked per-case below instead of one
     * blanket guard up front, so GET_CONFIG isn't rejected before it
     * gets a chance to run. */

//    /* Held across the whole switch, including commit()'s Flash/SD I/O:
//     * simplest correct option, and cheap in practice -- SET_*/GET_CONFIG
//     * are rare, operator-triggered commands, not a periodic path, so a
//     * Monitor round occasionally waiting a few ms on this is fine. See
//     * config.h's comment on config_get_limits() for why this exists at
//     * all: without it, monitor_task() could read a torn mix of old and
//     * new fields while a SET_* is applying. */
    osMutexAcquire(g_config.lock, osWaitForever);

    switch (f->tag) {
    case TLV_TAG_SET_TEMP_NORMAL:
        if (f->value != NULL && f->len == sizeof(temp_range_payload_t)) {
            const temp_range_payload_t *p = (const temp_range_payload_t *)f->value;
            g_config.limits.temp_normal_min = p->min;
            g_config.limits.temp_normal_max = p->max;
            commit("temp normal range");
        }
        break;

    case TLV_TAG_SET_TEMP_WARNING:
        if (f->value != NULL && f->len == sizeof(temp_range_payload_t)) {
            const temp_range_payload_t *p = (const temp_range_payload_t *)f->value;
            g_config.limits.temp_warning_min = p->min;
            g_config.limits.temp_warning_max = p->max;
            commit("temp warning range");
        }
        break;

    case TLV_TAG_SET_HUM_NORMAL:
        if (f->value != NULL && f->len == sizeof(bound_payload_t)) {
            g_config.limits.humidity_normal_min = ((const bound_payload_t *)f->value)->min;
            commit("humidity normal bound");
        }
        break;

    case TLV_TAG_SET_HUM_WARNING:
        if (f->value != NULL && f->len == sizeof(bound_payload_t)) {
            g_config.limits.humidity_warning_min = ((const bound_payload_t *)f->value)->min;
            commit("humidity warning bound");
        }
        break;

    case TLV_TAG_SET_LIGHT_NORMAL:
        if (f->value != NULL && f->len == sizeof(bound_payload_t)) {
            g_config.limits.light_normal_min = ((const bound_payload_t *)f->value)->min;
            commit("light normal bound");
        }
        break;

    case TLV_TAG_SET_LIGHT_WARNING:
        if (f->value != NULL && f->len == sizeof(bound_payload_t)) {
            g_config.limits.light_warning_min = ((const bound_payload_t *)f->value)->min;
            commit("light warning bound");
        }
        break;

    case TLV_TAG_SET_BATT_NORMAL:
        if (f->value != NULL && f->len == sizeof(bound_payload_t)) {
            g_config.limits.battery_normal_min = ((const bound_payload_t *)f->value)->min;
            commit("battery normal bound");
        }
        break;

    case TLV_TAG_SET_BATT_WARNING:
        if (f->value != NULL && f->len == sizeof(bound_payload_t)) {
            g_config.limits.battery_warning_min = ((const bound_payload_t *)f->value)->min;
            commit("battery warning bound");
        }
        break;

    case TLV_TAG_GET_CONFIG: {
        config_reply_payload_t reply;
        reply.temp_normal_min = g_config.limits.temp_normal_min;
        reply.temp_normal_max = g_config.limits.temp_normal_max;
        reply.temp_warning_min = g_config.limits.temp_warning_min;
        reply.temp_warning_max = g_config.limits.temp_warning_max;
        reply.humidity_normal_min = g_config.limits.humidity_normal_min;
        reply.humidity_warning_min = g_config.limits.humidity_warning_min;
        reply.light_normal_min = g_config.limits.light_normal_min;
        reply.light_warning_min = g_config.limits.light_warning_min;
        reply.battery_normal_min = g_config.limits.battery_normal_min;
        reply.battery_warning_min = g_config.limits.battery_warning_min;
        (void)comm_send(g_config.comm, TLV_TAG_CONFIG_REPLY,
                         (const uint8_t *)&reply, sizeof(reply));
        break;
    }

    default:
        break;
    }

    osMutexRelease(g_config.lock);
}

/* ===============================================================
 * Public API: create / destroy / query
 * =============================================================== */

Config *config_create(Communication *comm)
{
    g_config.comm = comm;
    g_config.lock = osMutexNew(NULL); /* == FreeRTOS xSemaphoreCreateMutex(). Created before osKernelStart(), same as Communication's and SDFatFS's own RTOS objects -- safe pre-scheduler, once osKernelInitialize() has run. */
    config_flash_load(&g_config.limits);
    return &g_config;
}

void config_destroy(Config *c)
{
    (void)c;
}

config_limits_t config_get_limits(void)
{
    config_limits_t copy;
    osMutexAcquire(g_config.lock, osWaitForever);
    copy = g_config.limits;
    osMutexRelease(g_config.lock);
    return copy;
}
