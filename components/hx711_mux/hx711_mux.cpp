#include "hx711_mux.h"

namespace esphome {
namespace hx711_mux {

void HX711MuxHub::increment_initial_reads() {
 this->initial_reads_completed_++; 
}

void HX711MuxHub::setup() {
  ESP_LOGI(TAG, "Initialisiere HX711 Mux Board GPIOs...");
  clk_pin_->setup();
  dout_pin_->setup();
  clk_pin_->digital_write(false);
  active_channel_ = 0; 
  waiting_for_ready_ = false;
  initial_reads_completed_ = 0;
}

void HX711MuxHub::loop() {
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

// ====================================================================
// REALISIERUNG DER HUB-LESEMETHODE
// ====================================================================
void HX711MuxHub::read_hardware_() {
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

optional<float> MuxTareFilter::new_value(float value) {
  this->parent_->last_filtered_ticks_ = value;
  return value - this->parent_->tare_value_;
}

HX711MuxSensor::HX711MuxSensor() : tare_filter_(this) {}

void HX711MuxSensor::setup() {
  this->pref_ = global_preferences->make_preference<float>(this->get_object_id_hash());
  if (!this->pref_.load(&this->tare_value_)) {
    this->tare_value_ = 0.0f; 
  }
  ESP_LOGI(TAG, "'%s': Geladener Tara-Nullpunkt aus dem Flash: %.0f Ticks", this->get_name().c_str(), this->tare_value_);

  this->add_filter(&this->tare_filter_);
}

void HX711MuxSensor::handle_raw_value(int current_channel, float raw_value) {
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

void HX711MuxSensor::perform_tare() {
  this->tare_value_ = this->last_filtered_ticks_; 
  this->pref_.save(&this->tare_value_);
  global_preferences->sync();
  
  this->publish_state(this->last_live_raw_);
  ESP_LOGI(TAG, "'%s': Kanal erfolgreich tariert. Neuer Nullpunkt: %.0f Ticks", this->get_name().c_str(), this->tare_value_);
}

void HX711MuxTareButton::press_action() {
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

void HX711MuxSumSensor::add_sensor(HX711MuxSensor *sensor) {
  tracked_sensors_.push_back(sensor);
  sensor->add_on_state_callback([this, sensor](float value) {
      this->on_sensor_update_(sensor, value);
  });
}

void HX711MuxSumSensor::setup() {
  // Bleibt leer – vollkommen zustandsbasiert!
}

void HX711MuxSumSensor::on_sensor_update_(HX711MuxSensor *source, float value) {
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

}  // namespace hx711_mux
}  // namespace esphome
