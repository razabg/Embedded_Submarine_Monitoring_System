/*
 * central_computer.h - GroundStation's handle to a combat submarine's
 * real Central Computer (Part B), reached over Ethernet (section 4:
 * "requests stored log data and event data for a given date/time
 * range"). A CombatSubmarine composes one of these -- see
 * combat_submarine.h.
 *
 * Real client now: connects to Central Computer's GsLinkServer
 * (CentralComputer/src/gsLink/) over TCP, using the same TcpTransport
 * (Shared/Net/) the LNC-gateway link already uses -- just pointed the
 * other way (GroundStation is the client here; Central Computer is
 * the server -- see CLAUDE.md's note on why these two links run in
 * opposite directions).
 *
 * Connect-per-query, not a held-open connection: this is an
 * occasional, on-demand link (the operator asks for one submarine's
 * data every so often), not the continuous LNC<->CC stream, and a
 * submarine's Central Computer may not always be reachable. Each call
 * opens a fresh TcpTransport, sends one request, reads the reply
 * stream, and closes -- no reconnect logic needed, since there's
 * never a long-lived connection to lose.
 *
 * A connection failure (host unreachable, refused, timeout, no reply)
 * is swallowed into a single-line result rather than thrown -- the
 * GroundStation menu just prints whatever comes back, the same way
 * whether the query succeeded or the submarine couldn't be reached.
 */
#ifndef CENTRAL_COMPUTER_H
#define CENTRAL_COMPUTER_H

#include <cstdint>
#include <string>
#include <vector>

class CentralComputer
{
public:
    CentralComputer(std::string host, uint16_t port);

    /* Returns every stored log/event line in [start, end]
     * ("YYYY-MM-DD HH:MM:SS" strings, matching the Central Computer's
     * own DataCollectionAnalysis format). On any failure (malformed
     * range, unreachable host, no reply), returns a single line
     * describing what went wrong instead of throwing. */
    std::vector<std::string> queryLogs(const std::string &start, const std::string &end) const;
    std::vector<std::string> queryEvents(const std::string &start, const std::string &end) const;

private:
    std::string host_;
    uint16_t port_;

    std::vector<std::string> query(uint8_t tag, const std::string &start, const std::string &end) const;
};

#endif /* CENTRAL_COMPUTER_H */
