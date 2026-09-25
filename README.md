# Submarine Monitoring System

This project monitors a fleet of submarines. It has three parts: firmware on an
STM32 board that reads the sensors, a Linux service that collects the board's
data, and a Linux program that manages the whole fleet from the ground.

```
Ground Station  <--- Ethernet --->  Central Computer  <--- UART / Ethernet --->  LNC (STM32)
```

- **LNC (Local Node Controller)**: C firmware running on the sensor board.
- **Central Computer**: a C/C++ Linux service. It talks to one LNC, stores its
  data, and answers queries from the Ground Station.
- **Ground Station**: a C++ Linux program. It manages the fleet (research and
  combat submarines, their missions, and messages between them) and can request
  stored data from a submarine's Central Computer.

All links use the same binary TLV (Tag-Length-Value) protocol. It is written
once in `Shared/` and compiled as-is into all three programs.

## Repository layout

| Folder | Language | Runs on | Contents |
|---|---|---|---|
| `LNC/` | C | STM32 Nucleo-L476RG | Firmware, 9 modules on FreeRTOS |
| `CentralComputer/` | C / C++ | Linux | Communication, commands, storage, gateway |
| `GroundStation/` | C++ | Linux | Fleet management and queries to a Central Computer |
| `Shared/ProtocolTLV/` | C | All | TLV protocol implementation |
| `Shared/Net/` | C++ | Linux | `Transport` interface and TCP implementation, used by CC and GS |

Since the protocol lives in one place, the firmware and the two Linux programs
always use the same wire format. A protocol change is made in one commit.

## 1. LNC (C, STM32 Nucleo-L476RG, FreeRTOS)

The firmware has nine modules. Each one is a FreeRTOS task or a set of
functions with a single job. Modules communicate only through queues, mutexes
and the communication layer. No module accesses another module's internal
state.

| Module | Responsibility |
|---|---|
| Monitor | Samples temperature, humidity, light and battery every 5 s, classifies the reading as Normal, Warning or Error, and logs it |
| Object Detection | Filters noisy edges from an IR receiver into "object detected" and "object cleared" events |
| Event | Timestamps events, drives the RGB LED and buzzer, writes `EVENTS.TXT`, and notifies the Central Computer |
| Log | Writes daily measurement logs and keeps the last 7 days |
| Communication | Owns the UART link, builds and parses TLV frames, and sends by priority (keep-alive, then events, then data) |
| Configuration | Holds all configurable limits and saves them to internal Flash |
| Init | Syncs time with the Central Computer at boot, then starts the other modules |
| Keep-Alive | Sends a heartbeat with the latest reading every 6 s |
| Watchdog | Refreshes the IWDG and reports if the last reset was caused by the watchdog |

### Peripherals

| Peripheral | Used for | Notes |
|---|---|---|
| `ADC1` / `ADC2` | Battery (potentiometer), light (LDR) | Separate channels, polled on demand |
| `TIM2` | Free-running 1 µs counter | Timing for the DHT11 single-wire protocol |
| `TIM3` | Buzzer PWM and note sequencing | Used for both the alarm and the object-detection tone |
| `TIM5` | Object-detection timeout | Reset on every valid IR edge |
| `TIM8` | Red LED "breathing" effect | PWM duty cycle updated from a task |
| `I2C3` | DS1307 external RTC | Battery-backed reference time; the internal RTC is used at runtime |
| `SPI1` | microSD card (FatFS) | Daily logs and the events file |
| `USART2` | Communication module | 115200 8N1 to the Central Computer |
| `IWDG` | Watchdog | Runs on its own clock source |
| Internal Flash | Configuration | Loaded at boot; defaults are written on first boot |

**Tools:** STM32CubeIDE / CubeMX, HAL, FreeRTOS (CMSIS-RTOS v2), FatFS.

**Design rules:**
- Each module is an ADT with an opaque struct.
- Only the owning module calls a peripheral's HAL functions. For example, only
  Communication uses the UART.
- ISRs only set a flag or pass data to a task. Protocol and file-system code
  never runs in interrupt context.

## 2. Central Computer (C / C++, Linux)

