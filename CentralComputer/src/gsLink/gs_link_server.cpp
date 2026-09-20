/*
 * gs_link_server.cpp - see gs_link_server.h for the class-level overview.
 */
#include "gs_link_server.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <system_error>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "time_utils.h"
#include "tlv.h"

namespace
{

/* Matches SerialPort's/TcpTransport's/gateway's own ~100ms client
 * timeout. The accept-loop's own listening-socket timeout (200ms) is
 * separate and a bit longer -- it only needs to be frequent enough to
 * notice stop_ promptly, not to bound per-byte latency. */
constexpr int kAcceptTimeoutSec = 0;
constexpr int kAcceptTimeoutUsec = 200000;
constexpr int kClientTimeoutSec = 0;
constexpr int kClientTimeoutUsec = 100000;

/* Request payload for GS_QUERY_DATA/GS_QUERY_EVENTS -- same 12-byte
 * shape as management_command.h's query_range_payload_t. Duplicated
 * here rather than shared, matching this codebase's established
 * convention for this exact wire shape (already duplicated between
 * the LNC's log.c/event.c, and again in management_command.h and
 * comm_test.cpp on the CC side -- see CLAUDE.md). */
struct __attribute__((packed)) GsQueryRange
{
    uint8_t start_year, start_month, start_date, start_hour, start_min, start_sec;
    uint8_t end_year, end_month, end_date, end_hour, end_min, end_sec;
};

/* Same small local mapping DataCollectionAnalysis::on_frame() already
 * has (data_collection_analysis.cpp) -- not exposed via that header,
 * so duplicated here rather than plumbed through just for this. */
const char *mode_name(int mode)
{
    switch (mode) {
    case 0: return "Normal";
    case 1: return "Warning";
    case 2: return "Error";
    default: return "Unknown";
    }
}

/* Encodes and sends one TLV frame over `fd`. Throws std::runtime_error
 * on a write failure -- handle_client()'s caller (accept_loop()) just
 * logs and moves on to the next connection, same as any other
 * client-gone scenario. */
void send_frame(int fd, uint8_t tag, const uint8_t *value, uint8_t value_len)
{
    uint8_t frame[TLV_MAX_FRAME];
    size_t frame_len = 0;

    if (tlv_encode(tag, value, value_len, frame, sizeof(frame), &frame_len) != TLV_OK) {
        throw std::runtime_error("gs_link: tlv_encode failed");
    }

    size_t sent = 0;
    while (sent < frame_len) {
        ssize_t n = ::send(fd, frame + sent, frame_len - sent, 0);
        if (n <= 0) {
            throw std::runtime_error("gs_link: send failed (client gone)");
        }
        sent += static_cast<size_t>(n);
    }
}

} // namespace

GsLinkServer::GsLinkServer(DataCollectionAnalysis &dca, uint16_t port) : dca_(dca)
{
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        throw std::system_error(errno, std::generic_category(), "GsLinkServer: socket");
    }

    int reuse = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    /* Lets accept_loop() wake up periodically to check stop_, instead
     * of blocking in accept() forever -- same SO_RCVTIMEO technique
     * this codebase already uses for every other blocking socket call
     * (SerialPort's VMIN/VTIME, TcpTransport, gateway.cpp), just
     * applied to the listening socket itself. */
    struct timeval accept_tv{};
    accept_tv.tv_sec = kAcceptTimeoutSec;
    accept_tv.tv_usec = kAcceptTimeoutUsec;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_RCVTIMEO, &accept_tv, sizeof(accept_tv));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    if (::bind(listen_fd_, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) != 0) {
        int saved_errno = errno;
        ::close(listen_fd_);
        throw std::system_error(saved_errno, std::generic_category(), "GsLinkServer: bind");
    }

    if (::listen(listen_fd_, 1) != 0) {
        int saved_errno = errno;
        ::close(listen_fd_);
        throw std::system_error(saved_errno, std::generic_category(), "GsLinkServer: listen");
    }

    std::printf("gs_link: listening on 127.0.0.1:%u\n", port);
    std::fflush(stdout);

    accept_thread_ = std::thread(&GsLinkServer::accept_loop, this);
}

