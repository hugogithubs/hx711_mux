# ESPHome HX711 Dual-Channel Multiplexer Component

A high-performance, native C++ extension for ESPHome designed for synchronized readout of both channels (Channel A & Channel B) across one or multiple HX711 load cell amplifiers.

---

## 🎯 Motivation & Background

The official `hx711` component integrated into ESPHome quickly reaches its structural limits in more complex projects. It is inherently **rigid and inflexible**: a single sensor entry in standard YAML can only ever read one specific channel (Channel A *or* Channel B). Running both channels simultaneously by defining two separate instances is **not** natively supported, as they would need to share the same physical pins, which is blocked by ESPHome's schema validation.

Furthermore, pure software bit-banging solutions in modern ESPHome environments are highly susceptible to severe measurement spikes when high-frequency serial protocols are active in the background (e.g., Modbus requests from JK-BMS, Daly-BMS, or Victron devices). These software interrupts disrupt the precise microsecond timing required by the HX711 clock signal.

**This component fundamentally solves these issues through native C++ execution:** It handles the multiplexing of both channels (A & B) synchronously via a single hardware hub, protects the clock timing against BMS interference using short hardware interrupt locks (`portENTER_CRITICAL`), and relocates the taring process (storing the zero point in flash memory) to the mathematically perfect position *before* the smoothing filters.

---

## 💡 Application Example: Monitoring a Large Battery Storage Bank

A prime use case for this component is the mechanical stress and compression monitoring of prismatic Lithium Iron Phosphate (LiFePO4) cells within a stationary home energy storage system (e.g., a 16S battery pack):

*   **The Problem:** LiFePO4 cells physically expand during high current draw or at a high State of Charge (known as "cell swelling"). To protect the cells and maximize their lifespan, they are clamped inside a rigid structural frame equipped with heavy-duty compression springs. Excessive pressure (over-compression) damages the internal cell structure, while insufficient pressure leads to capacity loss over time.
*   **The Solution with this Component:** Heavy-duty industrial load cells are mounted at the ends of the clamping fixture.
    *   **Channel A (Board 1)** measures the mechanical force exerted on the left spring axis.
    *   **Channel B (Board 1)** measures the mechanical force exerted on the right spring axis.
    *   The **Total Weight (Sum)** instantly provides Home Assistant with the absolute structural force (in kg or Newtons) acting on the battery bank.
*   **The Highlight:** Even while the ESP32 board is fetching hundreds of operational metrics (cell voltages, currents, temperatures) from a JK-BMS via Modbus every few milliseconds, the weight readings remain completely stable, rattle-free, and instantly drop back to a flawless zero line when tared.

---

## ✨ Features & What to Expect

*   * **True Dual-Channel Multiplexing (with selectable Gain):** Utilizes the hardware switching of the HX711 to read Channel A and Channel B alternately in a fast cycle using the same two pins. Due to hardware limitations, Channel B always runs fixed in the low-power Gain 32 mode (`LOW`). For Channel A, you can now switch between `HIGH` (Gain 128, default) and `LOW` (Gain 64) via the YAML interface. Case insensitivity is handled automatically.
*   **Hardware Interrupt Protection (Anti-BMS-Jitter):** The critical bit-banging routine is temporarily protected against incoming interrupts on the ESP32 core (`portENTER_CRITICAL`). Measurements are *never* distorted by serial Modbus or BMS traffic.
*   **🛡️ Automatic UI Frontend with Safety Lock:** The component automatically generates a corresponding `Tare` button **as well as an `Unlock` switch** for each connected load cell in Home Assistant. Taring is strictly blocked until the safety switch is manually toggled on. Once pressed, the switch automatically locks itself again to protect your flash memory (NVS) from excessive writes.
*   **⏱️ Boot-Muting against Startup Jitter:** Upon system startup or after an Over-The-Air (OTA) update, the component mutes data transmission for the first 5 seconds. This allows your YAML filters to fill up with stable background values, effectively preventing temporary negative drops or spikes in your Home Assistant history plots.
*   **⏱️ Boot Muting with dynamic Turbo Warmup:** Upon system boot or after an OTA update, the hub automatically switches to a fast 500ms interval (instead of 1000ms) until each sensor has collected exactly 20 raw samples. Data publishing is blocked during this warmup phase. This allows the filter chains to fill up instantly with stable values in the background, effectively preventing faulty spikes in the plot before the system transitions to normal one-second operational intervals.
*   **Perfectly Synchronized Sum Sensor:** The mathematical sum sensor only publishes a new state once *all* tracked input cells have successfully refreshed their data within the same operational cycle. This completely eliminates artificial stepping or jitter on the total combined weight.
*   **Multi-Instance Capable (N:M Hub):** Supports an arbitrary number of parallel HX711 amplifier boards hooked up to different GPIO pin sets.

