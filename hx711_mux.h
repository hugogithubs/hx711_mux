#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/button/button.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include <vector>
#include <map>

namespace esphome {
namespace hx711_mux {

class HX711MuxSensor;

// ====================================================================
// 1. DER HARDWARE-HUB (Das HX711-Board mit Bit-Banging)
// ====================================================================
class HX711MuxHub : public Component {
 public:
  void set_clk_pin(InternalGPIOPin *pin) { clk_pin_ = pin; }
  void set_dout_pin(InternalGPIOPin *pin) { dout_pin_ = pin; }
  void register_sensor(HX711MuxSensor *sensor) { sensors_.push_back(sensor); }

  void setup() override {
    ESP_LOGI("hx711_mux", "Initialisiere HX711 Mux Board...");
    clk_pin_->setup();
    clk_pin_->pin_mode(gpio::FLAG_OUTPUT);
    dout_pin_->setup();
    dout_pin_->pin_mode(gpio::FLAG_INPUT);
    clk_pin_->digital_write(false);
    active_channel_ = 0; // Starte standardmäßig mit Kanal A
  }

  void loop() override {
    // Intervall-Steuerung: Liest die Hardware jede Sekunde im Haupt-Loop aus
    uint32_t now = millis();
    if (now - last_read_ > 1000) {
      last_read_ = now;
      read_hardware_();
    }
  }

 protected:
  void read_hardware_() {
    if (dout_pin_->digital_read() == 1) {
      // Warten auf Data Ready mit Timeout (max 250ms)
      uint32_t wait_start = millis();
      while (dout_pin_->digital_read() == 1) {
        if (millis() - wait_start > 250) {
          // Timeout-Behandlung (Reset-Puls >60µs)
          clk_pin_->digital_write(true);
          delayMicroseconds(70);
          clk_pin_->digital_write(false);
          delayMicroseconds(10);
          active_channel_ = 0; // Zurücksetzen auf Startkanal A
          ESP_LOGW("hx711_mux", "Timeout beim Warten auf Data Ready!");
          return;
        }
        delay(1);
      }
    }

    long value = 0;

    // KRITISCHER BEREICH: Interrupts kurz sperren (Maximaler Schutz vor Störungen)
    portMUX_TYPE myMutex = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&myMutex);

    // Die 24 Datenbits takten und einlesen
    for (int i = 0; i < 24; i++) {
      clk_pin_->digital_write(true);
      delayMicroseconds(1);
      value = (value << 1) | (dout_pin_->digital_read() ? 1 : 0);
      clk_pin_->digital_write(false);
      delayMicroseconds(1);
    }

    // Die zusätzliche Puls-Sequenz für die NÄCHSTE Messung senden
    if (active_channel_ == 0) {
      for (int i = 0; i < 2; i++) { // Puls 25 und 26 -> Umschalten auf Kanal B (Gain 32)
        clk_pin_->digital_write(true); delayMicroseconds(1);
        clk_pin_->digital_write(false); delayMicroseconds(1);
      }
    } else {
      clk_pin_->digital_write(true); delayMicroseconds(1); // Puls 25 -> Umschalten auf Kanal A (Gain 128)
      clk_pin_->digital_write(false); delayMicroseconds(1);
    }

    // Kritischen Bereich sofort wieder verlassen
    portEXIT_CRITICAL(&myMutex);

    // Vorzeichenkorrektur (Zweierkomplement für 24-Bit)
    if (value & 0x800000) {
      value |= 0xFF000000;
    }

    // Daten an die registrierten Sensoren übergeben
    for (auto *sensor : sensors_) {
      sensor->handle_raw_value(active_channel_, (float)value);
    }

    // Kanal für den nächsten Durchlauf umschalten
    active_channel_ = (active_channel_ == 0) ? 1 : 0;
  }

  InternalGPIOPin *clk_pin_;
  InternalGPIOPin *dout_pin_;
  uint32_t last_read_{0};
  int active_channel_{0};
  std::vector<HX711MuxSensor *> sensors_;
};

// ====================================================================
// 2. DER SENSOR (Die einzelne Wägezelle mit Flash-Tara NACH den Filtern)
// ====================================================================
class HX711MuxSensor : public sensor::Sensor, public Component {
 public:
  void set_hub(HX711MuxHub *hub) { hub_ = hub; }
  void set_channel(int channel) { target_channel_ = channel; }

