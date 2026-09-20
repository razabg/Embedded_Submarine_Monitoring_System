/*
 * main.cpp - the real Central Computer program: an interactive menu
 * (this window) giving an operator full control -- send any of the
 * eight SET_* threshold commands, ask the LNC for its current time,
 * browse stored data, and switch the live transport between UART and
 * the Ethernet-simulation gateway at any time -- while a second,
 * auto-launched terminal window streams every incoming frame as
 * human-readable text (via `tail -f logs/live.log`), so the menu
 * output and the live traffic feed don't interleave in one window.
 *
 * Ties together all four Part-B modules: Communication (LNC-facing
 * transport), Log (CC's own operational log), DataCollectionAnalysis
 * (stores measurement/event data), ManagementCommand (sends commands,
 * handles replies + the LNC's boot-time TIME_SYNC_REQUEST). Log and
 * DataCollectionAnalysis live for the whole program; Transport,
 * Communication and ManagementCommand are rebuilt together (see
 * Connection/build_connection()) whenever the operator switches
 * transports, since Communication holds a Transport& and
 * ManagementCommand holds a Communication& -- both must be torn down
 * and reconstructed as a unit, in that order.
 *
 * Default transport: UART, /dev/ttyACM0 (matches every other manual
 * test tool in this project). Switch to the Ethernet gateway (or back)
 * any time from the menu -- no restart needed.
 *
 * Quit via menu option 0 (or Ctrl-D/EOF).
 */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <unistd.h>

#include "communication.h"
#include "data_collection_analysis.h"
#include "gs_link_server.h"
#include "log.h"
#include "management_command.h"
#include "serial_transport.h"
#include "tcp_transport.h"

static const char *LIVE_LOG_PATH = "logs/live.log";

/* GroundStation connects here (see GsLinkServer/CLAUDE.md) -- separate
 * from the LNC-gateway's own default port (5555), since a real
 * deployment could have both a gateway and this listener alive on the
 * same machine at once. */
static constexpr uint16_t GS_LINK_PORT = 6000;

/* ===============================================================
 * Human-readable frame descriptions -- reuses the exact payload
 * types on_frame() itself decodes (declared in
 * data_collection_analysis.h), so this can never drift out of sync
 * with what's actually being stored.
 * =============================================================== */

static const char *mode_name(int mode)
{
    switch (mode) {
    case 0: return "Normal";
    case 1: return "Warning";
    case 2: return "Error";
    default: return "Unknown";
    }
}

static std::string describe_frame(const tlv_frame_t &f)
{
    char buf[160];

    switch (f.tag) {
    case TLV_TAG_KEEP_ALIVE:
        if (f.value != nullptr && f.len == sizeof(keepalive_payload_t)) {
            const auto *p = reinterpret_cast<const keepalive_payload_t *>(f.value);
            std::snprintf(buf, sizeof(buf),
                          "Keep-alive: temp=%dC hum=%u%% light=%u%% batt=%u%% mode=%s", p->temp_c,
                          p->humidity_pct, p->light_pct, p->battery_pct, mode_name(p->mode));
            return buf;
        }
        return "Keep-alive (malformed)";

    case TLV_TAG_MODE_CHANGE:
        if (f.value != nullptr && f.len == sizeof(mode_change_payload_t)) {
            const auto *p = reinterpret_cast<const mode_change_payload_t *>(f.value);
            std::snprintf(buf, sizeof(buf), "Mode change: %s -> %s (temp=%dC hum=%u%% light=%u%% batt=%u%%)",
                          mode_name(p->old_mode), mode_name(p->new_mode), p->temp_c, p->humidity_pct,
                          p->light_pct, p->battery_pct);
            return buf;
        }
        return "Mode change (malformed)";

    case TLV_TAG_OBJECT_DETECTED:
        return "Object detected";

    case TLV_TAG_OBJECT_CLEARED:
        return "Object cleared";

    case TLV_TAG_QUERY_RECORD:
        /* Already human-readable text -- log.c/event.c forward the
         * stored line as-is (see CLAUDE.md's QUERY_RECORD note). */
        return f.value != nullptr ? std::string(reinterpret_cast<const char *>(f.value), f.len)
                                   : std::string("(empty query record)");

    case TLV_TAG_QUERY_END:
        return "Query finished";

    default:
        std::snprintf(buf, sizeof(buf), "(tag=0x%02X len=%u)", f.tag, f.len);
        return buf;
    }
}

