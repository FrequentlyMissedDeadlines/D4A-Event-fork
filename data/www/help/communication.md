# Communication Guide

The K10 Bot exposes four independent transports that all speak the **same binary frame protocol**. This guide describes each transport's characteristics, behaviour, and usage patterns.

---

## Overview

```
Controller  ──────────────────────────────────┐
  │  UDP :24642         (Core 0, max priority) │
  │  WebSocket :81/ws   (Core 0, max priority) │  →  AmakerBotService → service handlers
  │  HTTP :80/botserver (Core 1, normal prio)  │
  │  BLE NUS            (setup(), NimBLE task)  │
Controller  ──────────────────────────────────┘
```

| | UDP | WebSocket | HTTP | BLE (NUS) |
|---|---|---|---|---|
| **Port** | 24642 | 81 | 80 | — (GATT) |
| **Endpoint** | — | `/ws` | `/botserver?cmd=<hex>` | Nordic UART Service |
| **Protocol** | Binary | Binary | Hex-encoded GET | Binary |
| **Connection** | Connectionless | Persistent | One request per frame | Persistent (GATT) |
| **Reply** | Same source port | Same client only | HTTP response body | Notify characteristic |
| **FreeRTOS core** | 0 (max priority) | 0 (max priority) | 1 (normal priority) | NimBLE host task |
| **Multiple clients** | Yes (last sender wins) | Yes | Yes | One at a time |
| **Best for** | Real-time, Python scripts | Web UI, BotScript | Debugging, curl | No-WiFi, mobile apps |

---

## Sender identity and master control

Master registration is **per-sender-IP**.  
Each transport extracts the sender's IP and passes it to `AmakerBotService::dispatch()`:

