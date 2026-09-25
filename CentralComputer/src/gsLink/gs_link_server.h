/*
 * gs_link_server.h - Central Computer's side of the GroundStation link
 * (section 4: "Ground Station... requests stored log data and event
 * data for a given date/time range"). Central Computer is the SERVER
 * here -- the opposite role from the LNC-gateway link, where it's the
 * client (see CLAUDE.md's "Ethernet-simulation gateway" note) -- since
 * it owns the data GroundStation wants, and there's exactly one
 * GroundStation but potentially many Central Computers, each at its
 * own address; GroundStation connects out to whichever one the
 * operator picks.
 *
 * Modeled on gateway.cpp's bind/listen/accept skeleton, but NOT a
 * dumb byte relay like the gateway -- this one decodes each incoming
 * GS_QUERY_DATA/GS_QUERY_EVENTS frame with the TLV machinery and
 * answers directly from DataCollectionAnalysis's stored rows.
 * Single-threaded per session (unlike the gateway's two relay
 * threads): a query is a synchronous request-then-reply-stream, never
 * two simultaneous directions of traffic needing independent timing.
 *
 * One connection == one query, by design (matches GroundStation's own
 * connect-per-query client): a client connects, sends exactly one
 * GS_QUERY_DATA or GS_QUERY_EVENTS frame, gets a stream of GS_RECORD
 * frames followed by one GS_END, and disconnects. The accept loop
 * then serves the next connection.
 *
 * RAII, same shape as Communication: constructor binds+listens+starts
 * the accept-loop thread (throws on failure, same as TcpTransport's
 * connect failure); destructor stops the thread and joins it, never
 * throws. Holds DataCollectionAnalysis& (not owned -- same convention
 * as Communication's Transport&), so it must be constructed AFTER and
 * destroyed BEFORE the DataCollectionAnalysis it's given (declaration
 * order in main.cpp handles this automatically -- see the comment
 * there).
 */
#ifndef GS_LINK_SERVER_H
#define GS_LINK_SERVER_H

#include <atomic>
#include <cstdint>
#include <thread>

#include "data_collection_analysis.h"

class GsLinkServer
{
public:
    GsLinkServer(DataCollectionAnalysis &dca, uint16_t port);
    ~GsLinkServer();

    /* Owns a listening socket fd + a running thread -- copying would
     * let two objects both believe they own (and could both close)
     * the socket. Same reasoning as TcpTransport/SerialPort. */
    GsLinkServer(const GsLinkServer &) = delete;
    GsLinkServer &operator=(const GsLinkServer &) = delete;

private:
    DataCollectionAnalysis &dca_;
    int listen_fd_ = -1;
    std::atomic<bool> stop_{false};
    std::thread accept_thread_;

    void accept_loop();

    /* Serves exactly one query over an already-accepted client_fd,
     * then returns (whether it answered a query, the client
     * disconnected early, or a genuine socket error occurred) --
     * accept_loop() closes client_fd and goes back to accept()
     * either way. */
    void handle_client(int client_fd);
};

#endif /* GS_LINK_SERVER_H */
