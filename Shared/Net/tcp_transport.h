/*
 * tcp_transport.h - Transport implementation over a TCP client socket.
 *
 * Always the CLIENT side -- who the server is depends on which link
 * this object is used for:
 *   - CentralComputer, talking to the LNC-simulation gateway (the
 *     gateway owns the real serial port, so it's the server -- see
 *     CLAUDE.md's "Ethernet-simulation gateway" note).
 *   - GroundStation, talking to a submarine's CentralComputer (this
 *     time CentralComputer is the server -- opposite direction from
 *     the link above, same reusable client code, just pointed at a
 *     different address).
 * Lives in Shared/ for exactly that reason: two separate programs, in
 * two separate client/server relationships, both need it.
 *
 * RAII-style like SerialPort: constructor connects, destructor closes,
 * no separate open()/close() to remember.
 *
 * Knows nothing about TLV, messages, or commands -- same as SerialPort,
 * it only moves bytes.
 */
#ifndef TCP_TRANSPORT_H
#define TCP_TRANSPORT_H

#include "transport.h"
#include <cstdint>
#include <string>

class TcpTransport : public Transport
{
public:
    /*
     * Connects to host:port immediately (the server's listening
     * address). Throws std::system_error on failure -- if this
     * constructor returns normally, the connection is open and ready
     * to use.
     */
    TcpTransport(std::string host, uint16_t port);

    /* Closes the socket. Never throws. */
    ~TcpTransport() override;

    /* Owns one OS socket fd -- copying would let two objects both
     * believe they own (and could both close) it. Not move-enabled
     * either, for the same reason SerialPort isn't: this project never
     * needs to relocate a live connection, only reconnect it. */
    TcpTransport(const TcpTransport &) = delete;
    TcpTransport &operator=(const TcpTransport &) = delete;
    TcpTransport(TcpTransport &&) = delete;
    TcpTransport &operator=(TcpTransport &&) = delete;

    /*
     * Read whatever has arrived, up to len bytes.
     *   > 0  number of bytes read
     *   = 0  nothing arrived before the receive timeout (NOT a
     *        disconnect -- a real orderly shutdown from the far end
     *        is reported as an error instead, since recv() returning
     *        0 means something different for a socket than for a
     *        serial port's read timeout)
     * Throws std::system_error on a real error, including the far end
     * closing the connection.
     */
    long read(uint8_t *buf, size_t len) override;

    /* Write all len bytes. Loops internally, same reasoning as
     * SerialPort::write() -- send() may accept fewer bytes than asked. */
    long write(const uint8_t *buf, size_t len) override;

    /* Close the socket (if open) and connect again to the same
     * host:port this object was constructed with. Throws
     * std::system_error on failure; callers can retry later. */
    void reconnect() override;

private:
    std::string host_;
    uint16_t port_;
    int fd_ = -1;

    /* Resolves host_:port_, connects, and applies the receive timeout.
     * Sets fd_ on success; throws (fd_ stays -1) on failure. */
    void do_connect();

    /* Closes fd_ if open. Never throws. */
    void close_fd() noexcept;
};

#endif /* TCP_TRANSPORT_H */