- **UDP** — `packet.remoteIP()` (the source IP of the UDP datagram)
- **WebSocket** — `client->remoteIP()` (the TCP connection's remote IP)
- **HTTP** — the HTTP client's remote IP

Only one IP can hold master control at a time. A second `REGISTER` from a *different* IP fails with `resp_operation_failed (0x03)` until the current master unregisters or its heartbeat times out.

---

## Transport 1 — UDP

### Characteristics

- **Library**: `AsyncUDP` (ESP-IDF)
- **Core**: 0 at maximum FreeRTOS priority — lowest possible latency
- **Delivery**: fire-and-forget, no connection state
- **Reply**: sent back to the source IP + source port of the incoming datagram
- **Heartbeat**: bot bot-side timeout is **50 ms**; send every ≤ 30 ms to be safe

### Frame exchange

```
Controller                          Bot (Core 0)
    │── [binary frame] ──────────────►│
    │◄─ [response] ───────────────────│  (only if response is non-empty)
```

Heartbeat (`0x43`) sends no response at all — this is intentional to keep latency minimal.

### Python snippet

```python
import socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(0.5)

# Send raw binary
sock.sendto(bytes([0x41, 0x44, 0x34, 0x41, 0x41, 0x41]), ("192.168.1.100", 24642))

# Read response
data, _ = sock.recvfrom(64)
print(hex(data[0]), hex(data[1]))  # action echo, response code
```

### Notes

- No connection handshake — the first packet can be the `REGISTER` frame
- If the router does NAT, the bot replies to the *external* port of the sender; ensure your firewall does not block it
- Multiple controllers can send UDP packets; whichever registers first holds master control

---

## Transport 2 — WebSocket

### Characteristics

- **Library**: `ESPAsyncWebServer` + `AsyncWebSocket`
- **Core**: 0 at maximum FreeRTOS priority (separate `AsyncWebServer` instance, **not** shared with the HTTP server on port 80)
- **Endpoint**: `ws://<bot-ip>:81/ws`
- **Frame type**: binary (text frames are silently ignored)
- **Fragmented frames**: dropped — bot messages are small enough that fragmentation never occurs in practice
- **Multiple clients**: supported; reply goes only to the client that sent the frame
- **Client cleanup**: stale connections are freed by `cleanupClients()`, called from the display task every ~500 ms

### Connection lifecycle

```
Client                              Bot (Core 0)
    │── TCP connect ─────────────────►│  WS_EVT_CONNECT logged
    │── WS upgrade ──────────────────►│
    │── [binary frame] ──────────────►│  dispatch() called synchronously
    │◄─ [binary response] ────────────│  same client only
    │   …                             │
    │── TCP close ────────────────────►│  WS_EVT_DISCONNECT logged
```

If the connection drops while master is registered, the heartbeat watchdog fires within 50 ms and calls the emergency-stop callback.

### Web UI behaviour (BotScript.js)

The web UI manages the WebSocket automatically:

| Constant | Value | Effect |
|---|---|---|
| `HEARTBEAT_INTERVAL_MS` | **40 ms** | `setInterval` sends `0x43` every 40 ms after registration |
| Connection timeout | 5 s | Socket closed if `onopen` not received in time |
| Auto-reconnect delay | 3 s | Reconnects automatically if session is live (master registered) and the connection drops |

Two send modes are used internally:

| Function | Behaviour | Used for |
|---|---|---|
| `sendWSPacket(data)` | `async/await` — waits for the matching response | Registration, name queries, angle/speed GET commands |
| `sendWSFireAndForget(data)` | Non-blocking — returns immediately | Heartbeat, real-time servo control (gamepad / keyboard) |

The fire-and-forget path is essential for gamepad control: the gamepad polls every 50 ms, and waiting for a response on each servo command would create a backlog that makes control feel laggy.

### Python snippet

```python
import websocket  # pip install websocket-client
ws = websocket.create_connection("ws://192.168.1.100:81/ws")
ws.send_binary(bytes([0x41, 0x44, 0x34, 0x41, 0x41, 0x41]))  # REGISTER "D4AAA"
resp = list(ws.recv())
print(hex(resp[0]), hex(resp[1]))  # action echo, response code
ws.close()
```

### Notes

- The bot's WebSocket server is on port **81**, completely separate from the HTTP server on port 80 — they are two different `AsyncWebServer` instances on two different ports
- Sending a text frame (instead of binary) is silently ignored — always use binary frames
- `broadcast()` is available server-side to push unsolicited data to all connected clients (not currently used by the firmware but available for custom extensions)

---

## Transport 3 — HTTP (`/botserver`)

### Characteristics

- **Library**: `ESPAsyncWebServer`
- **Core**: 1 at normal priority
- **Method**: `GET /botserver?cmd=<hex>`
- **Frame encoding**: the binary frame bytes are hex-encoded as a continuous string with no separators (e.g. `413030303030` for `[0x41, 0x30, 0x30, 0x30, 0x30, 0x30]`)
- **Response encoding**: raw binary, `application/octet-stream`

### HTTP status codes

| Situation | HTTP status | Body |
|---|---|---|
| Frame dispatched, non-empty response | **200** | Raw binary response |
| Frame dispatched, empty response (e.g. heartbeat) | **204** | Empty |
| Missing `cmd` parameter | **400** | Plain text error message |
| Invalid hex string in `cmd` | **400** | Plain text error message |

### Static file serving

The HTTP server also serves all files under `/www` on LittleFS as the site root (`/`):

| Path | Source |
|---|---|
| `http://<bot-ip>/` | `/www/index.html` |
| `http://<bot-ip>/BotScript.html` | `/www/BotScript.html` |
| `http://<bot-ip>/buildinfo.json` | Runtime-generated JSON |
| `http://<bot-ip>/cam/snapshot` | Live JPEG from camera |
| `http://<bot-ip>/cam/stream` | MJPEG stream |
| `http://<bot-ip>/scripts/` | Script CRUD API (GET list, GET/PUT/DELETE item) |

Static files are served with a **1-day cache** (`max-age=86400`). `buildinfo.json` and `/scripts/` use `no-store` to always reflect current state.

### curl examples

```bash
# REGISTER with token "D4AAA" → hex: 41 44 34 41 41 41
curl -s http://192.168.1.100/botserver?cmd=41443441 4141 | xxd
# Expected: 41 00  (action echo + resp_ok)

# HEARTBEAT → hex: 43 (no response body → HTTP 204)
curl -s -o /dev/null -w "%{http_code}" http://192.168.1.100/botserver?cmd=43

# SET_SERVOS_SPEED servo 0, speed +100 → hex: 23 01 64
curl -s http://192.168.1.100/botserver?cmd=230164 | xxd

# GET_BATTERY → hex: 29 (MotorServoService service_id 0x02, cmd 0x09)
curl -s http://192.168.1.100/botserver?cmd=29 | xxd
# Expected: 29 00 <level_byte>
```

### Notes

- HTTP has no persistent connection — the sender IP is the HTTP client IP, so `REGISTER` via HTTP records the browser/script IP as master
- Because HTTP is on Core 1 (normal priority), heavy web UI traffic does not interfere with real-time UDP / WebSocket control on Core 0
- The script management API (`/scripts/`) supports `GET` (list or fetch), `PUT` (save), and `DELETE` via standard HTTP verbs

---

## Transport 4 — BLE (Nordic UART Service)

### Characteristics

- **Library**: NimBLE-Arduino
- **Profile**: Nordic UART Service (NUS) — the de-facto BLE UART standard
- **Service UUID**: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- **RX characteristic** (write from client): `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
- **TX characteristic** (notify to client): `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`
- **Advertised name**: same as the bot's WiFi hostname (e.g. `K10-XXXX`)
- **Initialised in**: `setup()` (not inside a FreeRTOS task — see notes below)
- **Clients**: one BLE central at a time

### Frame exchange

```
BLE Central (phone / laptop)         Bot
    │── write NUS RX ────────────────►│  dispatch() called
    │◄─ notify NUS TX ────────────────│  response (if non-empty)
```

The binary frame format is identical to UDP and WebSocket: byte 0 is the action byte, followed by payload bytes.

### Sender identity

BLE clients do not have an IP address. The BLE transport uses a fixed virtual IP (`0.0.0.0`) when calling `dispatch()`. This means a BLE master and a WiFi master are distinguished by IP, so BLE can coexist with WiFi transports.

### Notes

- The ESP32-S3 does **not** support Bluetooth Classic / SPP — only BLE is available
- `NimBLEDevice::init()` blocks until the NimBLE host task starts; it **must** run in `setup()`, not in a max-priority FreeRTOS task (which would starve the NimBLE host task and cause a watchdog reboot)
- BLE is always active alongside WiFi — no configuration needed
- Use any BLE UART app (e.g. nRF Connect, Serial Bluetooth Terminal) to send raw binary frames
- Diagnostics: `getRxCount()`, `getTxCount()`, `getDroppedCount()` are available on `BotServerBLE` just like the other transports

---

## Concurrent use

All four transports can be active simultaneously:

- A Python UDP controller can register as master and send heartbeats at 30 ms
- Browser tabs can connect to the WebSocket for monitoring without being master
- curl commands can read status (e.g. `GET_BATTERY`, `GET_NAME`) from any IP without registering

Only **master-protected commands** (motor/servo write, WiFi change, reboot) enforce the single-master rule. Read commands (`GET_NAME`, `GET_BATTERY`, `PING`) work from any sender without registration.

---

## Diagnostics

All four servers expose live counters accessible from the **App Info** screen (Screen 1) on the TFT display and via `getRxCount()` / `getTxCount()` / `getDroppedCount()` in C++:

| Counter | Meaning |
|---|---|
| `#in` | Frames/requests successfully dispatched |
| `#out` | Responses sent |
| `#drop` | Frames rejected (zero-length, bad hex, fragmented WS frame) |

---

*See also: [binary-protocol.md](binary-protocol.md) · [quickstart.md](quickstart.md) · [architecture.md](architecture.md)*