---

## 🛠️ Technical Processing Order

Every single raw measurement passes through this exact pipeline:

1. **Hardware-Read:** Raw ticks are read using an optimized C++ clock cycle.
2. **Smoothing (YAML):** The raw ticks pass through your mathematical pre-filters first (e.g., `median` and `sliding_window_moving_average`) to eliminate any noise or outliers.
3. **C++ Tare Filter:** The zero point stored in flash memory is subtracted from the already smoothed ticks.
4. **Calibration (YAML):** The cleaned and tared signal runs into your `calibrate_linear` and is converted into kilograms.

---

## 📦 Installation & Integration

You can integrate this component into your ESPHome configuration either directly via GitHub (recommended) or locally as a custom component.

### Option 1: Direct Integration via GitHub (Recommended)

Add the component to your ESPHome YAML configuration using the `external_components` block. ESPHome will automatically pull the files from this repository during the compilation task.

```yaml
external_components:
  - source:
      type: git
      url: https://github.com
    components: [ hx711_mux ]
```

### Option 2: Local Integration (Development Mode)

If you wish to modify the source code locally or compile offline, you can copy the component folder manually. To ensure it loads correctly, place the files exactly within the following file path structure:

**Required File Path:**
```text
config\esphome\hx711_mux_local\hx711_mux\
```

Verify that this subdirectory contains the following three files:
*   `__init__.py`
*   `sensor.py`
*   `hx711_mux.h`

Then, reference the local directory inside your ESPHome configuration:

```yaml
external_components:
  - source:
      type: local
      path: hx711_mux_local
    components: [ hx711_mux ]
```

---

### 🧩 Crucial Framework Dependencies (Requirement)

For ESPHome to successfully compile the underlying C++ classes for the safety switches (Template Switch) and automatic buttons, the core components `switch`, `template`, and `button` **must** be explicitly initialized at least once inside your top-level YAML configuration file.

Ensure these placeholder blocks are present in your setup:

```yaml
# Activates the button core framework for the automated tare buttons
button:

# Activates the switch framework and template engine required for the lock mechanism
switch:
  - platform: template
    name: "Mux Template Activator"
    id: mux_template_activator
    internal: true
    optimistic: true
```

---

## ⚙️ Configuration Example (Multi-Board Setup)

The following example showcases a deployment utilizing **two physical HX711 boards**, hosting two load cells each (totaling 4 individual cells). All 4 cells are ultimately summed up into a single, perfectly synchronized total compression weight, alongside individual axle sum weights.

### Deine `esphome.yaml` Konfiguration

