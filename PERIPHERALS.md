# LNC Embedded System — Summary

Snapshot as of 2026-09-09. What's actually in the firmware: peripherals,
pins, FreeRTOS architecture, and protocols. See `CLAUDE.md` section 9 for
the full design rationale behind each choice.

## 1. Peripherals & pins

| Peripheral | Pin(s) | Sensor / purpose | Notes |
|---|---|---|---|
| `ADC1` | `PA0` | Battery (potentiometer) | 12-bit, on-demand poll, Monitor only |
| `ADC2` | `PA1` | Light (LDR) | 8-bit, on-demand poll, Monitor only |
| `PB5` (bit-banged) | `PB5` | Temp + humidity (DHT11) | Single-wire, `TIM2` for µs timing |
| EXTI (`PB10`) | `PB10` | IR receiver (VS1838B) — Object Detection stand-in | Both-edge, shared `EXTI15_10` line |
| EXTI (`PB3`) | `PB3` | Stop-alarm button | Own vector; debounced in software (50ms tick compare) |
| `I2C3` | `PC0`(SCL)/`PC1`(SDA) | DS1307 external RTC | Durable time source, synced to internal RTC at boot + on time sync |
| `RTC` (internal) | — | All runtime timestamps | Working clock; `LSI`-derived (known drift issue, `LSE` not enabled) |
| `SPI1` | `PA5`/`PA6`/`PA7`, CS `PB6` | SD card (FatFS) | Log files + `EVENTS.TXT` |
| `USART2` | `PA2`(TX)/`PA3`(RX) | Communication (Central Computer link) | Only module allowed to touch it |
| `IWDG` | — | Watchdog | ~4.1s timeout, 1s refresh |
| Flash (last page, bank 2) | — | Configuration persistence | Loaded at boot; defaults written if empty |
| RGB LED | `PB13`(R)/`PB14`(B)/`PB15`(G) | Mode-status LED | Plain GPIO, no PWM |

### Timers

| Timer | Owner | Role | Config |
|---|---|---|---|
| `TIM2` | DHT11 (read by Object Detection too) | Free-running 1µs counter | Prescaler=79 → 1MHz |
| `TIM3` | Buzzer | PWM tone (`CH1`/`PB4`) + note timing via its own update-IT | Alarm siren + sonar ping, per-note Period/Pulse |
| `TIM5` | Object Detection | Base+IT, 10s presence-timeout countdown | Prescaler=7999, Period=99999 → 10.000s |
| `TIM8` | SonarLed | PWM only (`CH4`/`PC9`), breathing red LED | Duty nudged from task every 30ms, no IT |
| `TIM6` | — | Reserved, unused | Init'd by CubeMX, never wired up |

Monitor / Keep-Alive / Watchdog use `osDelayUntil` in their own task loop
instead of a hardware timer — no peripheral needed for periodic timing.

## 2. FreeRTOS architecture

**8 threads created, 7 real.** `StartDefaultTask` is CubeMX's unused
default stub (`osDelay(1)` forever). Event, Log, Configuration, and Init
have **no task of their own** — they run as plain function calls on
whichever task invokes them (Init runs in `main()` before
`osKernelStart()`; Event/Log run inside Monitor's, Object Detection's, or
Communication's task).

| Task | Module | Trigger | Stack | Priority |
|---|---|---|---|---|
| `monitor_task` | Monitor | `osDelayUntil`, 5s | 4096 B | Normal |
| `comm_tx_task` | Communication | Semaphore-gated send loop | 1024 B | AboveNormal |
| `comm_rx_task` | Communication | Byte queue, blocking | 4096 B | AboveNormal |
| `objdet_task` | Object Detection | Task flags (EXTI/timer ISR) | 4096 B | Normal |
| `sonarled_task` | Event (SonarLed) | `osDelayUntil`, 30ms | 1024 B | Normal |
| `keepalive_task` | Keep-Alive | `osDelayUntil`, 6s | 2048 B | Normal |
| `watchdog_task` | Watchdog | `osDelayUntil`, 1s | 1024 B | Normal |

### Synchronization / IPC tools

| Tool | Used? | Where / why |
|---|---|---|
| Message queues | **Yes** (4) | Communication: `txq_high`(1)/`txq_med`(4)/`txq_low`(4) — the keep-alive>events>data priority scheme; `rxq_bytes`(128) — decouples UART ISR from `comm_rx_task` |
| Counting semaphore | **Yes** (1) | `sem_tx_ready` — wakes `comm_tx_task` when any queue gets an item |
| Mutex | **Yes** (2) | `g_monitor.latest_lock` — guards Monitor's cross-task latest-reading cache (read by Keep-Alive); `s_sd_lock` — guards `sdfatfs.c`'s shared FatFS state (now called from multiple tasks) |
| Task notifications (`osThreadFlags`) | **Yes** | Object Detection only — `OBJDET_FLAG_EDGE`/`OBJDET_FLAG_TIMEOUT`, set from the EXTI and `TIM5` ISRs, woken in `objdet_task` (ISR can't touch the SD card itself) |
| Event flags (`osEventFlags`) | No | — |
| Software timers (`osTimer`) | No | Object Detection's presence timeout was originally planned as one; ended up as a dedicated hardware timer (`TIM5`) + task notification instead |

**ISR discipline:** every ISR/HAL callback does the minimum (clear flag,
enqueue, or set a task flag) — no SD-card access or protocol logic ever
runs in interrupt context. Exception that's still safe: the button's
`event_button_pressed()` runs directly in the shared `HAL_GPIO_EXTI_Callback`,
but it only stops the buzzer (register writes) — no blocking, no SD access.

## 3. Protocols

| Link | Protocol | Notes |
|---|---|---|
| LNC ↔ Central Computer | **TLV** over UART (115200 8N1) | `Shared/ProtocolTLV`; byte-streamed via `tlv_receiver_feed_byte()` |
| LNC ↔ DS1307 | I2C (master) | `HAL_I2C_Master_Transmit/Receive`, blocking |
| LNC ↔ SD card | SPI | FatFS `user_diskio_spi.c`, blocking transfers |
| LNC ↔ DHT11 | Single-wire, bit-banged | No hardware protocol peripheral — polled via `TIM2` |
| IR remote → Object Detection | Heuristic edge-timing filter, not real NEC decode | Speed rejection + burst confirmation (see `CLAUDE.md` section 7) |
