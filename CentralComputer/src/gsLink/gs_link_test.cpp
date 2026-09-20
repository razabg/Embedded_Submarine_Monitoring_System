/*
 * gs_link_test.cpp - manual smoke test for GsLinkServer. Not a
 * permanent part of the build -- seeds a DataCollectionAnalysis
 * directly (no LNC/Communication needed at all, since GsLinkServer
 * only ever touches DataCollectionAnalysis), starts a real
 * GsLinkServer, then acts as a real TCP client against it (the same
 * role GroundStation will play) to prove the whole round trip: a
 * hand-encoded GS_QUERY_DATA/GS_QUERY_EVENTS frame in, GS_RECORD...
 * GS_END back out, matching what was seeded.
 */
#include "data_collection_analysis.h"
#include "gs_link_server.h"
#include "tlv.h"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <system_error>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{

struct __attribute__((packed)) GsQueryRange
{
    uint8_t start_year, start_month, start_date, start_hour, start_min, start_sec;
    uint8_t end_year, end_month, end_date, end_hour, end_min, end_sec;
};

int connect_to(uint16_t port)
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        throw std::system_error(errno, std::generic_category(), "socket");
    }
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (::connect(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) != 0) {
        throw std::system_error(errno, std::generic_category(), "connect");
    }
    return fd;
}

void send_query(int fd, uint8_t tag)
{
    GsQueryRange range{0, 1, 1, 0, 0, 0, 99, 12, 31, 23, 59, 59}; /* 2000-01-01 .. 2099-12-31, widest possible */
    uint8_t frame[TLV_MAX_FRAME];
    size_t frame_len = 0;
    if (tlv_encode(tag, reinterpret_cast<const uint8_t *>(&range), sizeof(range), frame, sizeof(frame),
                   &frame_len) != TLV_OK) {
        throw std::runtime_error("tlv_encode failed");
    }
    if (::send(fd, frame, frame_len, 0) != static_cast<ssize_t>(frame_len)) {
        throw std::runtime_error("send failed");
    }
}

/* Reads and prints GS_RECORD lines until GS_END arrives. Returns how
 * many records were printed. */
int read_records(int fd)
{
    tlv_receiver_t recv_state;
    tlv_receiver_init(&recv_state);
    int count = 0;

    for (;;) {
        uint8_t buf[256];
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) {
            throw std::runtime_error("connection closed before GS_END");
        }
        for (ssize_t i = 0; i < n; i++) {
            tlv_frame_t frame;
            if (tlv_receiver_feed_byte(&recv_state, buf[static_cast<size_t>(i)], &frame) != TLV_OK) {
                continue;
            }
            if (frame.tag == TLV_TAG_GS_RECORD) {
                std::string line(reinterpret_cast<const char *>(frame.value), frame.len);
                std::cout << "  record: " << line << "\n";
                count++;
            } else if (frame.tag == TLV_TAG_GS_END) {
                return count;
            } else {
                std::cout << "  (unexpected tag 0x" << std::hex << static_cast<int>(frame.tag) << std::dec
                          << ")\n";
            }
        }
    }
}

} // namespace

int main()
{
    try {
        DataCollectionAnalysis dca("test_output/gs_link_test.db", /*retention_days=*/7);
        dca.record_measurement("2026-09-08 10:00:00", 24, 55, 60, 80, 0);
        dca.record_event("2026-09-08 10:00:05", "object_detected", "");

        constexpr uint16_t kTestPort = 16000;
        GsLinkServer gs_link(dca, kTestPort);

        std::cout << "--- querying measurements ---\n";
        {
            int fd = connect_to(kTestPort);
            send_query(fd, TLV_TAG_GS_QUERY_DATA);
            int n = read_records(fd);
            ::close(fd);
            std::cout << (n >= 1 ? "PASS" : "FAIL") << ": got " << n << " measurement record(s)\n";
        }

        std::cout << "--- querying events ---\n";
        {
            int fd = connect_to(kTestPort);
            send_query(fd, TLV_TAG_GS_QUERY_EVENTS);
            int n = read_records(fd);
            ::close(fd);
            std::cout << (n >= 1 ? "PASS" : "FAIL") << ": got " << n << " event record(s)\n";
        }

        std::cout << "--- confirming the accept loop serves a second, independent connection ---\n";
        {
            int fd = connect_to(kTestPort);
            send_query(fd, TLV_TAG_GS_QUERY_DATA);
            int n = read_records(fd);
            ::close(fd);
            std::cout << (n >= 1 ? "PASS" : "FAIL") << ": second connection got " << n << " record(s)\n";
        }
    } catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
