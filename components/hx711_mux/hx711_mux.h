#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/button/button.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include <vector>
#include <map>

#ifdef USE_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace esphome {
namespace hx711_mux {

static const char *const TAG = "hx711_mux";
class HX711MuxSensor;

// ====================================================================
// 1. DER HARDWARE-HUB
// ====================================================================
class HX711MuxHub : public Component {
 public:
  void set_clk_pin(InternalGPIOPin *pin) { clk_pin_ = pin; }
  void set_dout_pin(InternalGPIOPin *pin) { dout_pin_ = pin; }
  void register_sensor(HX711MuxSensor *sensor) { sensors_.push_back(sensor); }

  void setup() override {
    ESP_LOGI(TAG, "Initialisiere HX711 Mux Board GPIOs...");
    clk_pin_->setup();
    dout_pin_->setup();
    clk_pin_->digital_write(false);
    active_channel_ = 0; 
  }

  void loop() override {
    uint32_t now = millis();
    if (now - last_read_ > 1000) {
      last_read_ = now;
      read_hardware_();
    }
  }

 protected:
  void read_hardware_();

  InternalGPIOPin *clk_pin_;
  InternalGPIOPin *dout_pin_;
  uint32_t last_read_{0};
  int active_channel_{0};
  std::vector<HX711MuxSensor *> sensors_;
};

// ====================================================================
// 2. DER SENSOR (Glättung im YAML -> Tara in C++ -> Scaling im YAML)
// ====================================================================
class HX711MuxSensor : public sensor::Sensor, public Component {
 public:
  void set_hub(HX711MuxHub *hub) { hub_ = hub; }
  void set_channel(int channel) { target_channel_ = channel; }

  void setup() override {
    this->pref_ = global_preferences->make_preference<float>(this->get_object_id_hash());
    if (!this->pref_.load(&this->tare_value_)) {
      this->tare_value_ = 0.0f; 
    }
    ESP_LOGI(TAG, "'%s': Geladener Tara-Nullpunkt aus dem Flash: %.0f Ticks", this->get_name().c_str(), this->tare_value_);

    class MuxTareFilter : public sensor::Filter {
     public:
      MuxTareFilter(HX711MuxSensor *parent) : parent_(parent) {}
      
      // KORREKTUR: "optional" muss zwingend kleingeschrieben werden, passend zur Basisklasse!
      optional<float> new_value(float value) override {
        // Sichert die gefilterten Ticks direkt vor dem Abzug für perform_tare()
        parent_->last_filtered_ticks_ = value;
        return value - parent_->tare_value_;
      }
     protected:
      HX711MuxSensor *parent_;
    };

    // Filter registrieren (Reihenfolge bleibt perfekt erhalten!)
    this->add_filter(new MuxTareFilter(this));
  }

  void handle_raw_value(int current_channel, float raw_value) {
    if (current_channel == target_channel_) {
      this->last_live_raw_ = raw_value; // Sichert den absolut rohen Hardware-Wert in Ticks
      this->publish_state(raw_value);   // Schickt die Ticks unberührt in die YAML-Glättungsfilter
    }
  }

  void perform_tare() {
    // Mathematisch exakt: Der neue Nullpunkt setzt auf den gefilterten Ticks auf
    this->tare_value_ = this->last_filtered_ticks_; 
    this->pref_.save(&this->tare_value_);
    global_preferences->sync();
    
    // INSTANTAN-RESET: Schickt den aktuellen Rohwert direkt noch mal los,
    // zieht das neue Tara ab und drückt die Kurve rechtwinklig auf Null!
    this->publish_state(this->last_live_raw_);
    ESP_LOGI(TAG, "'%s': Kanal erfolgreich tariert. Neuer Nullpunkt: %.0f Ticks", this->get_name().c_str(), this->tare_value_);
  }

 public: // Für das Filter-Objekt freigeben
  float tare_value_{0.0f};
  float last_filtered_ticks_{0.0f};

 protected:
  HX711MuxHub *hub_;
  int target_channel_;
  float last_live_raw_{0.0f};
  ESPPreferenceObject pref_;
};

// ====================================================================
// REALISIERUNG DER HUB-LESEMETHODE
// ====================================================================
inline void HX711MuxHub::read_hardware_() {
  if (dout_pin_->digital_read() == 1) {
    uint32_t wait_start = millis();
    while (dout_pin_->digital_read() == 1) {
      if (millis() - wait_start > 250) {
        clk_pin_->digital_write(true);
        delayMicroseconds(70);
        clk_pin_->digital_write(false);
        delayMicroseconds(10);
        active_channel_ = 0; 
        ESP_LOGW(TAG, "Timeout beim Warten auf Data Ready!");
        return;
      }
        delay(1);
    }
  }

  long value = 0;

#ifdef USE_ESP32
  static portMUX_TYPE myMutex = portMUX_INITIALIZER_UNLOCKED;
  portENTER_CRITICAL(&myMutex);
#endif

  for (int i = 0; i < 24; i++) {
    clk_pin_->digital_write(true);
    delayMicroseconds(1);
    value = (value << 1) | (dout_pin_->digital_read() ? 1 : 0);
    clk_pin_->digital_write(false);
    delayMicroseconds(1);
  }

  if (active_channel_ == 0) {
    for (int i = 0; i < 2; i++) { 
      clk_pin_->digital_write(true); delayMicroseconds(2); 
      clk_pin_->digital_write(false); delayMicroseconds(2);
    }
  } else {
    clk_pin_->digital_write(true); delayMicroseconds(2);
    clk_pin_->digital_write(false); delayMicroseconds(2);
  }

#ifdef USE_ESP32
  portEXIT_CRITICAL(&myMutex);
#endif

  if (value & 0x800000) {
    value |= 0xFF000000;
  }

  for (auto *sensor : sensors_) {
    sensor->handle_raw_value(active_channel_, (float)value);
  }

  active_channel_ = (active_channel_ == 0) ? 1 : 0;
}

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

inline void HX711MuxButton_press_action_fix() {
  // Verhindert Linker-Fehler
}

// ====================================================================
// 4. DER FLEXIBLE N:M SUMMEN-SENSOR
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
