# ESP32-FanLamp-EspAlexa
Project to control one or more FanLamp fans using only Alexa and an ESP32.

# ESP32 BLE to Alexa Bridge (FanLamp / Mikomika / ZhiJia)

A robust, reverse-engineered ESP32 bridge that allows local control of BLE ceiling fans (commonly branded as Mikomika, FanLamp, or ZhiJia) using Amazon Alexa, without needing proprietary cloud apps.

This project was built to solve the unreliability of standard BLE replay attacks when dealing with these specific fan controllers in the 2.4 GHz band.

## 🛠️ Key Technical Discoveries (Why this works when others fail)

During the reverse-engineering of this protocol, we discovered several hardware and software bottlenecks that prevent simple Bluetooth replays from working correctly. This code is designed to overcome them:

### 1. The "Frame Burst" Anti-Replay Validation
These controllers do **not** accept single packets. If you sniff the remote and send a single payload, the fan will completely ignore it. 
* **The fix:** The remote actually sends a burst of **4 different packets** for every button press. The first 18 bytes are identical, but the tail (validation signature) changes. The fan requires all 4 packets in sequence to validate the command. This code uses multi-dimensional arrays to fire the exact 4-packet sequence required.

### 2. WiFi/BLE RF Coexistence (Antenna Collisions)
The ESP32 has a single 2.4 GHz antenna. When Alexa sends an HTTP request via WiFi, the ESP32 tries to reply via WiFi and emit the BLE signal at the exact same millisecond. The hardware arbitrator drops the BLE packet, resulting in fans that only obey "1 out of every 5 times".
* **The fix:** We implemented a **FreeRTOS Queue (Buzón)**. Alexa's HTTP commands are instantly queued (0ms delay) so the WiFi response can finish immediately. The main loop then safely unspools the queue and takes exclusive control of the BLE radio, ensuring 100% transmission reliability.

### 3. No MAC Spoofing Required
Despite the remotes using a static MAC address (often `2C:98:43:AF:0B:46`), the fan's hardware receiver ignores the MAC headers entirely. It only looks at the `Manufacturer Specific Data` payload. You do **not** need to spoof your ESP32's MAC address.

### 4. Stateless Design
Storing the fan state in the ESP32's Flash memory (`Preferences`) leads to desynchronization if someone uses the physical remote. This code is completely stateless. It acts purely as an action bridge, using relative step calculations for brightness and temperature (0-100 scales).

## 🚀 How to Use

1. **Sniff your Remote:** Use an app like *nRF Connect for Mobile*. Press a button on your physical remote and capture the data. **Crucial:** You must capture the sequence of 4 distinct packets for *every single action* (ON, OFF, Speed 1-6, etc.).
2. **Paste Hex Codes:** Open `src/main.cpp` and replace the placeholder `"..."` strings in the arrays with your 31-byte raw Hex strings.
3. **Configure WiFi:** Add your SSID and Password.
4. **Flash:** Upload the code to your ESP32 (e.g., WROOM-32U) via PlatformIO.
5. **Discover Devices:** Ask Alexa to "Discover my devices." It will find standard Light (WhiteSpectrum) and Fan (Dimmable) entities.

## 📦 Dependencies
* [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) (For lightning-fast BLE advertising)
* [Espalexa](https://github.com/Aircoookie/Espalexa) (For local, cloud-free Alexa integration)

## 💡 Notes on Device Types
The lights are exposed to Alexa as `whitespectrum` rather than `color` to provide native sliders for Brightness and Color Temperature (Warm/Cold) without the confusing RGB UI.
