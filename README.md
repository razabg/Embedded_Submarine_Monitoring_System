# Submarine Monitoring System

A three-tier embedded systems project: firmware on an STM32 board reads a
submarine's sensors and reports its status up a chain of two Linux programs
that manage a fleet of submarines from the ground.

```
Ground Station  <---  Ethernet  --->  Central Computer  <---  UART / Ethernet  --->  LNC (STM32)
 (fleet & OOP)                        (fleet's data hub)                              (sensors)
```

- **LNC** ("Local Node Controller") — firmware, in C, running on the sensor
  board itself.
- **Central Computer** — a C/C++ Linux service that talks to one LNC, stores
  its data, and answers queries from the Ground Station.
- **Ground Station** — a C++ Linux program that manages the whole fleet
  (research and combat submarines, their missions, and messages between
  them) and pulls historical data from any submarine's Central Computer on
  demand.

Every message on every link — LNC↔Central Computer and Central Computer↔Ground
Station — uses the same hand-rolled **TLV (Tag-Length-Value)** binary protocol,
implemented once in `Shared/` and compiled unmodified into all three programs.

---

## Repository layout

| Folder | Language | Runs on | Contents |
|---|---|---|---|
| `LNC/` | C | STM32 Nucleo-L476RG | Firmware: 9 modules on FreeRTOS |
| `CentralComputer/` | C / C++ | Linux | LNC-facing service: comms, commands, storage, gateway |
| `GroundStation/` | C++ | Linux | Fleet management (OOP) + queries to a submarine's Central Computer |
| `Shared/ProtocolTLV/` | C | both | The TLV wire protocol — one implementation, no forks |
| `Shared/Net/` | C++ | both PC programs | `Transport` interface + TCP implementation, shared by CC and GS |

Keeping the protocol in one shared tree means the firmware and both PC
programs always agree on the wire format — a change is one commit, not three.

---

## 1. LNC — the end unit (C, STM32 Nucleo-L476RG, FreeRTOS)

The firmware that actually sits on the submarine. Nine modules, each
FreeRTOS task (or plain function call) doing exactly one job, talking to
each other only through queues, mutexes and a shared communication layer —
never by reaching into another module's state.

| Module | Responsibility |
|---|---|
| **Monitor** | Every 5 s, samples temperature/humidity/light/battery, classifies Normal/Warning/Error, logs the reading |
| **Object Detection** | Filters a repurposed IR receiver's noisy edges into clean "object detected / cleared" events |
| **Event** | Timestamps every event, drives the RGB LED and buzzer, writes `EVENTS.TXT`, notifies the Central Computer |
| **Log** | Writes daily measurement logs, rotates on a 7-day cycle |
| **Communication** | Owns the UART link; frames/parses TLV; sends by priority (keep-alive > events > data) |
| **Configuration** | Holds all configurable limits, persists them to internal Flash |
| **Init** | Boot sequence: time sync with the Central Computer, then starts every other module |
| **Keep-Alive** | Sends a heartbeat with the latest reading every 6 s |
| **Watchdog** | Refreshes the IWDG hardware watchdog; reports whether the last boot was a watchdog reset |

### Peripherals & tools

| Peripheral | Used for | Notes |
|---|---|---|
| `ADC1` / `ADC2` | Battery (potentiometer), light (LDR) | Independent channels, polled on demand |
| `TIM2` | Free-running 1 µs clock | Times the DHT11's bit-banged single-wire protocol |
| `TIM3` | Buzzer PWM + note sequencing | Alarm siren and object-detection "sonar ping," same ADT |
| `TIM5` | Object-detection presence timeout | Hardware countdown, reset on every valid IR edge |
| `TIM8` | Breathing red LED | PWM duty swept from a dedicated task |
| `I2C3` | DS1307 external RTC | Battery-backed source of truth; internal RTC is the working clock |
| `SPI1` | microSD card (FatFS) | Daily log files + the events file |
| `USART2` | Communication module only | 115200 8N1 to the Central Computer |
| `IWDG` | Watchdog | Independent clock source, survives a main-clock fault |
| Internal Flash | Configuration | Loaded at boot; defaults written on first boot |

**Toolchain:** STM32CubeIDE / CubeMX for peripheral configuration, HAL for
drivers, FreeRTOS (CMSIS-RTOS v2) for scheduling, FatFS for the SD card.

**Design discipline:** opaque-struct ADTs per module, no module touches a
HAL peripheral API except its owner (Communication is the only module that
sees UART), and every ISR does the least possible work — clear a flag and
hand off to a task — so no protocol or file-system logic ever runs in
interrupt context.

---

