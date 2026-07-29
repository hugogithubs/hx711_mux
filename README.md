
# ESPHome HX711 Dual-Channel Multiplexer Komponente

Eine hochperformante, native C++ Erweiterung für ESPHome zur synchronisierten Auslesung beider Kanäle (Kanal A & Kanal B) eines oder mehrerer HX711-Wägezellen-Verstärker.

---

## 🎯 Motivation & Hintergrund

Die offizielle, in ESPHome integrierte `hx711`-Komponente stößt bei komplexeren Projekten schnell an ihre Grenzen. Sie ist strukturell **starr und unflexibel**: Ein einzelner Sensor-Eintrag kann im YAML-Standard immer nur exakt einen Kanal auslesen (Kanal A *oder* Kanal B). Es ist **nicht** möglich, beide Kanäle zu nutzen, indem man zwei separate Instanzen anlegt, da beide dieselben Pins nutzen müssten, was durch die Validierung des Frameworks verhindert wird.

Zudem neigen reine Software-Bit-Banging-Lösungen in modernen ESPHome-Setups zu massiven Messwert-Ausreißern, wenn im Hintergrund hochfrequente serielle Protokolle laufen (z. B. Modbus-Abfragen von JK-BMS, Daly-BMS oder Victron-Geräten). Die dortigen Software-Interrupts zerreißen das empfindliche Timing des HX711-Takts.

**Diese Komponente löst all diese Probleme fundamental durch nativen C++ Code:** Sie übernimmt das Multiplexing beider Kanäle (A & B) synchron über einen einzigen Hardware-Hub, schützt das Takt-Timing durch kurzzeitige Interrupt-Sperren (`portENTER_CRITICAL`) vor BMS-Störungen und verlagert das Tarieren (Nullpunkt-Speicherung im Flash) an die mathematisch perfekte Stelle direkt *vor* den Glättungsfiltern.

---

## 💡 Anwendungsbeispiel: Überwachung eines großen Batterie-Speichers

Ein typisches Szenario für den Einsatz dieser Komponente ist die mechanische Überwachung von prismatischen Lithium-Eisenphosphat-Zellen (LiFePO4) in einem stationären Heimspeicher (z. B. 16S Akku-Block):

*   **Das Problem:** LiFePO4-Zellen dehnen sich bei hoher Belastung oder hohem Ladezustand physikalisch aus (sogenanntes "Cell Swelling"). Um die Lebensdauer der Zellen nicht zu gefährden, werden sie in einem stabilen Rahmen mit massiven Druckfedern eingespannt. Zu hoher Druck (Kompression) beschädigt die Zellen; zu niedriger Druck führt zu Kapazitätsverlust.
*   **Die Lösung mit dieser Komponente:** An den Enden des Spannrahmens werden schwere Industrie-Wägezellen montiert. 
    *   **Kanal A (Board 1)** misst den Druck der linken Feder-Achse.
    *   **Kanal B (Board 1)** misst den Druck der rechten Feder-Achse.
    *   Das **Gesamtgewicht (die Summe)** zeigt im Home Assistant sofort die absolute mechanische Gesamtkraft (in kg oder Newton) an, die auf die Batteriezellen wirkt.
*   **Der Clou:** Obwohl das ESP32-Board zeitgleich über Modbus im Millisekundentakt hunderte Datenwerte (Zellspannungen, Ströme, Temperaturen) aus dem JK-BMS ausliest, bleibt die Druckmessung der Waage absolut stabil, zappelfrei und liefert bei Entlastung per Knopfdruck eine perfekte Nullkurve.

---

## ✨ Features & Was dich erwartet

*   **Echtes Dual-Channel Multiplexing:** Nutzt die Hardware-Umschaltung des HX711, um Kanal A (Gain 128) und Kanal B (Gain 32) im schnellen Wechsel über dieselben zwei Pins auszulesen.
*   **Hardware-Interrupt-Schutz (Anti-BMS-Jitter):** Der kritische Bit-Banging-Bereich wird im ESP32-Kern kurzzeitig gegen Interrupts gesperrt (`portENTER_CRITICAL`). Messungen werden *niemals* durch Modbus- oder serielle BMS-Abfragen verzerrt.
*   **🛡️ Automatisches UI-Frontend mit Sicherheits-Sperre:** Die Komponente erzeugt im Home Assistant für jede Wägezelle vollautomatisch einen passenden `Tarieren`-Button **sowie einen `Freigabe`-Schalter**. Das Tarieren ist blockiert, bis die Freigabe manuell aktiviert wird. Nach dem Betätigen sperrt sich der Schalter zum Schutz deines Flash-Speichers (NVS) sofort wieder selbstständig.
*   **⏱️ Boot-Muting gegen Einschalt-Jitter:** Beim Systemstart oder nach einem OTA-Update blockiert die Komponente die Datenweitergabe für die ersten 5 Sekunden. Die Filterketten können sich so im Hintergrund mit stabilen Werten füllen, wodurch Fehlmessungen im Plot effektiv verhindert werden.
*   **Perfekt synchronisierter Summen-Sensor:** Der mathematische Summen-Sensor rechnet erst ab, wenn *alle* beteiligten Zellen im selben Zyklus aktualisiert wurden. Das verhindert Jitter auf dem Gesamtgewicht.
*   **Multi-Instanz-fähig (N:M Hub):** Unterstützt beliebig viele parallele HX711-Boards an unterschiedlichen Pins.

