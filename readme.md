# 🦁 WildLensAI

![Platform](https://img.shields.io/badge/platform-ESP32-blue?style=flat-square)
![Protocol](https://img.shields.io/badge/protocol-ESP--NOW-green?style=flat-square)
![Language](https://img.shields.io/badge/language-C%2B%2B%20%2F%20Arduino-orange?style=flat-square)
![Network](https://img.shields.io/badge/network-Mesh-purple?style=flat-square)
![Use Case](https://img.shields.io/badge/use--case-Wildlife%20Monitoring-teal?)

> A distributed mesh network of ESP32 devices for wildlife motion detection and data relay — built for the wild, designed to scale.

---

## 🌿 Overview

**WildLensAI** enables a mesh network of ESP32 nodes to perform distributed motion detection and data relay. Each node runs identical firmware and can either **originate** a detection event or **relay** it across the network — making the system robust, scalable, and loop-safe.

Key capabilities:
- **Motion Detection** — triggered via serial input and GPIO pulse
- **Mesh Relay** — packets are forwarded peer-to-peer using ESP-NOW, with loop prevention and relay depth limiting
- **Raspberry Pi Integration** — nodes broadcast data after receiving a response from a connected Pi
- **Non-blocking Operation** — finite state machine driven by `millis()` keeps every node responsive

Ideal for wildlife monitoring, distributed IoT sensor grids, and remote sensing deployments.

---

## 📁 Folder Structure

```
wildlensAI/
├── esp_motion_detection/       # Core ESP32 firmware
├── edgeai/                       # Edge AI - Capture + Detection
└── README.md
```

> The firmware is self-contained. Each ESP32 node runs the same `.ino` file — topology is configured by setting peer MAC addresses directly in the code.

---

## ⚙️ How It Works

```
[Motion Trigger]
      │
      ▼
[ESP32 Node] ──ESP-NOW──► [Peer Node] ──► [Peer Node] ──► ...
      │
      ▼
[Raspberry Pi] ── response ──► [Broadcast to mesh]
```

1. A node detects motion (via serial or GPIO).
2. It sends a packet to the connected Raspberry Pi and waits for a response.
3. On response, the node broadcasts the data across the mesh via ESP-NOW.
4. Peer nodes relay the packet further, enforcing depth limits to prevent infinite loops.

---

## 🚀 Getting Started

### Requirements
- ESP32 development board(s)
- Arduino IDE with ESP32 board support
- Raspberry Pi (optional, for AI processing)
- ESP-NOW enabled (built into ESP32)

### Setup
1. Clone the repo:
   ```bash
   git clone https://github.com/jai2992/wildlensAI.git
   ```
2. Open `esp_motion_detection.ino` in Arduino IDE.
3. Set the MAC addresses of your peer nodes in the config section.
4. Flash to all ESP32 devices.

---

## 📡 Tech Stack

| Component | Details |
|-----------|---------|
| Microcontroller | ESP32 |
| Wireless Protocol | ESP-NOW |
| Firmware Language | C++ / Arduino |
| Edge Compute | Raspberry Pi |
| Network Topology | Mesh (configurable peers) |

---

## 🤝 Contributing

Pull requests are welcome! For major changes, please open an issue first to discuss what you'd like to change.

---

## 📄 License

[MIT](LICENSE)