/* ===============================================================
 * Live log window -- appends human-readable lines to LIVE_LOG_PATH;
 * a second terminal, spawned once at startup, tails that file. No
 * PID handshake back to this process (unlike some designs that use a
 * message queue for that) -- nothing here needs to signal or kill
 * that window later, so the extra IPC isn't needed. Closing it is
 * left to the operator.
 * =============================================================== */

/* tail's own --pid=<PID> makes it exit on its own once that PID is
 * gone (checked each time it wakes up to look for new data, so up to
 * ~1s after we exit) -- so the window closes itself whether we quit
 * normally or crash, without us having to track gnome-terminal's own
 * PID (unreliable: gnome-terminal typically just messages an
 * already-running server process and exits immediately, so system()'s
 * child PID doesn't correspond to the actual window anyway). */
static void launch_log_window()
{
    /* gnome-terminal is a thin client: under WSLg it hands this command
     * to a persistent gnome-terminal-server process over D-Bus, which
     * spawns the actual shell in ITS OWN cwd, not ours -- so a relative
     * LIVE_LOG_PATH ("logs/live.log") can resolve against the wrong
     * directory and make `tail` fail instantly with "No such file or
     * directory", leaving a dead pane behind a window that never
     * renders (observed: blank gray window, no tail/gnome-terminal
     * process actually running). Resolving to an absolute path here
     * removes the dependency on whatever directory that server process
     * happens to be in. */
    std::string abs_log_path = std::filesystem::absolute(LIVE_LOG_PATH).string();
    std::string cmd = "gnome-terminal --title 'Central Computer -- live log' -- "
                       "bash -c 'tail -f --pid=" +
                       std::to_string(getpid()) + " " + abs_log_path + "' &";
    /* The trailing '&' backgrounds gnome-terminal, so this return
     * value only reflects whether the shell itself launched -- not
     * whether gnome-terminal actually succeeded (e.g. no DISPLAY);
     * that failure happens later, inside the backgrounded process,
     * with nothing here to observe it. Checked anyway (system()'s
     * return is [[nodiscard]]) and reported on the one failure mode
     * this can actually catch; not fatal either way -- the program
     * still works without the second window, just less conveniently. */
    if (std::system(cmd.c_str()) != 0) {
        std::cout << "warning: failed to launch the live log window; watch " << LIVE_LOG_PATH
                  << " manually (e.g. tail -f " << LIVE_LOG_PATH << ")\n";
    }
}

/* ===============================================================
 * Connection: Transport + Communication + ManagementCommand, rebuilt
 * together whenever the operator switches transports.
 *
 * Declaration order here is deliberate and load-bearing: unique_ptr
 * members are destroyed in REVERSE declaration order, so this
 * destroys comm FIRST, then mgmt, then transport. That's the only
 * safe order: Communication's destructor is what stops its background
 * rx_thread_ (joins it -- see communication.h), and that thread is
 * what calls into mgmt's frame handler. Destroying mgmt before comm
 * (as an earlier version of this file did) leaves a window where
 * rx_thread_ is still alive and comm still exists, but mgmt doesn't
 * -- a frame arriving in that window calls into freed memory. This
 * exact bug caused a real, intermittent crash (segfault) during
 * testing, on every normal quit, not just when switching transports
 * (Connection's automatic destruction hit the same wrong order).
 * switch_transport()'s explicit reset() calls below must use this
 * same order too, for the same reason.
 * =============================================================== */

struct Connection {
    std::unique_ptr<Transport> transport;
    std::unique_ptr<ManagementCommand> mgmt;
    std::unique_ptr<Communication> comm;
    std::string description;
};

static bool parse_host_port(const std::string &arg, std::string &host_out, uint16_t &port_out)
{
    size_t colon = arg.find(':');
    if (colon == std::string::npos || colon == 0 || colon == arg.size() - 1) {
        std::cout << "expected host:port, e.g. 127.0.0.1:5555 (got \"" << arg << "\")\n";
        return false;
    }
    host_out = arg.substr(0, colon);
    port_out = static_cast<uint16_t>(std::atoi(arg.substr(colon + 1).c_str()));
    return true;
}

