/*
 * central_computer.cpp - see central_computer.h for the class-level
 * overview.
 */
#include "central_computer.h"

#include "tcp_transport.h"
#include "tlv.h"

#include <cstdio>
#include <stdexcept>

namespace
{

/* Request payload for GS_QUERY_DATA/GS_QUERY_EVENTS -- same 12-byte
 * shape as management_command.h's/gs_link_server.cpp's own copies.
 * Duplicated here rather than shared, matching this codebase's
 * established convention for this exact wire shape (see CLAUDE.md). */
struct __attribute__((packed)) GsQueryRange
{
    uint8_t start_year, start_month, start_date, start_hour, start_min, start_sec;
    uint8_t end_year, end_month, end_date, end_hour, end_min, end_sec;
};

/* How long to keep polling for a reply before giving up -- same
 * "bounded wait for something that might never answer" problem
 * ManagementCommand::get_config() already solves on the CC side (see
 * its header comment), just via a poll-count cap here instead of a
 * condition_variable, since this runs synchronously on the menu's own
 * thread rather than being woken by a background RX thread. Each
 * TcpTransport::read() call times out after ~100ms on its own (see
 * tcp_transport.cpp), so this is roughly a 5s ceiling. */
constexpr int kMaxPollIterations = 50;

/* Parses "YYYY-MM-DD HH:MM:SS" (TimeUtils::format()'s own shape, the
 * canonical timestamp string this whole system uses) into the packed
 * per-field half GS_QUERY_DATA/GS_QUERY_EVENTS needs. Returns false on
 * a malformed string -- the caller turns that into a one-line result
 * rather than throwing. */
bool parse_half(const std::string &s, uint8_t &year, uint8_t &month, uint8_t &date, uint8_t &hour,
                 uint8_t &min, uint8_t &sec)
{
    int y, mo, d, h, mi, se;
    if (std::sscanf(s.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6) {
        return false;
    }
    year = static_cast<uint8_t>(y - 2000);
    month = static_cast<uint8_t>(mo);
    date = static_cast<uint8_t>(d);
    hour = static_cast<uint8_t>(h);
    min = static_cast<uint8_t>(mi);
    sec = static_cast<uint8_t>(se);
    return true;
}

} // namespace

CentralComputer::CentralComputer(std::string host, uint16_t port) : host_(std::move(host)), port_(port) {}

std::vector<std::string> CentralComputer::query(uint8_t tag, const std::string &start,
                                                  const std::string &end) const
{
    GsQueryRange range{};
    if (!parse_half(start, range.start_year, range.start_month, range.start_date, range.start_hour,
                     range.start_min, range.start_sec) ||
        !parse_half(end, range.end_year, range.end_month, range.end_date, range.end_hour, range.end_min,
                    range.end_sec)) {
        return {"(malformed date/time -- expected YYYY-MM-DD HH:MM:SS)"};
    }

    uint8_t frame[TLV_MAX_FRAME];
    size_t frame_len = 0;
    if (tlv_encode(tag, reinterpret_cast<const uint8_t *>(&range), sizeof(range), frame, sizeof(frame),
                   &frame_len) != TLV_OK) {
        return {"(internal error: failed to encode query)"};
    }

    std::vector<std::string> lines;
    try {
        TcpTransport transport(host_, port_);
        transport.write(frame, frame_len);

        tlv_receiver_t recv_state;
        tlv_receiver_init(&recv_state);
        bool done = false;
        int iterations = 0;
        while (!done) {
            if (++iterations > kMaxPollIterations) {
                lines.push_back("(no reply -- central computer unreachable or unresponsive)");
                return lines;
            }

            uint8_t buf[256];
            long n = transport.read(buf, sizeof(buf));
            for (long i = 0; i < n && !done; i++) {
                tlv_frame_t recv_frame;
                if (tlv_receiver_feed_byte(&recv_state, buf[static_cast<size_t>(i)], &recv_frame) != TLV_OK) {
                    continue; /* TLV_INCOMPLETE: keep going. TLV_ERR_CRC: bad frame, already dropped. */
                }
                if (recv_frame.tag == TLV_TAG_GS_RECORD) {
                    lines.emplace_back(reinterpret_cast<const char *>(recv_frame.value), recv_frame.len);
                } else if (recv_frame.tag == TLV_TAG_GS_END) {
                    done = true;
                }
            }
        }
    } catch (const std::exception &e) {
        if (lines.empty()) {
            return {std::string("(connection failed: ") + e.what() + ")"};
        }
        lines.push_back("(connection lost -- results may be incomplete)");
        return lines;
    }

    if (lines.empty()) {
        lines.push_back("(no records in range)");
    }
    return lines;
}

std::vector<std::string> CentralComputer::queryLogs(const std::string &start, const std::string &end) const
{
    return query(TLV_TAG_GS_QUERY_DATA, start, end);
}

std::vector<std::string> CentralComputer::queryEvents(const std::string &start, const std::string &end) const
{
    return query(TLV_TAG_GS_QUERY_EVENTS, start, end);
}