  void setup() override {
    // Flash-Speicherplatz vom ESPHome-System reservieren (32-Bit Float für Ticks)
    this->pref_ = global_preferences->make_preference<float>(this->get_object_id_hash());
    if (!this->pref_.load(&this->tare_value_)) {
      this->tare_value_ = 0.0f; 
    }
    ESP_LOGI("hx711_mux", "Geladener Tara-Nullpunkt aus dem Flash: %.0f Ticks", this->tare_value_);

    // REIHENFOLGEN-HOOK: Wir klinken uns DIREKT hinter deiner YAML-Filterkette ein.
    // Sobald Median und Moving-Average fertig sind, fangen wir den Wert ab, 
    // ziehen das Tara ab, und erst DANACH läuft der Wert in dein calibrate_linear!
    this->add_on_raw_value_callback([this](float filtered_raw_ticks) {
        float zero_tracked_ticks = filtered_raw_ticks - this->tare_value_;
        return zero_tracked_ticks;
    });
  }

  void handle_raw_value(int current_channel, float raw_value) {
    if (current_channel == target_channel_) {
      this->last_live_raw_ = raw_value; // Sichert den rohen Hardware-Wert für das Nullen
      this->publish_state(raw_value);   // Schickt die Ticks zuerst in die Filterkette
    }
  }

  void perform_tare() {
    if (this->has_state()) {
        // Berechne den neuen Nullpunkt basierend auf den gefilterten Ticks
        this->tare_value_ = this->state + this->tare_value_; 
        this->pref_.save(&this->tare_value_);
        global_preferences->sync();
        
        // Zwingt den Sensor instantan auf 0 Ticks Differenz (Sofortiges Nullen!)
        this->publish_state(0.0f);
        ESP_LOGI("hx711_mux", "Kanal erfolgreich tariert. Neuer Nullpunkt: %.0f Ticks", this->tare_value_);
    }
  }

 protected:
  HX711MuxHub *hub_;
  int target_channel_;
  float tare_value_{0.0f};
  float last_live_raw_{0.0f};
  ESPPreferenceObject pref_;
};

// ====================================================================
// 3. DER TARIER-BUTTON
// ====================================================================
class HX711MuxTareButton : public button::Button, public Component {
 public:
  void set_sensor(HX711MuxSensor *sensor) { sensor_ = sensor; }
 protected:
  void press_action() override { sensor_->perform_tare(); }
  HX711MuxSensor *sensor_;
};

// ====================================================================
// 4. DER FLEXIBLE N:M SUMMEN-SENSOR (Perfekt synchronisiert)
// ====================================================================
class HX711MuxSumSensor : public sensor::Sensor, public Component {
 public:
  void add_sensor(HX711MuxSensor *sensor) {
    tracked_sensors_.push_back(sensor);
    sensor->add_on_state_callback([this, sensor](float value) {
        this->on_sensor_update_(sensor, value);
    });
  }

  void setup() override {
    for (auto *s : tracked_sensors_) {
      updated_flags_[s] = false;
    }
  }

 protected:
  void on_sensor_update_(HX711MuxSensor *source, float value) {
    updated_flags_[source] = true;

    bool all_updated = true;
    for (auto const& [sensor, has_updated] : updated_flags_) {
      if (!has_updated) {
        all_updated = false;
        break;
      }
    }

    if (all_updated) {
      float gesamt_summe = 0.0f;
      for (auto *s : tracked_sensors_) {
        if (s->has_state()) {
          gesamt_summe += s->state;
        }
      }
      
      this->publish_state(gesamt_summe);

      for (auto *s : tracked_sensors_) {
        updated_flags_[s] = false;
      }
    }
  }

  std::vector<HX711MuxSensor *> tracked_sensors_;
  std::map<HX711MuxSensor *, bool> updated_flags_;
};

}  // namespace hx711_mux
}  // namespace esphome
