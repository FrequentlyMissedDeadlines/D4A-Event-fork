# Web UI User Guide

The K10 Bot hosts a web interface at `http://<bot-ip>/`.  
Open it from any browser on the same WiFi network — no installation needed.

---

## Navigation bar

Every page shares the same top navigation bar:

| Link | Page | Purpose |
|---|---|---|
|  🛜 Wifi | `index.html` | Register master, configure WiFi |
| 📷 Live Cam | `camera.html` | Snapshot or live camera stream |
| 📚 JS Doc | `BotScriptUserGuide.html` | JavaScript scripting reference (opens in new tab) |
| 🎮 JS Play | `BotScript.html` | Write and run control scripts |
| 🛠 Build infos | `buildinfo.html` | Firmware/filesystem version info |

---

##  🛜 Wifi — Settings page (`index.html`)

The entry point. Use it to claim control of the bot and change its WiFi credentials.

### I. Get control

Before any servo or motor command works, one controller must register as **master**.

1. Look at the bot's TFT screen — the 5-character token is shown on the **App Info** screen (screen 1).
2. Type the token into the **Token** field.
3. Click **Register as Master**.  
   The status box turns green on success.
4. Click **Unregister** when finished to release the bot for other controllers.

> Only one master is allowed at a time. A second registration attempt will fail until the current master unregisters or the heartbeat times out (50 ms without a heartbeat → automatic release).

### II. Update settings

Load, change, and save WiFi credentials stored in the bot's non-volatile memory.

| Button | Action |
|---|---|
| **Load Settings** | Reads current SSID and password from the board into the fields |
| **Save Settings** | Writes the fields to the board (master required) |
| **Reset board settings to Default** | Clears NVS credentials and restores factory defaults (master required) |

> WiFi changes take effect on next reboot.

---

## 📷 Live Cam — Camera page (`camera.html`)

View the onboard camera from your browser.

| Mode button | Behaviour |
|---|---|
| **∅ Camera off** | No image fetch, lowest power |
| **📸 Snapshot** | Fetches a single JPEG from `/cam/snapshot` |
| **🎥 Live Stream** | Streams MJPEG from `/cam/stream` (uses more bandwidth) |

In snapshot mode two extra buttons appear:
- **📸 Capture New Snapshot** — fetches a fresh frame.
- **💾 Download Image** — saves the current frame to your device.

---

## 🎮 JS Play — Script Runner (`BotScript.html`)

The main control interface. Write JavaScript that calls built-in functions to move servos, react to keyboard or gamepad input, and automate behaviour.

### Control Panel  *(collapsed by default — click the header to expand)*

| Field / Button | Purpose |
|---|---|
| **Bot IP / WebSocket Port** | Auto-filled from the page URL; shown read-only |
| **Master Token** | 5-char token from the bot's screen |
| **🔐 Register as Master** | Establishes master control and starts the heartbeat |
| **🔓 Unregister** | Releases master control |
| **Bot Name** + **📝 Set Bot Name** | Rename the bot (persisted, master required) |
| **🔍 Detect Controller** | Scans for a connected USB/Bluetooth gamepad |
| **🎮 Show / Hide Gamepad** | Toggles an SVG overlay that lights up buttons in real time |

**Status indicators** (read-only):

| Indicator | Meaning |
|---|---|
| Connected | WebSocket to the bot is open |
| Registered | This browser tab holds master control |
| Heartbeat | Green = keep-alive running; red = stopped |
| Heartbeat avg response time | Round-trip latency of heartbeat packets |
| Controller Status / Name | Gamepad connection state |

### Script Editor

A dark-theme `<textarea>` where you write JavaScript.  
The code has access to all functions described in the [JavaScript User Guide](../BotScriptUserGuide.html).

| Button | Action |
|---|---|
| **▶️ Run script** | Evaluates the editor content immediately |
| **⬇️ Download** | Saves the editor content as a `.js` file |
| **⬆️ Upload** | Loads a `.js` or `.txt` file from disk into the editor |

**Output console** — messages from `_scriptLog()` and runtime errors appear here.

### Persistence

**Browser FS** (stored in `localStorage`):

| Control | Action |
|---|---|
| Script name field | Name used when saving |
| **💾 Save** | Saves current editor content under that name |
| **🗑️ Delete** | Removes the selected saved script |
| Dropdown | Selecting an entry loads it into the editor |

**Board examples** (stored on the bot's SPIFFS filesystem under `/scripts/`):

| Control | Action |
|---|---|
| **🔄 Refresh** | Fetches the list of scripts from the board |
| Dropdown | Selecting an entry previews it |
| **📥 Load** | Copies the selected board script into the editor |

### K10 Display

Remotely cycle the bot's TFT screen without touching the hardware.

| Button | Equivalent |
|---|---|
| **Next screen** | Same as pressing Button A on the K10 |
| **Previous screen** | Goes back one screen |

### Emergency

| Button | Effect |
|---|---|
| **🛑 Emergency reboot** | Immediately reboots the K10 board (master required). Stops all motion. |

---

## 🛠 Build infos — Firmware info (`buildinfo.html`)

Displays the git branch, commit SHA, build timestamp, and file counts for both the **running firmware** and the **mounted filesystem package**.

A one-line summary at the top tells you whether the two match. A mismatch usually means `pio run --target upload` and `pio run --target uploadfs` were run from different source states.

---

## Typical first-use workflow

```
1. Power on the bot → it connects to WiFi and shows its IP on the TFT screen.
2. Open http://<bot-ip>/ in your browser.
3.  🛜 Wifi page → enter the token → Register as Master.
4. 🎮 JS Play page → expand Control Panel → register again with the token
      (each browser tab registers independently).
5. Paste or type a script → ▶️ Run script.
6. Use keyboard / gamepad to control the bot in real time.
7. When done → 🔓 Unregister.
```

---

*See also: [architecture.md](architecture.md) · [quickstart.md](quickstart.md) · [BotScriptUserGuide](../BotScriptUserGuide.html)*
