#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/button/button.h"
#include "esphome/components/switch/switch.h"
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
    waiting_for_ready_ = false;
  }

  void loop() override {
    uint32_t now = millis();
    if (waiting_for_ready_ || (now - last_read_ > 1000)) {
      if (!waiting_for_ready_) {
        last_read_ = now; 
      }
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
  
  uint32_t timeout_start_{0};
  bool waiting_for_ready_{false};
};

// ====================================================================
// 2. DER DEDIZIERTE TARA-FILTER
// ====================================================================
class MuxTareFilter : public sensor::Filter {
 public:
  MuxTareFilter(HX711MuxSensor *parent) : parent_(parent) {}
  optional<float> new_value(float value) override;
 protected:
  HX711MuxSensor *parent_;
};

// ====================================================================
// 3. DER SENSOR (Jetzt mit Boot-Muting gegen Start-Ausreißer)
// ====================================================================
class HX711MuxSensor : public sensor::Sensor, public Component {
 public:
  HX711MuxSensor() : tare_filter_(this) {}

  void set_hub(HX711MuxHub *hub) { hub_ = hub; }
  void set_channel(int channel) { target_channel_ = channel; }

  void setup() override {
    this->pref_ = global_preferences->make_preference<float>(this->get_object_id_hash());
    if (!this->pref_.load(&this->tare_value_)) {
      this->tare_value_ = 0.0f; 
    }
    ESP_LOGI(TAG, "'%s': Geladener Tara-Nullpunkt aus dem Flash: %.0f Ticks", this->get_name().c_str(), this->tare_value_);

    this->add_filter(&this->tare_filter_);
    this->boot_timestamp_ = millis(); // Boot-Zeitpunkt merken
  }

  void handle_raw_value(int current_channel, float raw_value) {
    if (current_channel == target_channel_) {
      this->last_live_raw_ = raw_value; 
      
      // Sicherheits-Check: Die ersten 5 Sekunden nach dem Booten keine Werte publizieren
      // Dadurch füllen sich erst die YAML-Filter (Median/Moving Average) mit sauberen Daten
      if (millis() - this->boot_timestamp_ < 5000) {
        // Wir aktualisieren nur den Filter-Speicher, senden aber nix an HA/Summensensor
        float temp_filtered = raw_value - this->tare_value_;
        this->last_filtered_ticks_ = raw_value; 
        ESP_LOGD(TAG, "'%s': Ignoriere Start-Wert während Stabilisierungsphase: %.0f", this->get_name().c_str(), raw_value);
        return; 
      }

      this->publish_state(raw_value);   
    }
  }

  void perform_tare() {
    this->tare_value_ = this->last_filtered_ticks_; 
    this->pref_.save(&this->tare_value_);
    global_preferences->sync();
    
    this->publish_state(this->last_live_raw_);
    ESP_LOGI(TAG, "'%s': Kanal erfolgreich tariert. Neuer Nullpunkt: %.0f Ticks", this->get_name().c_str(), this->tare_value_);
  }

 public:
  float tare_value_{0.0f};
  float last_filtered_ticks_{0.0f};

 protected:
  HX711MuxHub *hub_;
  int target_channel_;
  float last_live_raw_{0.0f};
  ESPPreferenceObject pref_;
  uint32_t boot_timestamp_{0}; // Speicher für die Boot-Verzögerung
  
  MuxTareFilter tare_filter_; 
};

inline optional<float> MuxTareFilter::new_value(float value) {
  this->parent_->last_filtered_ticks_ = value;
  return value - this->parent_->tare_value_;
}

// ====================================================================
// REALISIERUNG DER HUB-LESEMETHODE
// ====================================================================
inline void HX711MuxHub::read_hardware_() {
  if (dout_pin_->digital_read() == 1) {
    if (!waiting_for_ready_) {
      timeout_start_ = millis();
      waiting_for_ready_ = true;
      return;
    }
    
    if (millis() - timeout_start_ > 250) {
      clk_pin_->digital_write(true);
      delayMicroseconds(70);
      clk_pin_->digital_write(false);
      delayMicroseconds(10);
      active_channel_ = 0; 
      waiting_for_ready_ = false; 
      ESP_LOGW(TAG, "Timeout beim Warten auf Data Ready! Reset-Puls gesendet.");
      return;
    }
    return;
  }

  waiting_for_ready_ = false; 
  long value = 0;

#ifdef USE_ESP32
  static portMUX_TYPE myMutex = portMUX_INITIALIZER_UNLOCKED;
#endif

  for (int i = 0; i < 24; i++) {
#ifdef USE_ESP32
    portENTER_CRITICAL(&myMutex);
#endif

    clk_pin_->digital_write(true);
    delayMicroseconds(1);
    value = (value << 1) | (dout_pin_->digital_read() ? 1 : 0);
    clk_pin_->digital_write(false);

#ifdef USE_ESP32
    portEXIT_CRITICAL(&myMutex);
#endif
    delayMicroseconds(1); 
  }

  if (active_channel_ == 0) {
    for (int i = 0; i < 2; i++) { 
#ifdef USE_ESP32
      portENTER_CRITICAL(&myMutex);
#endif
      clk_pin_->digital_write(true); delayMicroseconds(2); 
      clk_pin_->digital_write(false);
#ifdef USE_ESP32
      portEXIT_CRITICAL(&myMutex);
#endif
      delayMicroseconds(2);
    }
  } else {
#ifdef USE_ESP32
    portENTER_CRITICAL(&myMutex);
#endif
    clk_pin_->digital_write(true); delayMicroseconds(2);
    clk_pin_->digital_write(false);
#ifdef USE_ESP32
    portEXIT_CRITICAL(&myMutex);
#endif
    delayMicroseconds(2);
  }

  if (value & 0x800000) {
    value |= 0xFF000000;
  }

  for (auto *sensor : sensors_) {
    sensor->handle_raw_value(active_channel_, (float)value);
  }

  active_channel_ = (active_channel_ == 0) ? 1 : 0;
}

// ====================================================================
// 4. DER TARIER-BUTTON
// ====================================================================
class HX711MuxTareButton : public button::Button, public Component {
 public:
  void set_sensor(HX711MuxSensor *sensor) { sensor_ = sensor; }
  void set_unlock_switch(switch_::Switch *unlock_switch) { unlock_switch_ = unlock_switch; }

 protected:
  void press_action() override {
    if (this->unlock_switch_ != nullptr && this->unlock_switch_->state) {
      ESP_LOGI(TAG, "'%s': Freigabe aktiv. Führe Tarieren aus!", this->get_name().c_str());
      
      if (this->sensor_ != nullptr) {
        this->sensor_->perform_tare(); 
      }
   
      this->unlock_switch_->turn_off(); 
    } else {
      ESP_LOGW(TAG, "'%s': Tarieren blockiert! Bitte zuerst den Freigabe-Schalter einschalten.", this->get_name().c_str());
    }
  }

  HX711MuxSensor *sensor_{nullptr};
  switch_::Switch *unlock_switch_{nullptr};
};

// ====================================================================
// 5. DER FLEXIBLE N:M SUMMEN-SENSOR
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
