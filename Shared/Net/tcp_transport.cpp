/*
 * tcp_transport.cpp - Transport implementation over a TCP client socket.
 * See tcp_transport.h for the class-level overview.
 */
#include "tcp_transport.h"

#include <cerrno>       /* errno, EAGAIN/EWOULDBLOCK/EINTR/ECONNRESET */
#include <cstring>
#include <stdexcept>    /* std::runtime_error, std::logic_error */
#include <system_error> /* std::system_error, std::generic_category */
#include <utility>      /* std::move */

#include <netdb.h>       /* getaddrinfo()/freeaddrinfo()/gai_strerror(), struct addrinfo --
                          * the modern, protocol-independent address-resolution API (the
                          * replacement for hand-filling a sockaddr_in + inet_pton()) */
#include <sys/socket.h>  /* socket(), connect(), recv(), send(), setsockopt() */
#include <sys/types.h>
#include <unistd.h>      /* close() */

/* Matches SerialPort's VMIN=0/VTIME=1 read timeout (0.1s) -- so both
 * transports behave the same way from Communication's rx_loop()'s
 * point of view: "come back with 0 if nothing arrived in ~100ms".
 * constexpr (not just const): the value is fixed at COMPILE time, so
 * the compiler can bake it straight into the generated code -- no
 * runtime memory read needed, and it'd be a compile error to ever
 * misuse this somewhere that needs a genuine compile-time constant. */
static constexpr long kReceiveTimeoutUsec = 100000;

TcpTransport::TcpTransport(std::string host, uint16_t port)
    : host_(std::move(host)), port_(port) /* host_: the server's address,
                                            * "127.0.0.1" in this project --
                                            * a hostname would also work,
                                            * getaddrinfo() below can
                                            * resolve names too, not just
                                            * literal IPs */
{
    do_connect(); /* fd_ stays -1 if this throws - caller can retry later */
}

TcpTransport::~TcpTransport()
{
    close_fd();
}

void TcpTransport::do_connect()
{
    struct addrinfo hints{};   /* "what kind of address do I want back" --
                                 * {} zero-initializes it first, required
                                 * by this API before setting only the
                                 * fields below */
    struct addrinfo *res = nullptr; /* output: head of a linked list of
                                      * candidate addresses getaddrinfo()
                                      * hands back */

    hints.ai_family = AF_INET;      /* IPv4 only */
    hints.ai_socktype = SOCK_STREAM; /* TCP, not UDP */

    /* Resolves host_:port_ into `res`. Takes the port as a string (not
     * a number) since getaddrinfo() also accepts named services like
     * "http" -- std::to_string() here just converts our numeric port. */
    int gai_err = ::getaddrinfo(host_.c_str(), std::to_string(port_).c_str(), &hints, &res);
    if (gai_err != 0) {
        throw std::runtime_error(std::string("TcpTransport: getaddrinfo failed: ") +
                                  gai_strerror(gai_err));
    }

    int fd = -1;
    /* getaddrinfo() can return more than one candidate (e.g. a hostname
     * with several IPs) -- try each in turn until one actually
     * connects, since being *returned* doesn't mean it's *reachable*.
     * In this project (always "127.0.0.1") the list is always one
     * entry long, but this is the correct general pattern regardless. */
    for (struct addrinfo *p = res; p != nullptr; p = p->ai_next) {
        /* A fresh socket per candidate, matching *that* candidate's
         * own family/type/protocol (not necessarily AF_INET/SOCK_STREAM
         * again, in general -- here they will be, since hints forced it). */
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) {
            continue; /* this candidate's socket() failed -- try the next one */
        }
        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
            break; /* connected -- keep this fd, stop trying candidates */
        }
        ::close(fd);  /* this candidate didn't connect -- discard and try the next */
        fd = -1;
    }
    freeaddrinfo(res); /* done with the candidate list either way */

    if (fd < 0) {
        /* every candidate failed */
        throw std::system_error(errno, std::generic_category(), "TcpTransport: connect");
    }

    /* Give the now-connected socket the same ~100ms receive timeout
     * SerialPort gets from VMIN/VTIME -- see kReceiveTimeoutUsec above. */
    struct timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = kReceiveTimeoutUsec;
    if (::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
        int saved_errno = errno; /* close() below could otherwise clobber errno */
        ::close(fd);
        throw std::system_error(saved_errno, std::generic_category(),
                                 "TcpTransport: setsockopt SO_RCVTIMEO");
    }

    fd_ = fd; /* only commit to the member once every step above succeeded */
}

void TcpTransport::close_fd() noexcept
{
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1; /* marks this transport as closed, checked by read()/write() */
    }
}

void TcpTransport::reconnect()
{
    close_fd();   /* drop the old (presumably dead) connection first */
    do_connect(); /* then connect fresh, to the same host_:port_ */
}

long TcpTransport::read(uint8_t *buf, size_t len)
{
    if (fd_ < 0) {
        throw std::logic_error("TcpTransport::read on a closed connection");
    }

    for (;;) {
        ssize_t n = ::recv(fd_, buf, len, 0);
        if (n > 0) {
            /* real bytes arrived -- hand the count back up to
             * Communication::rx_loop(), which is the thread that
             * actually called this read(). */
            return static_cast<long>(n);
        }
        if (n == 0) {
            /* The far end closed the connection -- a real disconnect,
             * not "nothing arrived yet". Unlike a serial port's read
             * timeout, recv() returning 0 specifically means orderly
             * shutdown. Treat as an error so rx_loop() reconnects,
             * matching SerialPort's own read-failure handling. */
            throw std::system_error(ECONNRESET, std::generic_category(),
                                     "TcpTransport: peer closed connection");
        }
        /* n < 0 -- recv() failed; errno says why */
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            /* Not a real failure -- the SO_RCVTIMEO timer above simply
             * elapsed with nothing received. Report it the same way
             * SerialPort's own read timeout does: 0 bytes, try again
             * next time rx_loop() calls read(). */
            return 0;
        }
        if (errno == EINTR) {
            /* A signal (e.g. Ctrl-C) interrupted the blocked recv()
             * before it finished -- not a real error either, the
             * operation itself didn't fail. Just retry it. */
            continue;
        }
        /* Any other errno is a genuine failure. */
        throw std::system_error(errno, std::generic_category(), "TcpTransport: recv");
    }
}

long TcpTransport::write(const uint8_t *buf, size_t len)
{
    if (fd_ < 0) {
        throw std::logic_error("TcpTransport::write on a closed connection");
    }

    size_t sent = 0;
    /* send() may accept fewer bytes than asked. Loop until all are gone. */
    while (sent < len) {
        ssize_t n = ::send(fd_, buf + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue; /* interrupted by a signal, not a real failure -- retry */
            }
            throw std::system_error(errno, std::generic_category(), "TcpTransport: send");
        }
        sent += static_cast<size_t>(n);
    }

    return static_cast<long>(sent);
}
