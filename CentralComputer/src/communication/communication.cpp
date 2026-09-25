/*
 * communication.cpp - Central Computer Communication module (LNC-facing)
 * See communication.h for the public API and the class-level overview.
 */
#include "communication.h"

#include <chrono>
#include <stdexcept>

/* ===============================================================
 * Construction / destruction
 * =============================================================== */

Communication::Communication(Transport &transport) : transport_(transport)
{
    tlv_receiver_init(&rx_recv_);
    rx_thread_ = std::thread(&Communication::rx_loop, this);
}

Communication::~Communication()
{
    running_ = false;
    if (rx_thread_.joinable()) {
        rx_thread_.join();
    }
}

/* ===============================================================
 * TX side
 *
 * send() is the only public entry point; it builds the frame and
 * writes it directly (no priority queue -- unlike the LNC, the spec
 * gives no priority scheme for CC -> LNC traffic). send_mutex_ keeps
 * concurrent callers from interleaving bytes on the wire.
 * =============================================================== */

void Communication::send(uint8_t tag, const uint8_t *value, uint8_t value_len)
{
    uint8_t frame[TLV_MAX_FRAME];
    size_t frame_len;

    std::lock_guard<std::mutex> lock(send_mutex_);

    if (tlv_encode(tag, value, value_len, frame, sizeof(frame), &frame_len) != TLV_OK) {
        throw std::invalid_argument("Communication::send: tlv_encode failed");
    }

    transport_.write(frame, frame_len);
}

void Communication::set_management_handler(FrameHandler handler)
{
    on_management_ = std::move(handler);
}

void Communication::set_log_handler(FrameHandler handler)
{
    on_log_ = std::move(handler);
}

/* ===============================================================
 * RX side
 *
 * rx_loop() (running on rx_thread_) is the only thing that ever reads
 * transport_. It hands finished frames to route_frame() through the
 * frame_trampoline() bridge, which dispatches to on_management_/
 * on_log_ by tag.
 * =============================================================== */

void Communication::rx_loop()
{
    uint8_t buf[256];

    while (running_.load()) { // read safely the bool value
        long n;
        try {
            n = transport_.read(buf, sizeof(buf));
        } catch (const std::exception &) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            try {
                transport_.reconnect();
            } catch (const std::exception &) {
                /* still down; next loop iteration tries the read again */
            }
            continue;
        }

        if (n > 0) {
            tlv_receiver_feed(&rx_recv_, buf, static_cast<size_t>(n),
                               &Communication::frame_trampoline, this);
        }
    }
}

void Communication::frame_trampoline(const tlv_frame_t *frame, void *ctx)
{
    static_cast<Communication *>(ctx)->route_frame(*frame);
}

void Communication::route_frame(const tlv_frame_t &frame)
{
    switch (frame.tag) {
    case TLV_TAG_ACK:
    case TLV_TAG_NACK:
    case TLV_TAG_TIME_REPLY:
    case TLV_TAG_TIME_SYNC_REQUEST:
    case TLV_TAG_CONFIG_REPLY:
        on_management_(frame);
        break;

    case TLV_TAG_KEEP_ALIVE:
    case TLV_TAG_DATA_REPORT:
    case TLV_TAG_MODE_CHANGE:
    case TLV_TAG_OBJECT_DETECTED:
    case TLV_TAG_OBJECT_CLEARED:
    case TLV_TAG_STARTUP:
    case TLV_TAG_QUERY_RECORD:
    case TLV_TAG_QUERY_END:
        on_log_(frame);
        break;

    default:
        break;
    }
}