/* Builds a fresh Connection over UART. Throws on failure (bad device
 * path etc.) -- caller decides what to do (see switch_transport()). */
static Connection connect_uart(const std::string &path, DataCollectionAnalysis &dca,
                                std::ofstream &live_log, Log &log)
{
    Connection c;
    c.transport = std::make_unique<SerialTransport>(path);
    c.comm = std::make_unique<Communication>(*c.transport);
    c.comm->set_log_handler([&dca, &live_log](const tlv_frame_t &f) {
        live_log << describe_frame(f) << std::endl;
        dca.on_frame(f);
    });
    c.mgmt = std::make_unique<ManagementCommand>(*c.comm, log); /* self-registers */
    c.mgmt->get_config(); /* blocks briefly so the menu's first draw already shows real limits, not "?" */
    c.description = "uart " + path;
    return c;
}

/* Same, over the Ethernet-simulation gateway (TCP). */
static Connection connect_tcp(const std::string &host, uint16_t port, DataCollectionAnalysis &dca,
                               std::ofstream &live_log, Log &log)
{
    Connection c;
    c.transport = std::make_unique<TcpTransport>(host, port);
    c.comm = std::make_unique<Communication>(*c.transport);
    c.comm->set_log_handler([&dca, &live_log](const tlv_frame_t &f) {
        live_log << describe_frame(f) << std::endl;
        dca.on_frame(f);
    });
    c.mgmt = std::make_unique<ManagementCommand>(*c.comm, log);
    c.mgmt->get_config(); /* blocks briefly so the menu's first draw already shows real limits, not "?" */
    c.description = "tcp " + host + ":" + std::to_string(port);
    return c;
}

/* ===============================================================
 * Menu input helpers
 * =============================================================== */

static bool read_int(const std::string &prompt, int lo, int hi, int &out)
{
    for (;;) {
        std::cout << prompt;
        if (!(std::cin >> out)) {
            if (std::cin.eof()) {
                return false;
            }
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "not a number, try again\n";
            continue;
        }
        if (out < lo || out > hi) {
            std::cout << "must be between " << lo << " and " << hi << "\n";
            continue;
        }
        return true;
    }
}

static bool read_line(const std::string &prompt, std::string &out)
{
    std::cout << prompt;
    return static_cast<bool>(std::getline(std::cin >> std::ws, out));
}

/* Parses "YYYY-MM-DD HH:MM:SS" into the six wire fields
 * query_range_payload_t needs (year as an offset from 2000, matching
 * every other timestamp payload in this protocol). False on a
 * malformed string or an out-of-range field -- nothing is written to
 * the out-params in that case. */