---

## 🛠️ Technische Verarbeitungs-Reihenfolge

Damit die Mathematik absolut fehlerfrei aufgeht, durchläuft jeder Messwert diese exakte Kette:
1. **Hardware-Read:** Absolut unberührte Roh-Ticks werden per optimiertem C++ Takt ausgelesen.
2. **C++ Tara-Filter:** Der im Flash gespeicherte Nullpunkt wird direkt von den Roh-Ticks abgezogen.
3. **Glättung (YAML):** Die genullten Ticks durchlaufen deine Filter (z. B. Median, Moving Average).
4. **Kalibrierung (YAML):** Das bereinigte Signal läuft in dein `calibrate_linear` und wird in Kilogramm umgerechnet.

---

## 📦 Installation & Einbindung

Du kannst diese Komponente entweder direkt über GitHub (empfohlen) oder lokal als Custom Component in dein ESPHome-Projekt einbinden.

### Option 1: Einbindung direkt via GitHub (Empfohlen)

Füge die Komponente über das `external_components`-Feature direkt aus diesem GitHub-Repository in deine ESPHome-YAML-Konfiguration ein. ESPHome lädt die Dateien dann beim Kompilieren automatisch im Hintergrund herunter.

```yaml
external_components:
  - source:
      type: git
      url: https://github.com
    components: [ hx711_mux ]
```

### Option 2: Lokale Einbindung (Entwickler-Modus)

Wenn du den Code lokal bearbeiten oder offline kompilieren möchtest, kannst du den Komponenten-Ordner manuell in dein Verzeichnis kopieren. Damit die Einbindung auf Anhieb fehlerfrei funktioniert, musst du die Dateien exakt im folgenden Pfad ablegen:

**Erforderlicher Dateipfad:**
```text
config\esphome\hx711_mux_local\hx711_mux\
```

Stelle sicher, dass sich in diesem Unterordner die drei folgenden Dateien befinden:
*   `__init__.py`
*   `sensor.py`
*   `hx711_mux.h`

Binde den lokalen Ordner anschließend wie folgt in deiner ESPHome-YAML-Konfiguration ein:

```yaml
external_components:
  - source:
      type: local
      path: hx711_mux_local
    components: [ hx711_mux ]
```

---

### 🧩 Wichtige Framework-Abhängigkeiten (Voraussetzung)

Damit ESPHome die C++ Klassen für die Sicherheits-Schalter (Template-Switch) und die automatischen Taster korrekt im Hintergrund einkompilieren kann, müssen die Komponenten `switch`, `template` und `button` **zwingend** mindestens einmal in deiner YAML-Datei erwähnt werden.

Stelle sicher, dass diese Blöcke in deiner Konfiguration existieren:

```yaml
# Aktiviert das Button-Framework für die automatischen Tare-Taster
button:

# Aktiviert das Switch-Framework und das Template-System für die Verriegelung
switch:
  - platform: template
    name: "Mux-Template-Aktivator"
    id: mux_template_activator
    internal: true
    optimistic: true
```

---

## ⚙️ Konfigurations-Beispiel (Multi-Board Setup)

Das folgende Beispiel zeigt die Verwendung von **zwei physikalischen HX711-Boards**, an denen jeweils zwei Wägezellen (insgesamt 4 Zellen) betrieben werden. Alle 4 Zellen werden am Ende vollautomatisch zu einem synchronen Gesamtgewicht addiert. Zudem werden parallel zwei Teilsummen für die jeweiligen Achsen gebildet.

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

## 📋 Erzeugte Entitäten im Home Assistant