## 2. Central Computer (C / C++, Linux)

The hub for one submarine's LNC. Reusable across submarine types because
its own logic never assumes anything about the transport underneath it.

| Module | Responsibility |
|---|---|
| **Communication** | TLV send/receive over a swappable `Transport`; routes incoming frames by tag |
| **Management Command** | Builds and sends configuration/time commands to the LNC |
| **Log** | Prints and persists LNC-side logs |
| **Data Collection & Analysis** | Stores measurements and events, answers time-range queries, keeps one week of history |
| **GS Link** (`gsLink/`) | TCP server answering the Ground Station's log/event queries straight from Data Collection & Analysis |

**Transport is a config detail, not a design choice baked into the code.**
`Communication` is written against an abstract `Transport` interface
(`Shared/Net/transport.h`) — it never knows or cares whether the bytes are
travelling over a real serial port or a TCP socket:

- **UART** — `SerialTransport` wraps the hardware serial port directly.
- **Ethernet** — `TcpTransport` connects instead to a small standalone
  **gateway** process (`CentralComputer/src/gateway/`) that owns the real
  UART port and relays raw bytes over TCP, with zero protocol awareness of
  its own. This exercises the real Ethernet code path end-to-end even
  though the board itself only ever has UART.

Same pattern on the Ground Station side of the Central Computer: `GsLinkServer`
is a plain TCP server that decodes GS queries and answers from stored data —
a genuinely different protocol from the LNC link, sharing only the
transport-layer TCP code, not the TLV routing.

**Toolchain:** C++17, POSIX sockets/termios, a plain `makefile` per module,
built with `-Wall -Wextra -Wpedantic`. The TLV codec itself is C, compiled
unmodified into this C++ program to guarantee the wire format can never
drift between the two languages.

---

## 3. Ground Station (C++, Linux) — Fleet Management

The operator-facing program, and the object-oriented core of the project. It
manages a fleet of submarines of different types and, for the ones with a
real Central Computer, fetches their stored history over the network.

### Class design

```
                 Submarine (abstract base)
              serialNumber, name, assignment,
               received messages (+ sender)
                    /                \
   ResearchSubmarine              CombatSubmarine
   researchers, topic          mission, commander, crew size,
                                linked submarines, mission history
                                        |
                                  owns a CentralComputer
                                  (the Part-B program above,
                                   reached over Ethernet)
```

- **`FleetManager`** is the single manager class holding every submarine
  (`std::vector<std::unique_ptr<Submarine>>`) — no other class owns fleet
  state, and no class reads user input directly; `main.cpp` is the only
  place that touches `std::cin`.
- Every mutator returns `bool` (`setName`, `assignMission`, `endMission`, …)
  instead of throwing, so the menu can report success/failure uniformly.
- `CombatSubmarine` composes a `CentralComputer` handle. Asking the fleet
  for "submarine 4213's events from yesterday" resolves to: find the
  submarine → go through its `CentralComputer` → open a TCP connection to
  its `GsLinkServer` → stream the matching records back.

### Menu

Add a submarine · list the fleet · find by serial number · assign / update /
end a mission · link combat submarines into the same mission · send a
message between them · view a submarine's received messages (with sender) ·
exit.

**Toolchain:** C++17, STL containers (`vector`, `string`, `optional`),
`std::unique_ptr` for ownership, no raw `new`/`delete` anywhere in the fleet
model.

---

## Building & running

```bash
# Fast, no-hardware protocol tests
cd Shared/ProtocolTLV && make && make test

# PC programs
cd CentralComputer && make
cd GroundStation   && make
```

Firmware is built and flashed from STM32CubeIDE on Windows (`LNC/`, with
`Shared/` added as an external source path). The board's USB is handed
between Windows and WSL with `usbipd` (`stm win` / `stm linux`) since only
one OS can hold it at a time.

```bash
./CentralComputer/build/central_computer /dev/ttyACM0   # or --tcp host:port via the gateway
./GroundStation/build/ground_station
```

---

## Why it's built this way

- **Transport-independence is enforced by an interface, not a convention.**
  Every module above Communication calls a transport-agnostic API; UART and
  Ethernet are two implementations of the same abstract class, selected once
  at construction.
- **One protocol implementation, shared by every endpoint.** `Shared/ProtocolTLV`
  is plain, dependency-free C99 so it compiles identically into ARM firmware
  and x86 Linux binaries — a wire-format bug fixed once is fixed everywhere.
- **The gateway is a dumb pipe on purpose.** It relays bytes with no framing
  or parsing, so TLV encode/decode only ever happens at the two real
  endpoints — adding a hop never means adding a place the protocol can drift.