```yaml
# Erforderlich, da die Buttons im Hintergrund miterzeugt werden
button:

# Erforderlich, um das Template-Framework für die Freigabe-Schalter zu aktivieren
switch:
  - platform: template
    name: "Mux-Template-Aktivator"
    id: mux_template_activator
    internal: true
    optimistic: true

external_components:
  - source:
      type: local
      path: components # Verweist auf deinen lokalen Ordner
    components: [ hx711_mux ]

# ====================================================================
# 1. HARDWARE-HUBS ANLEGEN (Die physikalischen Boards)
# ====================================================================
hx711_mux:
  # BOARD 1 (Zuständig für Zelle 1 & 2)
  - id: hx711_hub_1
    clk_pin: 18
    dout_pin: 19

  # BOARD 2 (Zuständig für Zelle 3 & 4)
  - id: hx711_hub_2
    clk_pin: 25
    dout_pin: 26

# ====================================================================
# 2. DIE SENSOREN UND DIE SUMMENBILDUNG
# ====================================================================
sensor:
  # ------------------------------------------------------------------
  # WÄGEZELLEN AN BOARD 1
  # ------------------------------------------------------------------
  - platform: hx711_mux
    name: "Zelle 1 (Board 1 - Kanal A)"
    id: zelle_1_b1_ka
    hub_id: hx711_hub_1
    channel: A
    gain: "LOW"                # Optional parameter, "HIGH" / "LOW" (defaults to "HIGH" if omitted)
    accuracy_decimals: 5
    filters:
      - median:
          window_size: 5
          send_every: 1
      - sliding_window_moving_average:
          window_size: 5
          send_every: 1
      - calibrate_linear:
          datapoints:
            - 0 -> 0.00000
            - 10280 -> 0.22100

  - platform: hx711_mux
    name: "Zelle 2 (Board 1 - Kanal B)"
    id: zelle_2_b1_kb
    hub_id: hx711_hub_1
    channel: B
    accuracy_decimals: 5
    filters:
      - median:
          window_size: 5
          send_every: 1
      - sliding_window_moving_average:
          window_size: 5
          send_every: 1
      - calibrate_linear:
          datapoints:
            - 0 -> 0.00000
            - 2681 -> 0.22100

  # ------------------------------------------------------------------
  # WÄGEZELLEN AN BOARD 2
  # ------------------------------------------------------------------
  - platform: hx711_mux
    name: "Zelle 3 (Board 2 - Kanal A)"
    id: zelle_3_b2_ka
    hub_id: hx711_hub_2
    channel: A
    accuracy_decimals: 5
    filters:
      - median:
          window_size: 5
          send_every: 1
      - sliding_window_moving_average:
          window_size: 5
          send_every: 1
      - calibrate_linear:
          datapoints:
            - 0 -> 0.00000
            - 10500 -> 0.22100

  - platform: hx711_mux
    name: "Zelle 4 (Board 2 - Kanal B)"
    id: zelle_4_b2_kb
    hub_id: hx711_hub_2
    channel: B
    accuracy_decimals: 5
    filters:
      - median:
          window_size: 5
          send_every: 1
      - sliding_window_moving_average:
          window_size: 5
          send_every: 1
      - calibrate_linear:
          datapoints:
            - 0 -> 0.00000
            - 2730 -> 0.22100

  # --------------------------------------------------------------------
  # DREI SYNCHRONE SUMMEN-SENSOREN GLEICHZEITIG (PARALLEL) als Beispiel
  # --------------------------------------------------------------------
  
  # Summe 1: Nur die Zellen von Board 1 (Kanal A + Kanal B)
  - platform: hx711_mux
    type: sum
    name: "Gewicht Achse 1 (Board 1)"
    id: summe_achse_1
    accuracy_decimals: 5
    tracks:
      - zelle_1_b1_ka
      - zelle_2_b1_kb

  # Summe 2: Nur die Zellen von Board 2 (Kanal A + Kanal B)
  - platform: hx711_mux
    type: sum
    name: "Gewicht Achse 2 (Board 2)"
    id: summe_achse_2
    accuracy_decimals: 5
    tracks:
      - zelle_3_b2_ka
      - zelle_4_b2_kb

  # Summe 3: Das absolute Gesamtgewicht über alle 4 Sensoren parallel
  - platform: hx711_mux
    type: sum
    name: "Cell Compression Gesamtgewicht (Alle 4 Zellen)"
    id: waage_gesamtgewicht
    accuracy_decimals: 5
    tracks:
      - zelle_1_b1_ka
      - zelle_2_b1_kb
      - zelle_3_b2_ka
      - zelle_4_b2_kb
```
---

