/*
 * transport.h - the byte-transport contract Communication depends on.
 *
 * Communication used to own a concrete SerialPort directly. That tied it
 * to UART specifically, even though the project's transport rule says
 * UART vs Ethernet must be a config detail Communication doesn't know
 * about. This abstract class is the fix: Communication is written
 * against *this* interface only, never against SerialPort or a TCP
 * socket by name, so which concrete transport it's actually using is
 * decided once, by whoever constructs it -- not baked into
 * Communication's own code.
 *
 * Lives in Shared/ (not CentralComputer/) because TcpTransport
 * (tcp_transport.h, the one concrete implementation that needs this
 * interface) is used by two separate programs now: CentralComputer (as
 * the client dialing the LNC-simulation gateway) and GroundStation (as
 * the client dialing a submarine's CentralComputer). SerialTransport
 * (CentralComputer/src/communication/serial_transport.h, wraps
 * SerialPort -- used when UART is chosen) stays CentralComputer-only,
 * since GroundStation never talks UART.
 */
#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <cstddef>
#include <cstdint>

class Transport
{
public:
    virtual ~Transport() = default;

    /*
     * Read whatever has arrived, up to len bytes.
     *   > 0  number of bytes read
     *   = 0  nothing arrived before the read timeout (NOT end of file)
     * Throws std::system_error (or a subclass) on a real error.
     */
    virtual long read(uint8_t *buf, size_t len) = 0;

    /*
     * Write all len bytes. Throws on error.
     */
    virtual long write(const uint8_t *buf, size_t len) = 0;

    /*
     * Recover from a bad connection (unplugged serial device, dropped
     * TCP link) using whatever this transport was originally
     * constructed with. Throws on failure -- callers retry later.
     */
    virtual void reconnect() = 0;
};

#endif /* TRANSPORT_H */
