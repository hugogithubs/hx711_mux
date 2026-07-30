#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/button/button.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include "esphome/core/helpers.h"
#include <vector>
#include <map>

namespace esphome {
namespace hx711_mux {

static const char *const TAG = "hx711_mux";
class HX711MuxSensor;

// Pauschale für das schnelle Software-Warmup (20 Werte pro Sensor)
static const size_t WARMUP_SAMPLES_PER_SENSOR = 20;

// ====================================================================
// 1. DER HARDWARE-HUB
// ====================================================================
class HX711MuxHub : public Component {
 public:
  void set_clk_pin(InternalGPIOPin *pin) { clk_pin_ = pin; }
  void set_dout_pin(InternalGPIOPin *pin) { dout_pin_ = pin; }
  void register_sensor(HX711MuxSensor *sensor) { sensors_.push_back(sensor); }

  void increment_initial_reads() {
   initial_reads_completed_++; 
  }

  void setup() override {
    ESP_LOGI(TAG, "Initialisiere HX711 Mux Board GPIOs...");
    clk_pin_->setup();
    dout_pin_->setup();
    clk_pin_->digital_write(false);
    active_channel_ = 0; 
    waiting_for_ready_ = false;
    initial_reads_completed_ = 0;
  }

  void loop() override {
    uint32_t now = millis();
    
    // Berechne die Ziel-Anzahl an Samples: z.B. 2 Sensoren * 20 Samples = 40 Gesamt-Samples
    size_t required_total_samples = sensors_.size() * WARMUP_SAMPLES_PER_SENSOR;
    
    // Dynamischer Turbo-Takt: 500ms während des Warmups, danach 1000ms Normalbetrieb
    uint32_t current_interval = (initial_reads_completed_ < required_total_samples) ? 500 : 1000;

    if (waiting_for_ready_ || (now - last_read_ > current_interval)) {
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
  size_t initial_reads_completed_{0}; // Trackt die empfangenen Samples im Warmup
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
// 3. DER SENSOR (Mit pauschaliertem Warmup-Muting)
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
  }

  void handle_raw_value(int current_channel, float raw_value) {
    if (current_channel == target_channel_) {
      this->last_live_raw_ = raw_value; 
      
      // Zustand prüfen: Ist es der allererste Messwert dieses Kanals?
      if (!this->has_received_first_val_) {
        this->has_received_first_val_ = true;
        
        // Internen Zustand vorbefüllen, damit nachfolgende YAML-Filter gefüttert werden
        this->last_filtered_ticks_ = raw_value - this->tare_value_;
        
        // Dem Hub ein verarbeitetes Sample melden
        this->hub_->increment_initial_reads();
        
        ESP_LOGD(TAG, "'%s': Erster Warmup-Wert empfangen (%.0f). Publikation blockiert für Filter-Befüllung.", this->get_name().c_str(), raw_value);
        return;
      }

      // Inkrementiert das globale Sample-Tracking im Hub während der Turbo-Schleife
      this->hub_->increment_initial_reads();
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
  bool has_received_first_val_{false}; 
  
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
      // 1. Sicheren Hardware-Reset ausführen
      clk_pin_->digital_write(true);
      delayMicroseconds(70); // Länger als 60µs für Power-Down
      clk_pin_->digital_write(false);
      
      // 2. Dem HX711 Zeit zum Aufwachen geben (min. 400µs laut Datenblatt)
      delayMicroseconds(500); 
      
      // 3. Kanal synchronisieren (HX711 startet nach Reset IMMER auf Kanal A / 0)
      active_channel_ = 0; 
      waiting_for_ready_ = false; 
      
      ESP_LOGW(TAG, "Timeout beim Warten auf Data Ready! Hardware-Reset durchgeführt. Starte neu bei Kanal A.");
      return;
    }
    return;
  }

  waiting_for_ready_ = false; 
  uint32_t value = 0;

  // Performanter RAII-Sperrblock
  {
    InterruptLock lock;

    // 1. Die 24 Datenbits auslesen
    for (int i = 0; i < 24; i++) {
      clk_pin_->digital_write(true);
      delayMicroseconds(1);
      value = (value << 1) | (dout_pin_->digital_read() ? 1 : 0);
      clk_pin_->digital_write(false);
      delayMicroseconds(1); 
    }

    // 2. Die Extra-Pulse für den Kanalwechsel senden (Kanal 0 benötigt 2, Kanal 1 benötigt 1)
    int extra_pulses = (active_channel_ == 0) ? 2 : 1;
    for (int i = 0; i < extra_pulses; i++) {
      clk_pin_->digital_write(true); 
      delayMicroseconds(2); 
      clk_pin_->digital_write(false);
      delayMicroseconds(2);
    }
  }

  // Vorzeichenererweiterung für 24-Bit-Zweierkomplement
  if (value & 0x800000ULL) {
    value |= 0xFF000000ULL;
  }

  // 3. Erst HIER in ein echtes vorzeichenbehaftetes int32_t wandeln, 
  // damit der (float)-Cast danach auch negative Werte erzeugt!
  int32_t final_value = static_cast<int32_t>(value);

  for (auto *sensor : sensors_) {
    sensor->handle_raw_value(active_channel_, (float)final_value);
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
    // Bleibt leer – vollkommen zustandsbasiert!
  }

 protected:
  void on_sensor_update_(HX711MuxSensor *source, float value) {
    // Prüfen, ob alle überwachten Kanäle mindestens einen gültigen Zustand haben
    bool ready = true;
    for (auto *s : tracked_sensors_) {
      if (!s->has_state()) {
        ready = false;
        break;
      }
    }

    // Wenn alle Kanäle bereit sind, Summe aus den aktuellsten Filterwerten bilden
    if (ready) {
      float gesamt_summe = 0.0f;
      for (auto *s : tracked_sensors_) {
        gesamt_summe += s->state;
      }
      
      this->publish_state(gesamt_summe);
    }
  }

  std::vector<HX711MuxSensor *> tracked_sensors_;
};

}  // namespace hx711_mux
}  // namespace esphome
