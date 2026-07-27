# ESPHome HX711 Dual-Channel Multiplexer Component

Eine hochperformante, native C++ Erweiterung für ESPHome zur synchronisierten Auslesung beider Kanäle (Kanal A & Kanal B) eines oder mehrerer HX711-Wägezellen-Verstärker. 

Diese Komponente wurde speziell für anspruchsvolle Umgebungen entwickelt, in denen hochfrequente serielle Protokolle (wie Modbus-Abfragen von JK-BMS, Daly-BMS oder Victron-Geräten) herkömmliche Software-Bit-Banging-Methoden stören und zu Messwert-Ausreißern führen würden.

## ✨ Features & Was dich erwartet

*   **Echtes Dual-Channel Multiplexing:** Nutzt die Hardware-Umschaltung des HX711, um Kanal A (Gain 128) und Kanal B (Gain 32) im schnellen Wechsel über dieselben zwei Pins auszulesen.
*   **Hardware-Interrupt-Schutz (Anti-BMS-Jitter):** Der kritische Bit-Banging-Bereich wird im ESP32-Kern kurzzeitig gegen Interrupts gesperrt (`portENTER_CRITICAL`). Messungen werden *niemals* durch Modbus- oder serielle BMS-Abfragen verzerrt.
*   **Intantanes, senkrechtes Nullen (Kriecher-Schutz):** Der C++ eigene Tara-Filter sitzt logisch *hinter* den Glättungsfiltern im RAM, aber *vor* dem Scaling. Beim Druck auf "Tarieren" springt der Wert im Home Assistant **sofort und rechtwinklig auf Null**, ohne träge durch die Filter kriechan zu müssen.
*   **Dauerhafter Flash-Speicher (NVS):** Der ermittelte Nullpunkt (Tara) wird ausfallsicher im Flash des ESP32 abgelegt und übersteht jeden Stromausfall oder Neustart.
*   **Automatisches UI-Frontend:** Die Komponente erzeugt im Home Assistant für jede Wägezelle vollautomatisch einen passenden `Tarieren`-Button. Es ist kein manueller YAML-Code für Knöpfe nötig.
*   **Perfekt synchronisierter Summen-Sensor:** Der mathematische Summen-Sensor rechnet erst ab, wenn *alle* beteiligten Zellen im selben Zyklus aktualisiert wurden. Das verhindert Jitter auf dem Gesamtgewicht.
*   **Multi-Instanz-fähig (N:M Hub):** Unterstützt beliebig viele parallele HX711-Boards an unterschiedlichen Pins.

---

## 🛠️ Technische Verarbeitungs-Reihenfolge

Damit die Mathematik absolut fehlerfrei aufgeht, durchläuft jeder Messwert diese exakte Kette:
1. **Hardware-Read:** Absolut unberührte Roh-Ticks werden per optimiertem C++ Takt ausgelesen.
2. **Glättung (YAML):** Die Ticks durchlaufen deine Filter (z. B. Median, Moving Average).
3. **C++ Tara-Filter:** Der im Flash gespeicherte Nullpunkt wird vom geglätteten Ticks-Wert abgezogen.
4. **Kalibrierung (YAML):** Das genullte Signal läuft in dein `calibrate_linear` und wird in Kilogramm umgerechnet.

---

## 🚀 Konfigurations-Beispiel (Multi-Board Setup)

Das folgende Beispiel zeigt die Verwendung von **zwei physikalischen HX711-Boards**, an denen jeweils zwei Wägezellen (insgesamt 4 Zellen) betrieben werden. Alle 4 Zellen werden am Ende vollautomatisch zu einem synchronen Gesamtgewicht addiert.

### Ordnerstruktur auf deinem Server / Git
```text
your_repository/
└── components/
    └── hx711_mux/
        ├── __init__.py
        ├── sensor.py
        └── hx711_mux.h
```

### Deine `esphome.yaml` Konfiguration

```yaml
# Erforderlich, da die Buttons im Hintergrund miterzeugt werden
button:

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
            - 10500 -> 0.22100  # Individueller Faktor für Zelle 3

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
            - 2730 -> 0.22100  # Individueller Faktor für Zelle 4

  # ------------------------------------------------------------------
  # SYNCHRONER SUMMEN-SENSOR (Berechnet das absolute Gesamtgewicht)
  # ------------------------------------------------------------------
  - platform: hx711_mux
    type: sum
    name: "Zellen Gesamtgewicht (4 Zellen)"
    id: waage_gesamtgewicht
    accuracy_decimals: 5
    tracks:
      - zelle_1_b1_ka
      - zelle_2_b1_kb
      - zelle_3_b2_ka
      - zelle_4_b2_kb
```

---

## 📋 Erzeugte Entitäten im Home Assistant

Nach dem erfolgreichen Kompilieren und Einbinden stellt die Komponente vollautomatisch folgende Entitäten in Home Assistant zur Verfügung:

1. **Sensoren (Gewichte):**
   * `sensor.zelle_1_board_1_kanal_a` (In kg)
   * `sensor.zelle_2_board_1_kanal_b` (In kg)
   * `sensor.zelle_3_board_2_kanal_a` (In kg)
   * `sensor.zelle_4_board_2_kanal_b` (In kg)
   * `sensor.zellen_gesamtgewicht_4_zellen` (Perfekt synchronisierte Summe aller 4 Zellen)

2. **Buttons (Tarieren):**
   * `button.zelle_1_board_1_kanal_a_tarieren` (Setzt Zelle 1 im Flash auf Null)
   * `button.zelle_2_board_1_kanal_b_tarieren` (Setzt Zelle 2 im Flash auf Null)
   * `button.zelle_3_board_2_kanal_a_tarieren` (Setzt Zelle 3 im Flash auf Null)
   * `button.zelle_4_board_2_kanal_b_tarieren` (Setzt Zelle 4 im Flash auf Null)

---

## 💡 Hinweise zur Hardware-Umschaltung

Manche günstigen Nachbauten des HX711-Chips benötigen präzise Signal-Mindestlaufzeiten beim Umschalten. Diese Komponente ist im C++ Quellcode mit einer optimierten Puls-Verzögerung von `2µs` für die Taktimpulse 25 und 26 ausgestattet. Dies garantiert eine fehlerfreie Kanal-Umschaltung auf allen gängigen HX711-Modulen bei voller C++ Ausführungsgeschwindigkeit auf dem ESP32.

