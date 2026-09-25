/*
 * config.h - LNC Configuration module
 *
 * Section 2.6: owns the ten threshold values behind Monitor's
 * classify_mode() (section 2.5's eight SET_* commands -- temperature
 * needs a min+max per tier, humidity/light/battery are lower-bound
 * only), persists them in Flash, loads them at boot (defaults on an
 * empty/first-boot Flash), and produces an Event on every change
 * (section 2.3 -- events file only, no message to the Central
 * Computer).
 *
 * ADT, matching monitor.c's/event.c's/log.c's/init.c's shape: opaque
 * handle, one static instance, create()/destroy().
 *
 * Also answers TLV_TAG_GET_CONFIG (CC asking for the current
 * thresholds) with TLV_TAG_CONFIG_REPLY, so needs a Communication* to
 * reply through -- same reason log_create()/event_create() take one.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include "communication.h"
#include "tlv.h"

typedef struct Config Config;
/* Opaque handle -- fields live only in config.c. */

/* Monitor reads this every round instead of hardcoded #defines. */
typedef struct {
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
} config_limits_t;

/* Call once from main(). Loads from Flash, or defaults (and writes
 * them to Flash) on an empty/first-boot Flash page. Returns the
 * handle -- never NULL in practice, kept for ADT consistency. */
Config *config_create(Communication *comm);

/* Provided for ADT completeness; not expected to be called in practice
 * on real hardware. Null-safe. */
void config_destroy(Config *c);

///* Current limits, as a copy -- not a pointer to the live struct.
// * configuration_on_frame() (SET_*/GET_CONFIG) runs on comm_rx_task and
//// * mutates the real values field-by-field; Monitor reads them every 5 s
//// * from its own task. A pointer into live state could hand Monitor a
//// * torn mix of old and new fields if a SET_* lands mid-read; copying
//// * the whole struct out under the internal lock avoids that (same
//// * fix, same reason, as monitor.c's own monitor_get_latest()). */
config_limits_t config_get_limits(void);

/* Communication -> Configuration: one of the eight SET_* threshold
 * commands (section 2.5), or SET_TIME (handled the same way Init's
 * own CC-sourced time correction is -- see config.c). Signature
 * matches the weak stub already declared in communication.c exactly,
 * so this strong definition overrides it at link time. */
void configuration_on_frame(const tlv_frame_t *f);

#endif /* CONFIG_H */
