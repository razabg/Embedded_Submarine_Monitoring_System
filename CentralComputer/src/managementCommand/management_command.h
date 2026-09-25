/*
 * management_command.h - Central Computer Management Command module.
 * Sends the eight SET_* threshold commands + GET_TIME (section 2.5),
 * and handles their replies (ACK/NACK/TIME_REPLY) plus the LNC's own
 * TLV_TAG_TIME_SYNC_REQUEST, which it answers with TLV_TAG_TIME_SYNC_REPLY
 * -- see on_frame()'s TLV_TAG_TIME_SYNC_REQUEST case in the .cpp for
 * the actual comm_.send() call that does this (fixes CLAUDE.md's known
 * "route_frame() silently drops TIME_SYNC_REQUEST" gap).
 *
 * Needs Communication& to both send and receive, so its constructor
 * self-registers on_frame() as the management handler.
 *
 * ACK/NACK: the LNC's config.c doesn't send either yet -- handled here
 * anyway, ready for when it does. send() itself is fire-and-forget.
 *
 * Also takes a Log& -- every command actually sent, and every reply
 * received (including the automatic TIME_SYNC_REQUEST fix above),
 * gets written there. Logging lives here rather than in each caller
 * (e.g. main.cpp's menu) so nothing that goes through this class can
 * be sent without leaving a permanent record, regardless of who calls it.
 *
 * Also sends TLV_TAG_GET_CONFIG and tracks the LNC's answer
 * (TLV_TAG_CONFIG_REPLY) in thresholds() -- the LNC's actual current
 * limits, confirmed by it, not just "what we last told it". Empty
 * until the first reply arrives; main.cpp's menu displays whatever is
 * cached here, refreshed by calling get_config() again.
 *
 * get_config() blocks (bounded by a timeout) until that reply actually
 * lands, unlike every other send in this class -- it runs on the
 * caller's (main.cpp menu) thread, but the reply arrives on
 * Communication's rx_thread_. Without waiting, thresholds() could
 * still hold the previous value the instant get_config() returns, so
 * a caller that immediately displays thresholds() (main.cpp's menu
 * header, redrawn right after this call) would show stale data one
 * redraw longer than expected -- confirmed on hardware: the header
 * needed a second manual refresh before it showed a just-changed
 * value. A std::condition_variable is the fix: on_frame()'s
 * TLV_TAG_CONFIG_REPLY case notifies it after updating thresholds_,
 * and get_config() waits on it (with a timeout, in case the LNC never
 * answers) instead of returning the moment the request is sent.
 *
 * Also sends TLV_TAG_QUERY_DATA/QUERY_EVENTS (query_data()/
 * query_events()) -- asks the LNC to search its own SD card and
 * stream back matching stored lines, same as comm_test.cpp's manual
 * test tool. Unlike every other reply in this class, the replies
 * (TLV_TAG_QUERY_RECORD/QUERY_END) do NOT come back through on_frame()
 * here -- Communication::route_frame() sends those to the log handler
 * instead (see communication.cpp), so they show up wherever that's
 * wired (main.cpp's live log window), same as any other frame the LNC
 * sends unprompted.
 */
#ifndef MANAGEMENT_COMMAND_H
#define MANAGEMENT_COMMAND_H

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>

#include "communication.h"
#include "log.h"
#include "tlv.h"

/* Wire formats, duplicated from the LNC's own (config.c, init.c) --
 * same convention as data_collection_analysis.h. Declared here, not
 * private to the .cpp, so a future test can reuse the exact types. */

struct __attribute__((packed)) temp_range_payload_t {
    int8_t min;
    int8_t max;
};

struct __attribute__((packed)) bound_payload_t {
    uint8_t min;
};

/* Same shape both ways: LNC's TIME_REPLY and our TIME_SYNC_REPLY. */
struct __attribute__((packed)) time_payload_t {
    uint8_t year; /* 0-99, offset from 2000 */
    uint8_t month;
    uint8_t date;
    uint8_t hour;
    uint8_t min;
    uint8_t sec;
    uint8_t dow; /* 1-7, Monday=1..Sunday=7 */
};

