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

High-performance, ultra-low latency hardware bridge and Home Assistant integration that expands your TV's ambient backlighting to all **peripheral room lights** (dining room spots, side table lamps, wall sconces, and ceiling lights). Passively sniffs the proprietary WS2811 data stream of a **Govee DreamView T1 (H6199)** camera system and coordinates the room lights in real time with zero lag.

Works with **any content** displayed on the screen: Smart TV apps (Netflix, YouTube, Prime Video), consoles (PS5, Xbox Series X, Switch), Apple TV, Blu-ray, and live broadcast TV — without HDMI splitters, without cloud delays, and without modifying the original Govee hardware.

---

- **Author & Maintainer:** Víctor C. V. ([@VictorCV-DAM](https://github.com/VictorCV-DAM))
- **Email:** `victorcvtrabajo@gmail.com`
- **Copyright:** © 2024-2026 Víctor C. V. All rights reserved.
- **License:** [GNU General Public License v3.0](LICENSE) (GPL-3.0).

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

## 🛠️ Materials & Bill of Materials (BOM)

| Component | Quantity | Specification / Description |
| :--- | :---: | :--- |
| **ESP32-C3 DevKitM-1** | 1 | 160 MHz 32-bit RISC-V MCU, 4MB Flash, USB-C (Standard ESP32 dual-core Xtensa also supported) |
| **Jumper Wire** | 1 | Female-to-Male DuPont jumper wire (Single connection to Green D+ data wire) |
| **USB-C Cable & 5V PSU** | 1 | Standard 5V 1A power supply or TV USB port |
| **Govee DreamView T1 (H6199)** | 1 | Immersion TV Backlight Kit with camera control box and LED strip |

---

## 🔌 Hardware Wiring & Pinout

The ESP32 connects in **passive high-impedance listening mode** directly to the Govee LED strip connector without cutting or splicing the original cable:

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

### Connector Details (Govee T1 Output)
* **Pin 1 (Black):** GND
* **Pin 2 (White):** 12V DC Power
* **Pin 3 (Green):** **WS2811 Data Signal (D+)** $\rightarrow$ Connect to **GPIO3** of the ESP32.
* **Pin 4 (Red):** Auxiliary / Unused

> [!TIP]
> **Single-Wire Tap**: Only the **Green data wire** needs to be connected to GPIO3. When powered from the TV's USB port or the same power strip, common ground is naturally established.

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

## 🚀 Installation & Setup

### Step 1: Flash the ESP32 via ESPHome
1. Install [ESPHome](https://esphome.io/).
2. Copy `firmware/govee_sniffer.h` and `firmware/esp32_ambilight.yaml` to your ESPHome directory.
3. Configure your Wi-Fi credentials in `secrets.yaml`.
4. Compile and flash to your ESP32:
   ```bash
   esphome run esp32_ambilight.yaml
   ```

### Step 2: Install via HACS

[![Open your Home Assistant instance and open a repository inside the Home Assistant Community Store.](https://my.home-assistant.io/badges/hacs_repository.svg)](https://my.home-assistant.io/redirect/hacs_repository/?owner=VictorCV-DAM&repository=ha-peripheral-light-ambilight&category=integration)

#### ⚡ 1-Click Fast Install
Click the My Home Assistant badge above to open this repository directly inside your Home Assistant HACS store, then click **Download**.

#### 🔍 Manual Add in HACS
1. Open **HACS** in Home Assistant $\rightarrow$ Integrations $\rightarrow$ **Custom Repositories** (top right three dots).
2. Paste repository URL: `https://github.com/VictorCV-DAM/ha-peripheral-light-ambilight` with category **Integration**.
3. Click **Download** and restart Home Assistant.
4. Go to **Settings ➔ Devices & Services ➔ Add Integration** and search for **Peripheral Light Ambilight**.
5. Select your Left, Bottom/Center, Right, and optional Ceiling peripheral light entities.

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

## ☕ Support the Project

If this project enhanced your Home Assistant ambient lighting experience, consider supporting ongoing development:

[![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-Donate-orange?style=for-the-badge&logo=buy-me-a-coffee)](https://buymeacoffee.com/VictorCV)
[![PayPal](https://img.shields.io/badge/PayPal-Donate-blue?style=for-the-badge&logo=paypal)](https://paypal.me/victorcava)
[![Ko-fi](https://img.shields.io/badge/Ko--fi-Donate-red?style=for-the-badge&logo=ko-fi)](https://ko-fi.com/victorcv)
