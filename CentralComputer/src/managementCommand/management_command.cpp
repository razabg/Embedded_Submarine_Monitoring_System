/*
 * management_command.cpp - see management_command.h for the overview.
 */
#include "management_command.h"

#include <string>

#include "time_utils.h"

ManagementCommand::ManagementCommand(Communication &comm, Log &log) : comm_(comm), log_(log)
{
    comm_.set_management_handler([this](const tlv_frame_t &f) { on_frame(f); });
}

bool ManagementCommand::send_temp_range(uint8_t tag, const char *label, int8_t min, int8_t max)
{
    if (min > max) {
        log_.write(std::string("rejected ") + label + ": min=" + std::to_string(min) +
                   " > max=" + std::to_string(max));
        return false;
    }
    temp_range_payload_t p{min, max};
    comm_.send(tag, reinterpret_cast<const uint8_t *>(&p), sizeof(p));
    log_.write(std::string("sent ") + label + " min=" + std::to_string(min) +
               " max=" + std::to_string(max));
    return true;
}

bool ManagementCommand::send_bound(uint8_t tag, const char *label, uint8_t min)
{
    if (min > 100) {
        log_.write(std::string("rejected ") + label + ": min=" + std::to_string(min) + " > 100");
        return false;
    }
    bound_payload_t p{min};
    comm_.send(tag, reinterpret_cast<const uint8_t *>(&p), sizeof(p));
    log_.write(std::string("sent ") + label + " min=" + std::to_string(min));
    return true;
}

bool ManagementCommand::set_temp_normal(int8_t min, int8_t max)
{
    return send_temp_range(TLV_TAG_SET_TEMP_NORMAL, "SET_TEMP_NORMAL", min, max);
}

bool ManagementCommand::set_temp_warning(int8_t min, int8_t max)
{
    return send_temp_range(TLV_TAG_SET_TEMP_WARNING, "SET_TEMP_WARNING", min, max);
}

bool ManagementCommand::set_humidity_normal(uint8_t min)
{
    return send_bound(TLV_TAG_SET_HUM_NORMAL, "SET_HUM_NORMAL", min);
}

bool ManagementCommand::set_humidity_warning(uint8_t min)
{
    return send_bound(TLV_TAG_SET_HUM_WARNING, "SET_HUM_WARNING", min);
}

bool ManagementCommand::set_light_normal(uint8_t min)
{
    return send_bound(TLV_TAG_SET_LIGHT_NORMAL, "SET_LIGHT_NORMAL", min);
}

bool ManagementCommand::set_light_warning(uint8_t min)
{
    return send_bound(TLV_TAG_SET_LIGHT_WARNING, "SET_LIGHT_WARNING", min);
}

bool ManagementCommand::set_battery_normal(uint8_t min)
{
    return send_bound(TLV_TAG_SET_BATT_NORMAL, "SET_BATT_NORMAL", min);
}

bool ManagementCommand::set_battery_warning(uint8_t min)
{
    return send_bound(TLV_TAG_SET_BATT_WARNING, "SET_BATT_WARNING", min);
}

void ManagementCommand::get_time()
{
    comm_.send(TLV_TAG_GET_TIME, nullptr, 0);
    log_.write("sent GET_TIME");
}

bool ManagementCommand::get_config(int timeout_ms)
{
    std::unique_lock<std::mutex> lock(config_reply_mutex_);
    config_reply_pending_ = true;
    lock.unlock();

    comm_.send(TLV_TAG_GET_CONFIG, nullptr, 0);
    log_.write("sent GET_CONFIG");

    lock.lock();
    return config_reply_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                      [this] { return !config_reply_pending_; });
}

void ManagementCommand::query_data(const query_range_payload_t &range)
{
    comm_.send(TLV_TAG_QUERY_DATA, reinterpret_cast<const uint8_t *>(&range), sizeof(range));
    log_.write("sent QUERY_DATA to LNC");
}

void ManagementCommand::query_events(const query_range_payload_t &range)
{
    comm_.send(TLV_TAG_QUERY_EVENTS, reinterpret_cast<const uint8_t *>(&range), sizeof(range));
    log_.write("sent QUERY_EVENTS to LNC");
}

void ManagementCommand::on_frame(const tlv_frame_t &frame)
{
    /* log_.write() already both prints (to stdout) and persists (to
     * the rotating log file) -- see Log::write() -- so it replaces
     * what used to be a plain std::printf() here, giving every reply
     * the same permanent-record treatment as the commands this class
     * sends, with no separate console-only path left ungoverned. */
    switch (frame.tag) {
    case TLV_TAG_TIME_REPLY: {
        if (frame.value == nullptr || frame.len != sizeof(time_payload_t)) {
            return;
        }
        const auto *p = reinterpret_cast<const time_payload_t *>(frame.value);
        log_.write("LNC reports time " +
                   TimeUtils::format(p->year, p->month, p->date, p->hour, p->min, p->sec));
        break;
    }

    case TLV_TAG_CONFIG_REPLY: {
        if (frame.value == nullptr || frame.len != sizeof(config_reply_payload_t)) {
            return;
        }
        const auto *p = reinterpret_cast<const config_reply_payload_t *>(frame.value);
        thresholds_.tempNormalMin = p->temp_normal_min;
        thresholds_.tempNormalMax = p->temp_normal_max;
        thresholds_.tempWarningMin = p->temp_warning_min;
        thresholds_.tempWarningMax = p->temp_warning_max;
        thresholds_.humidityNormalMin = p->humidity_normal_min;
        thresholds_.humidityWarningMin = p->humidity_warning_min;
        thresholds_.lightNormalMin = p->light_normal_min;
        thresholds_.lightWarningMin = p->light_warning_min;
        thresholds_.batteryNormalMin = p->battery_normal_min;
        thresholds_.batteryWarningMin = p->battery_warning_min;
        log_.write("LNC reports current config (CONFIG_REPLY)");

        /* Wakes get_config()'s wait, which runs on a different
         * thread (the caller's, not this rx_thread_) -- see this
         * class's header comment on get_config(). */
        {
            std::lock_guard<std::mutex> lock(config_reply_mutex_);
            config_reply_pending_ = false;
        }
        config_reply_cv_.notify_one();
        break;
    }

    case TLV_TAG_ACK:
        log_.write("received ACK");
        break;

    case TLV_TAG_NACK:
        log_.write("received NACK");
        break;

    case TLV_TAG_TIME_SYNC_REQUEST: {
        /* The LNC asked at boot for the real time -- this is the reply
         * that was missing (CLAUDE.md's known gap). Sent from the CC's
         * own current time, same fields/units the LNC's time_payload_t
         * already uses for TLV_TAG_TIME_REPLY. */
        TimeUtils::CalendarFields f = TimeUtils::now_fields();
        time_payload_t reply{f.year, f.month, f.date, f.hour, f.min, f.sec, f.dow};
        comm_.send(TLV_TAG_TIME_SYNC_REPLY, reinterpret_cast<const uint8_t *>(&reply), sizeof(reply));
        log_.write("LNC requested time sync at boot -- replied with " + TimeUtils::now());
        break;
    }

    default:
        break;
    }
}
