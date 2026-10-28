<div align="center">
  <h1>Peripheral Light Ambilight for Home Assistant</h1>
  <p><b>Extend your TV Ambilight to peripheral room lights with ultra-low latency</b></p>

  [![HACS Custom](https://img.shields.io/badge/HACS-Custom%20Integration-41BDF5.svg?logo=homeassistantcommunitystore)](https://github.com/hacs/default)
  [![Home Assistant](https://img.shields.io/badge/Home%20Assistant-2024.1.0%2B-41BDF5.svg?logo=home-assistant)](https://www.home-assistant.io/)
  [![GitHub Release](https://img.shields.io/github/v/release/VictorCV-DAM/ha-peripheral-light-ambilight)](https://github.com/VictorCV-DAM/ha-peripheral-light-ambilight/releases)
  [![License: GPL-3.0](https://img.shields.io/badge/License-GPL--3.0-blue.svg)](LICENSE)
  [![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-Donate-orange?logo=buy-me-a-coffee)](https://buymeacoffee.com/VictorCV)
  [![PayPal](https://img.shields.io/badge/PayPal-Donate-blue?logo=paypal)](https://paypal.me/victorcava)
  [![Ko-fi](https://img.shields.io/badge/Ko--fi-Donate-red?logo=ko-fi)](https://ko-fi.com/victorcv)
</div>

High-performance, ultra-low latency hardware bridge and Home Assistant integration that expands your TV's ambient backlighting to all **peripheral room lights** (dining table spots, side floor lamps, wall sconces, and ceiling lights). Passively sniffs the proprietary WS2811 data stream of a **Govee DreamView T1 (H6199)** camera system and coordinates your room lights in real time with zero perceptible lag.

Works with **any content** on your screen: Smart TV apps (Netflix, YouTube, Prime Video, Disney+), gaming consoles (PS5, Xbox, Switch), Apple TV, and live TV — without HDMI splitters, without cloud delays, and without altering your original Govee setup.

---

## 🚀 Installation & Setup Guide

Follow these sequential steps to set up your hardware, flash the firmware, install the Home Assistant integration, and calibrate your lights.

```
┌─────────────────┐     ┌──────────────────┐     ┌───────────────────┐     ┌─────────────────┐
│ 1. Wire Tap     │ ──> │ 2. Flash ESPHome │ ──> │ 3. Install HACS   │ ──> │ 4. Calibrate    │
│ Single Green D+ │     │ Compile Firmware │     │ Add Integration   │     │ 360° Slider     │
└─────────────────┘     └──────────────────┘     └───────────────────┘     └─────────────────┘
```

---

### Step 1: Materials & Prerequisites
Before starting, ensure you have:
1. **ESP32-C3 DevKitM-1** (or any ESP32 with 160MHz+ CPU and Wi-Fi).
2. **1x DuPont Jumper Wire** (Female-to-Male).
3. **USB-C Cable & 5V 1A Power Supply** (a standard phone charger or your TV's USB port).
4. **Working Govee DreamView T1 (H6199)** setup installed on your television.
5. **Home Assistant** instance running on your local network with **HACS** installed.

---

### Step 2: Hardware Wiring (Passive Non-Intrusive Tap)
You do **not** need to cut wires, strip insulation, or solder anything. The connection is a passive, high-impedance tap directly into the connector socket:

```text
               ┌───[ USB-C Power (5V) ]───┐
            5V │ [ ]                  [ ] │ 5V
           GND │ [ ] ◄── Unconnected  [ ] │ GND
           3V3 │ [ ]                  [ ] │ ...
           IO4 │ [ ]                  [ ] │ ...
           IO3 │ [ ] ◄── GREEN WIRE   [ ] │ ...
           IO2 │ [ ]                  [ ] │ ...
           IO1 │ [ ]                  [ ] │ ...
           IO0 │ [ ]                  [ ] │ ...
               └──────────────────────────┘
```

#### Detailed Connection Steps:
1. **Unplug the Govee DreamView T1 control box** from its power adapter.
2. Locate the 4-pin output cable connecting the Govee control box to the LED strip on the back of the TV.
3. Look at the back of the connector housing (or slide back the silicone strain relief sleeve).
4. Identify **Pin 3 (Green Wire - Data Signal D+)**:
   * **Pin 1 (Black):** GND
   * **Pin 2 (White):** 12V Power
   * **Pin 3 (Green):** **WS2811 Data (D+) ➔ Connect here**
   * **Pin 4 (Red):** Auxiliary
5. Insert the male pin of your DuPont jumper wire securely into the socket hole alongside the **Green wire**.
6. Connect the female end of the jumper wire to pin **GPIO3** on your ESP32-C3.
7. Power the ESP32 via its USB-C port (using the TV's USB port or a 5V charger connected to the same power strip).
8. Reconnect the Govee power supply. The original TV LED strip will turn on and function completely normally.

> [!NOTE]
> **Why is only one wire needed?** The high-impedance GPIO input on the ESP32 passively sniffs the 800 kHz logic pulses without loading the circuit. When both the Govee power supply and the ESP32 USB power supply share the same household mains or TV ground plane, a reference ground is already established.

---

### Step 3: Flash the ESP32 via ESPHome

#### Method A: Via ESPHome Dashboard (Home Assistant Add-on)
1. Open Home Assistant and navigate to the **ESPHome** dashboard.
2. Click **+ New Device**, name it `esp32-ambilight-bridge`, and select **ESP32-C3**.
3. Click **Edit** on the newly created device card.
4. Replace the default YAML with the contents of [`firmware/esp32_ambilight.yaml`](firmware/esp32_ambilight.yaml).
5. In your ESPHome configuration directory (via Samba, SSH, or Studio Code Server), place [`firmware/govee_sniffer.h`](firmware/govee_sniffer.h) in the same folder as `esp32_ambilight.yaml`.
6. Edit your `secrets.yaml` file in ESPHome to include your Wi-Fi details (see [`firmware/secrets.yaml.example`](firmware/secrets.yaml.example)):
   ```yaml
   wifi_ssid: "YOUR_WIFI_NAME"
   wifi_password: "YOUR_WIFI_PASSWORD"
   static_ip: "192.168.1.192"      # Choose an available IP on your network
   gateway_ip: "192.168.1.1"
   dns_ip: "192.168.1.1"
   api_encryption_key: "YOUR_GENERATED_ESPHOME_KEY="
   ```
7. Connect the ESP32 to your PC via USB and click **Install ➔ Plug into this computer** (WebSerial flasher).
8. Once the initial USB flash completes, all future firmware updates can be done **wirelessly via OTA** with one click.

#### Method B: Via ESPHome CLI
1. Open a terminal with Python 3.10+ installed:
   ```bash
   pip install esphome
   ```
2. Clone or download this repository and navigate to the `firmware/` directory:
   ```bash
   cd firmware
   cp secrets.yaml.example secrets.yaml
   # Edit secrets.yaml with your Wi-Fi credentials
   ```
3. Connect your ESP32 via USB and flash:
   ```bash
   esphome run esp32_ambilight.yaml
   ```

---

### Step 4: Install the Custom Integration in Home Assistant

#### ⚡ 1-Click Fast Install via HACS
Click the My Home Assistant badge below to open this repository directly inside your Home Assistant HACS store, then click **Download**:

[![Open your Home Assistant instance and open a repository inside the Home Assistant Community Store.](https://my.home-assistant.io/badges/hacs_repository.svg)](https://my.home-assistant.io/redirect/hacs_repository/?owner=VictorCV-DAM&repository=ha-peripheral-light-ambilight&category=integration)

#### 🔍 Manual Install via HACS
1. Open **HACS** in Home Assistant $\rightarrow$ **Integrations**.
2. Click the three dots menu (**⋮**) in the top-right corner $\rightarrow$ **Custom repositories**.
3. In the Repository field, paste:
   ```text
   https://github.com/VictorCV-DAM/ha-peripheral-light-ambilight
   ```
4. Set the Type dropdown to **Integration** and click **Add**.
5. Find **Peripheral Light Ambilight** in the integration list, click on it, and click **Download**.
6. **Restart Home Assistant** (**Settings ➔ System ➔ Restart**).

---

### Step 5: Configure Light Zones via UI (Config Flow)
1. In Home Assistant, go to **Settings ➔ Devices & Services ➔ Add Integration**.
2. Search for **Peripheral Light Ambilight** and click it.
3. Assign your room's smart lights to their corresponding TV screen zones:
   * **Left Peripheral Light:** Light bulb or light group located on the left side of your room (e.g., `light.living_room_left`).
   * **Bottom / Center Light:** Lights under the TV, dining table, or sofa (e.g., `light.dining_table_lights`).
   * **Right Peripheral Light:** Light bulb or light group located on the right side of your room (e.g., `light.living_room_right`).
   * **Ceiling Light (Optional):** Overhead spots or ceiling lamp (e.g., `light.ceiling_spots`). Leave empty if not used.
   * **Transition Speed:** Recommended `0.1s` to `0.2s` for fluid, natural color shifts without sudden jumps.
4. Click **Submit**.

> [!IMPORTANT]
> **Mandatory Permission for Ultra-Low Latency:**
> Home Assistant restricts ESP32 devices from calling services directly by default. To unlock instant, lag-free response:
> 1. Go to **Settings ➔ Devices & Services ➔ ESPHome**.
> 2. Locate the `Govee DreamView Ambilight Bridge` card and click **Configure**.
> 3. Check the box: **"Allow the device to perform Home Assistant service calls"**.
> 4. Click **Submit**.

---

### Step 6: Calibration & Fine-Tuning
Under the newly created device card in Home Assistant, you have full control over the spatial mapping:

1. **Verify Zone Mapping with the RGB Test:**
   * Click **`button.test_rgb_sequence`** (*Test RGB Sequence*).
   * A 6-second diagnostic sequence will run across your room:
     * **Phase 1 (2s):** Left = Red, Center = Green, Right = Blue *(Ceiling = White)*.
     * **Phase 2 (2s):** Left = Green, Center = Blue, Right = Red *(Ceiling = Yellow)*.
     * **Phase 3 (2s):** Left = Blue, Center = Red, Right = Green *(Ceiling = Cyan)*.
   * This confirms that all light entities are receiving commands properly.

2. **Align the Corner Starting Point with 360° Rotation:**
   * Play a YouTube video showing primary colors on each edge (e.g., full screen red on left, blue on right).
   * If your physical Govee LED strip was installed starting from a different corner, simply drag the **`number.360_rotation`** slider between `-180°` and `+180°` until the colors in your room match the TV screen borders millimeter-perfectly.
   * If the sequence moves in the wrong direction (clockwise vs counter-clockwise), toggle **`switch.invert_rotation_direction`**.

3. **Tune Edge LED Counts:**
   * Adjust the sliders for **Left Edge** (`number.left_edge_leds`, def: 7), **Top Edge** (`number.top_edge_leds_ceiling`, def: 11), **Right Edge** (`number.right_edge_leds`, def: 7), and **Bottom Edge** (`number.bottom_edge_leds_center`, def: 11) to match your screen's aspect ratio.

4. **Switching Modes for Daily Use:**
   * **Watching TV:** Leave **`switch.ambilight_mode`** ON (or `select.mode_selector` set to `Ambilight`).
   * **Normal Living Room Lighting:** Turn **`switch.ambilight_mode`** OFF (or select `Custom`). The ESP32 immediately stops sending data, allowing you to manually dim, turn on/off, or change the colors of your dining room lights without any interference.

---

## ⚡ Key Capabilities

- **True Room-Scale Peripheral Ambilight:** Extends the TV immersion beyond the display frame to your perimeter and room lighting.
- **Zero-Lag Real-Time Sync:** Native ~25–30 FPS real-time capture directly from the camera data bus.
- **Non-Intrusive Sniffing:** High-impedance passive tap on a single data line — the original Govee LED strip continues functioning with 100% factory behavior.
- **No HDMI Splitter Required:** Works natively with DRM-protected 4K HDR, Dolby Vision, 120Hz VRR, and built-in Smart TV apps.
- **360° Software Rotation Calibration:** Granular slider (-180° to +180°) in Home Assistant to perfectly align TV colors with room light positions.
- **4-Zone Independent Control:** Left, Right, Bottom (Dining Table/Center), and Top (Ceiling) zones with individual LED count calibration sliders.
- **Dual Home Assistant Communication:** Direct high-speed API execution (`light.turn_on`) + Event-driven dispatch (`esphome.ambilight_color`).
- **No Cloud, 100% Local:** Operates entirely within your local network.

---

## 🎛️ Exposed Controls in Home Assistant

| Entity | Type | Function |
| :--- | :---: | :--- |
| `switch.ambilight_mode` | Switch | Toggles real-time Ambilight synchronization ON / OFF (Manual Mode). |
| `select.mode_selector` | Select | Dropdown selector: `Ambilight` (TV Sync) vs `Custom` (Independent room lighting). |
| `number.360_rotation` | Slider | Rotates mapping by -180° to +180° for perfect alignment with TV strip installation. |
| `number.left_edge_leds` | Slider | Number of LEDs sampled on the left edge (default: 7). |
| `number.top_edge_leds_ceiling` | Slider | Number of LEDs sampled on the top edge (default: 11). |
| `number.right_edge_leds` | Slider | Number of LEDs sampled on the right edge (default: 7). |
| `number.bottom_edge_leds_center` | Slider | Number of LEDs sampled on the bottom edge (default: 11). |
| `switch.invert_rotation_direction`| Switch | Inverts the clockwise/counter-clockwise strip direction. |
| `switch.enable_ceiling_light` | Switch | Enables transmitting the top edge color to a ceiling light entity. |
| `button.test_rgb_sequence` | Button | Runs a non-blocking 6-second RGB diagnostic cycle across all room lights. |

---

## 🔬 Technical Specifications & Engineering Deep-Dive

### 1. The WS2811 Protocol at 800 kHz
WS2811 LED data is transmitted via Single-Wire Return-to-Zero (NZR) pulses at $800\text{ kHz}$:
* **Logical `0`:** High for $\sim 400\text{ ns}$, Low for $\sim 850\text{ ns}$.
* **Logical `1`:** High for $\sim 800\text{ ns}$, Low for $\sim 450\text{ ns}$.

### 2. The Mid-Point Sampling Algorithm in IRAM
Standard microcontroller edge-detection interrupts fail because interrupt latency and Flash cache misses in ESP-IDF introduce jitter exceeding $1.5\ \mu\text{s}$.

This project implements `capture_govee_frame` strictly located in internal memory (`IRAM_ATTR`):
* **Cycle-Accurate Sampling:** Upon detecting a rising edge, the CPU waits **90 clock cycles ($560\text{ ns}$ at $160\text{ MHz}$)** using `esp_cpu_get_cycle_count()`.
* **State Latch:** At $560\text{ ns}$, a logical `0` has already fallen low, while a logical `1` is still high. Reading the GPIO register at this exact instant delivers **100% bit accuracy** with zero timing jitter.

### 3. Govee's 608 µs Corner Pause Bridging
The Govee T1 camera box computes corners on-the-fly, introducing internal pauses of up to **$608\ \mu\text{s}$ (97,289 CPU cycles)** within a single frame. Standard libraries (*FastLED*, *NeoPixelBus*) mistake this pause for a reset silence ($>50\ \mu\text{s}$) and drop the frame.
* **Pause Bridging Timeout:** Tuned to **$1.25\text{ ms}$ (200,000 CPU cycles)** to transparently bridge corner pauses.
* **Frame Reset Threshold:** Set to **$1.56\text{ ms}$ (250,000 CPU cycles)** to detect genuine inter-frame resets.
* **Frame Format:** 37 LEDs total (1 preamble LED + 36 active perimeter LEDs: Left=7, Top=11, Right=7, Bottom=11).
* **Color Encoding:** Native GRB order (Byte 0 = Green, Byte 1 = Red, Byte 2 = Blue).

### 4. Digital Signal Processing: 70/30 IIR Filter & 8-Point Hysteresis
TV cameras inherently pick up $50/60\text{ Hz}$ room flicker and sensor noise. Without filtering, smart bulbs would flicker and flood Home Assistant:
* **70/30 IIR Temporal Smoothing:**
  $$F_{\text{new}} = \frac{7 \cdot F_{\text{prev}} + 3 \cdot \text{Raw}}{10}$$
* **8-Point Noise Hysteresis:** State updates are only dispatched to Home Assistant when the color change exceeds $\Delta \ge 8$ points in any RGB channel.

---

## ❓ Frequently Asked Questions (FAQ)

### Does this replace or disable my original Govee TV LED strip?
**No.** The ESP32 is a purely passive high-impedance listener. The original Govee camera, control box, and TV strip continue functioning exactly as designed by the manufacturer.

### What kind of smart lights can I use for the peripheral room lighting?
Any smart light integrated into Home Assistant that supports RGB color changing (`rgb_color`): Philips Hue, WiZ, LIFX, Tuya (Wi-Fi or Zigbee), Yeelight, IKEA TRÅDFRI, Nanoleaf, Govee LAN, or custom WLED controllers.

### Will this flood my Home Assistant or slow down my Zigbee mesh?
**No.** The firmware features a built-in 8-point noise hysteresis filter and a 70/30 IIR temporal smoothing filter. Static scenes, paused videos, or uniform screens generate **0 commands per second**, ensuring zero network congestion.

---

## ☕ Support the Project

If this project enhanced your Home Assistant ambient lighting experience, consider supporting ongoing development:

[![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-Donate-orange?style=for-the-badge&logo=buy-me-a-coffee)](https://buymeacoffee.com/VictorCV)
[![PayPal](https://img.shields.io/badge/PayPal-Donate-blue?style=for-the-badge&logo=paypal)](https://paypal.me/victorcava)
[![Ko-fi](https://img.shields.io/badge/Ko--fi-Donate-red?style=for-the-badge&logo=ko-fi)](https://ko-fi.com/victorcv)

---

- **Author & Maintainer:** Víctor C. V. ([@VictorCV-DAM](https://github.com/VictorCV-DAM))
- **Email:** `victorcvtrabajo@gmail.com`
- **Copyright:** © 2024-2026 Víctor C. V. All rights reserved.
- **License:** [GNU General Public License v3.0](LICENSE) (GPL-3.0).