static bool parse_datetime(const std::string &s, uint8_t &year, uint8_t &month, uint8_t &date,
                            uint8_t &hour, uint8_t &min, uint8_t &sec)
{
    int y, mo, d, h, mi, se;
    if (std::sscanf(s.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6) return false;
    if (y < 2000 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 ||
        mi > 59 || se < 0 || se > 59) {
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

/* One line summarizing the LNC's thresholds as ManagementCommand last
 * heard them (see its class-level comment) -- "?" for any field never
 * confirmed by a CONFIG_REPLY yet. Shown in the menu header so a SET_*
 * change's effect is visible once refreshed (option 13), not just
 * assumed sent. */
static std::string format_thresholds(const ManagementCommand::Thresholds &t)
{
    auto i = [](auto opt) { return opt ? std::to_string(static_cast<int>(*opt)) : std::string("?"); };
    std::ostringstream out;
    out << "temp N[" << i(t.tempNormalMin) << "," << i(t.tempNormalMax) << "] W[" << i(t.tempWarningMin)
        << "," << i(t.tempWarningMax) << "]"
        << " | hum N>=" << i(t.humidityNormalMin) << " W>=" << i(t.humidityWarningMin)
        << " | light N>=" << i(t.lightNormalMin) << " W>=" << i(t.lightWarningMin)
        << " | batt N>=" << i(t.batteryNormalMin) << " W>=" << i(t.batteryWarningMin);
    return out.str();
}

/* ===============================================================
 * Menu actions
 * =============================================================== */

/* No Log& here -- ManagementCommand itself logs every command it
 * actually sends or rejects (see management_command.cpp), so these
 * wrappers just handle the interactive prompt and echo the outcome. */

static void set_temp_normal(ManagementCommand &mgmt)
{
    int min, max;
    if (!read_int("  min (C, -128..127): ", -128, 127, min)) return;
    if (!read_int("  max (C, -128..127): ", -128, 127, max)) return;
    std::cout << (mgmt.set_temp_normal(static_cast<int8_t>(min), static_cast<int8_t>(max))
                      ? "sent\n"
                      : "rejected (min must be <= max)\n");
}

static void set_temp_warning(ManagementCommand &mgmt)
{
    int min, max;
    if (!read_int("  min (C, -128..127): ", -128, 127, min)) return;
    if (!read_int("  max (C, -128..127): ", -128, 127, max)) return;
    std::cout << (mgmt.set_temp_warning(static_cast<int8_t>(min), static_cast<int8_t>(max))
                      ? "sent\n"
                      : "rejected (min must be <= max)\n");
}

static void set_humidity_normal(ManagementCommand &mgmt)
{
    int min;
    if (!read_int("  min (%, 0..100): ", 0, 100, min)) return;
    mgmt.set_humidity_normal(static_cast<uint8_t>(min));
    std::cout << "sent\n";
}

static void set_humidity_warning(ManagementCommand &mgmt)
{
    int min;
    if (!read_int("  min (%, 0..100): ", 0, 100, min)) return;
    mgmt.set_humidity_warning(static_cast<uint8_t>(min));
    std::cout << "sent\n";
}

static void set_light_normal(ManagementCommand &mgmt)
{
    int min;
    if (!read_int("  min (%, 0..100): ", 0, 100, min)) return;
    mgmt.set_light_normal(static_cast<uint8_t>(min));
    std::cout << "sent\n";
}

static void set_light_warning(ManagementCommand &mgmt)
{
    int min;
    if (!read_int("  min (%, 0..100): ", 0, 100, min)) return;
    mgmt.set_light_warning(static_cast<uint8_t>(min));
    std::cout << "sent\n";
}

static void set_battery_normal(ManagementCommand &mgmt)
{
    int min;
    if (!read_int("  min (%, 0..100): ", 0, 100, min)) return;
    mgmt.set_battery_normal(static_cast<uint8_t>(min));
    std::cout << "sent\n";
}

static void set_battery_warning(ManagementCommand &mgmt)
{
    int min;
    if (!read_int("  min (%, 0..100): ", 0, 100, min)) return;
    mgmt.set_battery_warning(static_cast<uint8_t>(min));
    std::cout << "sent\n";
}

static void do_get_time(ManagementCommand &mgmt)
{
    mgmt.get_time();
    std::cout << "sent -- watch the live log window for the LNC's reply\n";
}

static void do_refresh_config(ManagementCommand &mgmt)
{
    std::cout << (mgmt.get_config() ? "refreshed -- see the limits line above\n"
                                     : "no reply from the LNC within 1s -- check the connection\n");
}

/* Live query, straight from the LNC's own SD card -- distinct from
 * options 2/3, which read this CC's own local database (populated
 * passively from traffic already seen). This actually asks the LNC to
 * search LOG1..7.TXT/EVENTS.TXT right now. Replies (QUERY_RECORD/
 * QUERY_END) route through the log handler, not here -- see
 * management_command.h -- so they show up in the live log window. */
static void do_query_lnc_sd(ManagementCommand &mgmt)
{
    std::string startStr, endStr;
    if (!read_line("  start (YYYY-MM-DD HH:MM:SS): ", startStr)) return;
    if (!read_line("  end   (YYYY-MM-DD HH:MM:SS): ", endStr)) return;

    query_range_payload_t range{};
    if (!parse_datetime(startStr, range.start_year, range.start_month, range.start_date,
                         range.start_hour, range.start_min, range.start_sec) ||
        !parse_datetime(endStr, range.end_year, range.end_month, range.end_date, range.end_hour,
                         range.end_min, range.end_sec)) {
        std::cout << "bad date/time (expected YYYY-MM-DD HH:MM:SS, year 2000-2099)\n";
        return;
    }

    int which;
    std::cout << " 1) Measurements (QUERY_DATA)\n 2) Events (QUERY_EVENTS)\n 3) Both\n";
    if (!read_int("> ", 1, 3, which)) return;

    if (which == 1 || which == 3) mgmt.query_data(range);
    if (which == 2 || which == 3) mgmt.query_events(range);
    std::cout << "sent -- watch the live log window for the LNC's SD-card records\n";
}

static void show_measurements(DataCollectionAnalysis &dca)
{
    std::string start, end;
    if (!read_line("  start (YYYY-MM-DD HH:MM:SS): ", start)) return;
    if (!read_line("  end   (YYYY-MM-DD HH:MM:SS): ", end)) return;

    auto rows = dca.query_measurements(start, end);
    std::cout << rows.size() << " row(s)\n";
    for (const auto &m : rows) {
        std::cout << "  " << m.timestamp << "  temp=" << m.temp_c << "C hum=" << m.humidity_pct
                  << "% light=" << m.light_pct << "% batt=" << m.battery_pct << "% mode=" << m.mode
                  << "\n";
    }
}

static void show_events(DataCollectionAnalysis &dca)
{
    std::string start, end;
    if (!read_line("  start (YYYY-MM-DD HH:MM:SS): ", start)) return;
    if (!read_line("  end   (YYYY-MM-DD HH:MM:SS): ", end)) return;

    auto rows = dca.query_events(start, end);
    std::cout << rows.size() << " row(s)\n";
    for (const auto &e : rows) {
        std::cout << "  " << e.timestamp << "  " << e.type << "  " << e.description << "\n";
    }
}

/* Tears down the current connection and builds a fresh one from
 * operator input. On failure (e.g. bad device path), the OLD
 * connection is already gone -- reports the error and leaves conn
 * disconnected (transport/comm/mgmt all null) rather than silently
 * keeping a stale one alive, so the menu's other actions plainly
 * can't be used until reconnected. */
static void switch_transport(Connection &conn, DataCollectionAnalysis &dca, Log &log,
                              std::ofstream &live_log)
{
    int choice;
    std::cout << " 1) UART\n 2) Ethernet (via gateway)\n";
    if (!read_int("> ", 1, 2, choice)) return;

    /* Order matters -- see the Connection struct's comment above.
     * comm first (stops rx_thread_ via its destructor's join), only
     * then mgmt (now safe -- nothing can call into it anymore), then
     * transport. */
    conn.comm.reset();
    conn.mgmt.reset();
    conn.transport.reset();
    /* Reflects reality immediately -- if anything below fails or
     * returns early, this is what the menu header shows instead of
     * misleadingly repeating the old (now torn-down) connection's
     * description. Overwritten with the real one if connecting
     * actually succeeds (conn = connect_uart(...)/connect_tcp(...)
     * replaces the whole struct, description included). */
    conn.description = "disconnected";

    try {
        if (choice == 1) {
            std::string path;
            std::cout << "  device [/dev/ttyACM0]: ";
            std::getline(std::cin >> std::ws, path);
            if (path.empty()) path = "/dev/ttyACM0";
            conn = connect_uart(path, dca, live_log, log);
        } else {
            std::string hostport;
            std::cout << "  host:port [127.0.0.1:5555]: ";
            std::getline(std::cin >> std::ws, hostport);
            if (hostport.empty()) hostport = "127.0.0.1:5555";
            std::string host;
            uint16_t port = 0;
            if (!parse_host_port(hostport, host, port)) return;
            conn = connect_tcp(host, port, dca, live_log, log);
        }
        std::cout << "connected: " << conn.description << "\n";
        log.write("switched transport to " + conn.description);
    } catch (const std::exception &e) {
        std::cout << "connect failed: " << e.what() << "\n";
        log.write("transport switch failed: " + std::string(e.what()));
    }
}

static void run_menu(Connection &conn, DataCollectionAnalysis &dca, Log &log, std::ofstream &live_log)
{
    for (;;) {
        std::cout << "\n===== Central Computer (" << conn.description << ") =====\n";
        if (conn.mgmt) {
            std::cout << "limits: " << format_thresholds(conn.mgmt->thresholds()) << "\n";
        }
        std::cout << " 1) Get LNC current time\n"
                     " 2) Show stored measurements (time range)\n"
                     " 3) Show stored events (time range)\n"
                     " 4) Switch transport (UART / Ethernet)\n"
                     "\n"
                     " 5) Set temperature normal range\n"
                     " 6) Set temperature warning range\n"
                     " 7) Set humidity normal bound\n"
                     " 8) Set humidity warning bound\n"
                     " 9) Set light normal bound\n"
                     "10) Set light warning bound\n"
                     "11) Set battery normal bound\n"
                     "12) Set battery warning bound\n"
                     "\n"
                     "13) Refresh limits from LNC (GET_CONFIG)\n"
                     "14) Query LNC's SD card directly (time range)\n"
                     "\n"
                     " 0) Quit\n";

        int choice;
        if (!read_int("> ", 0, 14, choice)) {
            return;
        }

        if (choice != 4 && choice != 2 && choice != 3 && choice != 0 && !conn.mgmt) {
            std::cout << "not connected -- use option 4 first\n";
            continue;
        }

        switch (choice) {
        case 1: do_get_time(*conn.mgmt); break;
        case 2: show_measurements(dca); break;
        case 3: show_events(dca); break;
        case 4: switch_transport(conn, dca, log, live_log); break;
        case 5: set_temp_normal(*conn.mgmt); break;
        case 6: set_temp_warning(*conn.mgmt); break;
        case 7: set_humidity_normal(*conn.mgmt); break;
        case 8: set_humidity_warning(*conn.mgmt); break;
        case 9: set_light_normal(*conn.mgmt); break;
        case 10: set_light_warning(*conn.mgmt); break;
        case 11: set_battery_normal(*conn.mgmt); break;
        case 12: set_battery_warning(*conn.mgmt); break;
        case 13: do_refresh_config(*conn.mgmt); break;
        case 14: do_query_lnc_sd(*conn.mgmt); break;
        case 0: return;
        default: break;
        }
    }
}

int main(int argc, char **argv)
{
    try {
        Log log("logs/central_computer");
        DataCollectionAnalysis dca("data/central_computer.db");

        /* GsLinkServer holds DataCollectionAnalysis& (not owned) and
         * starts a background thread that may call into it at any
         * time -- declared AFTER dca so it's destroyed BEFORE dca
         * during stack unwind (C++ destroys locals in reverse
         * declaration order). Reversing this order would let dca's
         * destructor run while gs_link's accept-loop thread could
         * still be mid-query against it -- a use-after-free. */
        GsLinkServer gs_link(dca, GS_LINK_PORT);

        /* truncate, not append -- this file is a live view, not a
         * permanent record (DataCollectionAnalysis's db is that). */
        std::ofstream live_log(LIVE_LOG_PATH, std::ios::trunc);
        if (!live_log.is_open()) {
            throw std::runtime_error("failed to open " + std::string(LIVE_LOG_PATH));
        }

        launch_log_window();

        Connection conn; /* starts disconnected */
        if (argc > 1 && std::string(argv[1]) == "--tcp") {
            if (argc < 3) {
                std::fprintf(stderr, "usage: %s --tcp host:port\n", argv[0]);
                return 1;
            }
            std::string host;
            uint16_t port = 0;
            if (!parse_host_port(argv[2], host, port)) {
                return 1;
            }
            conn = connect_tcp(host, port, dca, live_log, log);
        } else {
            const char *path = (argc > 1) ? argv[1] : "/dev/ttyACM0"; /* default: UART */
            conn = connect_uart(path, dca, live_log, log);
        }

        std::cout << "connected: " << conn.description << "\n";
        log.write("connected to LNC via " + conn.description);

        run_menu(conn, dca, log, live_log);

        log.write("shutting down");
    } catch (const std::exception &e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

    std::printf("\nclosed\n");
    return 0;
}