/* Reply to TLV_TAG_GET_CONFIG -- matches config.c's config_reply_payload_t
 * field-for-field (duplicated here, same convention as every other
 * payload struct in this file). */
struct __attribute__((packed)) config_reply_payload_t {
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
};

/* Request for TLV_TAG_QUERY_DATA/QUERY_EVENTS -- matches log.c's/
 * event.c's query_range_payload_t exactly (12 bytes: start then end,
 * each year/month/date/hour/min/sec; year is an offset from 2000),
 * same convention as comm_test.cpp's own copy. */
struct __attribute__((packed)) query_range_payload_t {
    uint8_t start_year, start_month, start_date, start_hour, start_min, start_sec;
    uint8_t end_year, end_month, end_date, end_hour, end_min, end_sec;
};

class ManagementCommand
{
public:
    /* The LNC's last-confirmed thresholds -- populated only by an
     * actual TLV_TAG_CONFIG_REPLY, never guessed from what we sent.
     * Each field is empty until the first reply arrives. */
    struct Thresholds {
        std::optional<int8_t> tempNormalMin, tempNormalMax;
        std::optional<int8_t> tempWarningMin, tempWarningMax;
        std::optional<uint8_t> humidityNormalMin, humidityWarningMin;
        std::optional<uint8_t> lightNormalMin, lightWarningMin;
        std::optional<uint8_t> batteryNormalMin, batteryWarningMin;
    };

    /* Both held by reference, not stored -- caller keeps them alive,
     * same as Communication's own Transport&. */
    ManagementCommand(Communication &comm, Log &log);

    ManagementCommand(const ManagementCommand &) = delete;
    ManagementCommand &operator=(const ManagementCommand &) = delete;

    /* false = failed a sanity check (min<=max; 0-100 bounds), nothing
     * sent. true = handed to Communication::send() -- not a delivery
     * confirmation. */
    bool set_temp_normal(int8_t min, int8_t max);
    bool set_temp_warning(int8_t min, int8_t max);
    bool set_humidity_normal(uint8_t min);
    bool set_humidity_warning(uint8_t min);
    bool set_light_normal(uint8_t min);
    bool set_light_warning(uint8_t min);
    bool set_battery_normal(uint8_t min);
    bool set_battery_warning(uint8_t min);

    /* Sends TLV_TAG_GET_TIME; reply arrives later via on_frame(). */
    void get_time();

    /* Sends TLV_TAG_GET_CONFIG and blocks until the reply updates
     * thresholds() or timeout_ms passes. Returns false on timeout
     * (thresholds() left however it was); true means thresholds()
     * reflects this call's reply by the time it returns. */
    bool get_config(int timeout_ms = 1000);

    /* The LNC's thresholds as of the last CONFIG_REPLY (or all-empty
     * if none has arrived yet this session). */
    const Thresholds &thresholds() const { return thresholds_; }

    /* Sends TLV_TAG_QUERY_DATA/QUERY_EVENTS -- asks the LNC to search
     * its own SD card over [range] and stream back matching stored
     * lines. See this header's class-level comment for where the
     * replies actually go (not on_frame() here). */
    void query_data(const query_range_payload_t &range);
    void query_events(const query_range_payload_t &range);

private:
    Communication &comm_;
    Log &log_;
    Thresholds thresholds_;

    /* Rendezvous for get_config()'s wait -- notified from on_frame()'s
     * TLV_TAG_CONFIG_REPLY case, which runs on Communication's
     * rx_thread_, not the caller's thread. */
    std::mutex config_reply_mutex_;
    std::condition_variable config_reply_cv_;
    bool config_reply_pending_ = false;

    bool send_temp_range(uint8_t tag, const char *label, int8_t min, int8_t max);
    bool send_bound(uint8_t tag, const char *label, uint8_t min);

    /* Registered as Communication's management handler. */
    void on_frame(const tlv_frame_t &frame);
};

#endif /* MANAGEMENT_COMMAND_H */