GsLinkServer::~GsLinkServer()
{
    stop_ = true;
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
    }
}

void GsLinkServer::accept_loop()
{
    while (!stop_.load()) {
        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        int client_fd = ::accept(listen_fd_, reinterpret_cast<struct sockaddr *>(&client_addr), &client_len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue; /* accept-timeout elapsed, nothing arrived -- normal, re-check stop_ */
            }
            if (errno == EINTR) {
                continue;
            }
            std::fprintf(stderr, "gs_link: accept() failed: %s\n", std::strerror(errno));
            continue;
        }

        struct timeval client_tv{};
        client_tv.tv_sec = kClientTimeoutSec;
        client_tv.tv_usec = kClientTimeoutUsec;
        ::setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &client_tv, sizeof(client_tv));

        std::printf("gs_link: client connected from %s:%u\n", inet_ntoa(client_addr.sin_addr),
                    ntohs(client_addr.sin_port));
        std::fflush(stdout);

        try {
            handle_client(client_fd);
        } catch (const std::exception &e) {
            std::fprintf(stderr, "gs_link: session ended: %s\n", e.what());
        }

        ::close(client_fd);
    }
}

void GsLinkServer::handle_client(int client_fd)
{
    tlv_receiver_t recv_state;
    tlv_receiver_init(&recv_state);

    /* Reads until exactly one complete request frame arrives, the
     * client disconnects, or stop_ is set -- one connection answers
     * one query (see this class's header comment). */
    for (;;) {
        if (stop_.load()) {
            return;
        }

        uint8_t buf[256];
        ssize_t n = ::recv(client_fd, buf, sizeof(buf), 0);
        if (n == 0) {
            return; /* client disconnected before sending a full request */
        }
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            }
            throw std::system_error(errno, std::generic_category(), "gs_link: recv");
        }

        for (ssize_t i = 0; i < n; i++) {
            tlv_frame_t frame;
            tlv_status_t st = tlv_receiver_feed_byte(&recv_state, buf[static_cast<size_t>(i)], &frame);
            if (st != TLV_OK) {
                continue; /* TLV_INCOMPLETE: keep going. TLV_ERR_CRC: bad frame, already dropped. */
            }

            if (frame.tag != TLV_TAG_GS_QUERY_DATA && frame.tag != TLV_TAG_GS_QUERY_EVENTS) {
                continue; /* not a query we answer -- ignore, keep waiting */
            }
            if (frame.value == nullptr || frame.len != sizeof(GsQueryRange)) {
                return; /* malformed request -- nothing sensible to answer */
            }

            const auto *range = reinterpret_cast<const GsQueryRange *>(frame.value);
            std::string start = TimeUtils::format(range->start_year, range->start_month, range->start_date,
                                                   range->start_hour, range->start_min, range->start_sec);
            std::string end = TimeUtils::format(range->end_year, range->end_month, range->end_date,
                                                 range->end_hour, range->end_min, range->end_sec);

            if (frame.tag == TLV_TAG_GS_QUERY_DATA) {
                for (const auto &m : dca_.query_measurements(start, end)) {
                    char line[128];
                    std::snprintf(line, sizeof(line),
                                  "[%s] temp=%dC hum=%d%% light=%d%% batt=%d%% mode=%s", m.timestamp.c_str(),
                                  m.temp_c, m.humidity_pct, m.light_pct, m.battery_pct, mode_name(m.mode));
                    send_frame(client_fd, TLV_TAG_GS_RECORD, reinterpret_cast<const uint8_t *>(line),
                               static_cast<uint8_t>(std::strlen(line)));
                }
            } else {
                for (const auto &ev : dca_.query_events(start, end)) {
                    char line[128];
                    std::snprintf(line, sizeof(line), "[%s] %s: %s", ev.timestamp.c_str(), ev.type.c_str(),
                                  ev.description.c_str());
                    send_frame(client_fd, TLV_TAG_GS_RECORD, reinterpret_cast<const uint8_t *>(line),
                               static_cast<uint8_t>(std::strlen(line)));
                }
            }

            send_frame(client_fd, TLV_TAG_GS_END, nullptr, 0);
            return; /* one query answered -- session done, back to accept() */
        }
    }
}
