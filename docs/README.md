# K10 Bot — Documentation Index

## 📦 Participant guides (served on the device)

All documentation for contest participants is served directly by the bot at  
`http://<device-ip>/help/` and lives under [`data/www/help/`](../data/www/help/).

| Page | Purpose |
|------|---------|
| [🏆 D4A Contest](../data/www/help/D4A%20contest%20guide.md) | Contest rules, scoring, hardware overview |
| [⚡ Quickstart](../data/www/help/quickstart.md) | Register, heartbeat, control servos in minutes |
| [🌐 Web UI](../data/www/help/web-ui.md) | All five web pages explained |
| [📟 Board UI](../data/www/help/board-ui.md) | TFT screens, Button A, LED indicators |
| [📡 Communication](../data/www/help/communication.md) | UDP / WebSocket / HTTP transports |
| [🔢 Binary Protocol](../data/www/help/binary-protocol.md) | Full binary frame reference (all services) |
| [🏗 Architecture](../data/www/help/architecture.md) | High-level code architecture overview |

JS scripting API: the **📚 JS Doc** tab in the web UI (`BotScriptUserGuide.html`).

---

## 👤 Expert user guides

| Guide | Purpose |
|-------|---------|
| [WiFiService](user%20guides/WiFiService.md) | STA / AP mode, credentials NVS, hostname generation |
| [Python UDP Client](../example-clients/python/README.md) | TUI controller — setup, keyboard/joystick input, calibration wizard |}

---
cd 
## 🔧 Contributor guides

| Guide | Purpose |
|-------|---------|
| [AmakerBotService protocol](contributor%20guides/AmakerBotService_protocol.md) | Master registration, heartbeat, dispatch internals |
| [DFR1216Service](contributor%20guides/DFR1216Service.md) | Motor/LED expansion board driver |
| [IsServiceInterface](contributor%20guides/IsServiceInterface.md) | How to write a new service |
| [RollingLogger](contributor%20guides/RollingLogger.md) | Logging system internals |
| [UDPServiceHandlers](contributor%20guides/UDPServiceHandlers.md) | UDP message handler registration |
| [Technical infos](contributor%20guides/technical%20infos/) | Camera, GC2145 datasheet notes |