## 📋 Generated Entities in Home Assistant

Once compiled and connected, the component dynamically exposes all tracking values and safety controls directly to Home Assistant. Based on the multi-board configuration above, the following entities will be created:

### 1. 📊 Weight Sensors (State values in kg/custom unit)
*   `sensor.zelle_1_board_1_kanal_a`
*   `sensor.zelle_2_board_1_kanal_b`
*   `sensor.zelle_3_board_2_kanal_a`
*   `sensor.zelle_4_board_2_kanal_b`
*   `sensor.gewicht_achse_1_board_1` (Sub-total Axis 1)
*   `sensor.gewicht_achse_2_board_2` (Sub-total Axis 2)
*   `sensor.cell_compression_gesamtgewicht_alle_4_zellen` (Synchronized master total force)

### 2. 🔒 Safety Unlock Switches (Accidental Write Interception)
Each cell exposes a dedicated toggle switch to guard its zero point. Pressing the corresponding tare button remains locked out unless this toggle is explicitly flipped open beforehand:
*   `switch.zelle_1_board_1_kanal_a_freigabe`
*   `switch.zelle_2_board_1_kanal_b_freigabe`
*   `switch.zelle_3_board_2_kanal_a_freigabe`
*   `switch.zelle_4_board_2_kanal_b_freigabe`

### ⚖️ 3. Execution Buttons (Tare Triggers)
Forces the targeted cell to accept the current filtered tick average as its new electrical zero point inside the RAM and commits the value to non-volatile flash storage (NVS). Upon completion, the safety unlock toggle automatically drops back to its locked position:
*   `button.zelle_1_board_1_kanal_a_tarieren`
*   `button.zelle_2_board_1_kanal_b_tarieren`
*   `button.zelle_3_board_2_kanal_a_tarieren`
*   `button.zelle_4_board_2_kanal_b_tarieren`

---

## 💡 Hardware Switching Considerations

Certain budget variations of the HX711 IC require precise physical signal-settling windows during channel selection. This component includes a specialized `2µs` clock-pulse delay baked natively into its C++ read routine handling pulses 25 and 26. This guarantees flawless switching characteristics across all generic HX711 break-out modules without limiting the raw C++ polling loops on the ESP32.

---

## 🤖 Credits & Collaboration

This project is the result of an active co-development partnership between human engineering and Artificial Intelligence:
*   **Concept, Hardware Proofs & Structural Architecture:** Engineered and validated on-site against a live LiFePO4 battery cell deployment by the repository owner.
*   **Code Generation, Refactoring & Documentation:** Custom programmed, rigorously iterated, and documented using AI assistance.

This close-knit interaction made it possible to navigate and extend the core internals of ESPHome's Python and C++ abstraction APIs, creating a high-performance, robust multi-board multiplexer framework in record time.

---

## ⚠️ Liability Disclaimer

This repository hosts a private hobbyist development and is provided exclusively for information and educational purposes.

*   **No Warranty:** The software is provided "as is", without warranty of any kind, express or implied, including but not limited to the warranties of merchantability, fitness for a particular purpose, or non-infringement.
*   **User Risk:** Any deployment or use of this component happens entirely at the discretion and sole risk of the operator.
*   **Property and Personal Damages:** The author shall not be held liable for any claims, hardware defects (e.g., cell over-compression, degraded battery strings, fried ESP32 chips), data corruption, financial loss, or personal injuries resulting from the use, failure, or customization of this codebase.

When setting up safety monitoring loops for hazardous setups like Lithium Iron Phosphate banks (LiFePO4), the user remains completely responsible for providing separate, non-software mechanical over-pressure overrides (e.g., hardware pressure release spring-venting) or physical trip relays alongside this automation.

<!-- Test für Commit-Signierung -->