The Central Computer serves one submarine's LNC. Its logic does not depend on
the transport, so it can be reused with different submarine setups.

| Module | Responsibility |
|---|---|
| Communication | Sends and receives TLV frames over a `Transport` and routes incoming frames by tag |
| Management Command | Builds and sends configuration and time commands to the LNC |
| Log | Prints and saves LNC logs |
| Data Collection & Analysis | Stores measurements and events, answers time-range queries, keeps one week of history |
| GS Link (`gsLink/`) | TCP server that answers Ground Station queries from the stored data |

### Transport

`Communication` uses the abstract `Transport` interface in
`Shared/Net/transport.h`. It does not know whether data goes over a serial port
or a TCP socket. The transport is chosen once, at startup.

- **UART**: `SerialTransport` opens the serial port directly.
- **Ethernet**: `TcpTransport` connects to a small gateway process
  (`CentralComputer/src/gateway/`). The gateway owns the real UART port and
  forwards raw bytes over TCP. It does not parse the protocol. This lets me
  test the Ethernet path end to end, even though the board only has UART.

`GsLinkServer` is a separate TCP server for the Ground Station. It uses a
different query protocol than the LNC link. The two share the TCP transport
code, but not the TLV routing.

**Tools:** C++17, POSIX sockets and termios, one `makefile` per module, built
with `-Wall -Wextra -Wpedantic`. The TLV code is C and is compiled unchanged
into the C++ program, so the format is the same on both sides.

## 3. Ground Station (C++, Linux)

The Ground Station is the operator program and the main OOP part of the
project. It manages submarines of different types. For submarines that have a
Central Computer, it can fetch their stored history over the network.

### Class design

```
                 Submarine (abstract)
          serialNumber, name, assignment,
          received messages (with sender)
                /                \
   ResearchSubmarine          CombatSubmarine
   researchers, topic         mission, commander, crew size,
                              linked submarines, mission history
                                     |
                              has a CentralComputer
                              (connection to the Central
                               Computer over Ethernet)
```

- `FleetManager` holds all submarines in a
  `std::vector<std::unique_ptr<Submarine>>`. It is the only class that owns
  fleet data.
- Only `main.cpp` reads user input (`std::cin`). The classes do not.
- Mutators such as `setName`, `assignMission` and `endMission` return `bool`
  instead of throwing, so the menu handles success and failure the same way.
- `CombatSubmarine` holds a `CentralComputer` object. A request like
  "events from submarine 4213 yesterday" works like this: find the submarine,
  use its `CentralComputer` to connect to its `GsLinkServer` over TCP, and
  receive the matching records.

### Menu options

- Add a submarine
- List the fleet
- Find a submarine by serial number
- Assign, update or end a mission
- Link combat submarines to the same mission
- Send a message between submarines
- View a submarine's received messages
- Exit

**Tools:** C++17, STL (`vector`, `string`, `optional`), `std::unique_ptr` for
ownership. The fleet code has no raw `new` or `delete`.

## Building and running

```bash
# Protocol tests (no hardware needed)
cd Shared/ProtocolTLV && make && make test

# Linux programs
cd CentralComputer && make
cd GroundStation   && make
```

The firmware is built and flashed from STM32CubeIDE on Windows. Open `LNC/`
and add `Shared/` as an external source path. The board's USB can only be
attached to one OS at a time, so I switch it between Windows and WSL with
`usbipd` (`stm win` / `stm linux`).

```bash
./CentralComputer/build/central_computer /dev/ttyACM0   # or --tcp host:port through the gateway
./GroundStation/build/ground_station
```

## Design notes

- **Transport independence.** Code above the Communication module uses only the
  `Transport` interface. UART and Ethernet are two implementations of it.
- **One protocol implementation.** `Shared/ProtocolTLV` is plain C99 with no
  dependencies, so it builds the same way for the ARM firmware and for x86
  Linux. A bug fixed there is fixed in every program.
- **Simple gateway.** The gateway only forwards bytes. Encoding and decoding
  happen only at the two real endpoints, so adding the gateway does not create
  another place where the protocol can go out of sync.