Nach dem erfolgreichen Kompilieren stellt die Komponente vollautomatisch alle benötigten Steuerelemente und Messwerte im Home Assistant bereit. Bei Verwendung des obigen Beispiels entstehen folgende Entitäten:

### 1. 📊 Sensoren (Gewichte in kg)
*   `sensor.zelle_1_board_1_kanal_a`
*   `sensor.zelle_2_board_1_kanal_b`
*   `sensor.zelle_3_board_2_kanal_a`
*   `sensor.zelle_4_board_2_kanal_b`
*   `sensor.gewicht_achse_1_board_1` (Teilsumme Board 1)
*   `sensor.gewicht_achse_2_board_2` (Teilsumme Board 2)
*   `sensor.cell_compression_gesamtgewicht_alle_4_zellen` (Perfekt synchrone Gesamtsumme)

### 2. 🔒 Sicherheits-Freigabeschalter (Schutz vor Fehlbedienung)
Jede Einzelzelle erhält einen Schalter zur Verriegelung des Nullpunkts. Das Tarieren ist erst möglich, wenn der zugehörige Schalter manuell aktiviert wurde:
*   `switch.zelle_1_board_1_kanal_a_freigabe`
*   `switch.zelle_2_board_1_kanal_b_freigabe`
*   `switch.zelle_3_board_2_kanal_a_freigabe`
*   `switch.zelle_4_board_2_kanal_b_freigabe`

### ⚖️ 3. Taster (Tarieren)
Setzt nach erfolgreicher Freigabe die entsprechende Wägezelle im RAM und dauerhaft im Flash-Speicher (NVS) auf Null. Der zugehörige Freigabeschalter springt nach dem Betätigen sofort automatisch in den gesperrten Zustand zurück:
*   `button.zelle_1_board_1_kanal_a_tarieren`
*   `button.zelle_2_board_1_kanal_b_tarieren`
*   `button.zelle_3_board_2_kanal_a_tarieren`
*   `button.zelle_4_board_2_kanal_b_tarieren`

---

## 💡 Hinweise zur Hardware-Umschaltung

Manche günstigen Nachbauten des HX711-Chips benötigen präzise Signal-Mindestlaufzeiten beim Umschalten. Diese Komponente ist im C++ Quellcode mit einer optimierten Puls-Verzögerung von `2µs` für die Taktimpulse 25 und 26 ausgestattet. Dies garantiert eine fehlerfreie Kanal-Umschaltung auf allen gängigen HX711-Modulen bei voller C++ Ausführungsgeschwindigkeit auf dem ESP32.

---

## 🤖 Credits & Zusammenarbeit

Dieses Projekt ist das Ergebnis einer intensiven Co-Entwicklung zwischen Mensch und Künstlicher Intelligenz:
*   **Idee, Hardware-Tests & Logik-Architektur:** Entwickelt und validiert am lebenden Objekt (LiFePO4-Batteriespeicher) durch den Repository-Inhaber.
*   **Code-Generierung & Dokumentation:** Maßgeschneidert programmiert, iteriert und dokumentiert mithilfe von KI-Assistenz.

Durch dieses agile Zusammenspiel konnte die komplexe, tief verankerte Python- und C++ API von ESPHome in Rekordzeit adaptiert und eine hochperformante, fehlerfreie Multi-Board-Lösung geschaffen werden.

---

## ⚠️ Haftungsausschluss (Disclaimer)

Dieses Projekt ist im Rahmen einer privaten Co-Entwicklung entstanden und dient ausschließlich zu Informations- und Bildungszwecken. 

*   **Keine Garantie:** Der Code wird "wie besehen" (as is) bereitgestellt, ohne jegliche ausdrückliche oder implizite Zusicherung von Funktionalität, Genauigkeit oder Zuverlässigkeit.
*   **Eigenverantwortung:** Die Nutzung dieser Komponente erfolgt vollständig auf eigene Gefahr und eigenes Risiko des Anwenders. 
*   **Schadensersatz:** Der Autor übernimmt keinerlei Haftung für Schäden an Hardware (z. B. Batteriezellen, ESP32-Boards), Datenverluste, finanzielle Verluste oder Personenschäden, die durch die Nutzung, Fehlfunktion oder Modifikation dieses Codes entstehen.

Besonders bei der Überwachung von kritischen Systemen wie Lithium-Eisenphosphat-Speichern (LiFePO4) ist der Anwender selbst dafür verantwortlich, zusätzliche, unabhängige Sicherheitsmechanismen (z. B. mechanische Überdruckventile oder softwareseitige Abschaltungen im BMS) einzurichten.

