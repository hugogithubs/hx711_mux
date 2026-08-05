#include "hx711_mux.h"

namespace esphome {
namespace hx711_mux {

void HX711MuxHub::notify_warmup_sample_received() {
  this->initial_reads_completed_++;
}

void HX711MuxHub::setup() {
  ESP_LOGI(TAG, "Initialisiere HX711 Mux Board GPIOs...");
  this->initialize_pins_();
  this->wait_for_chip_ready_();
  this->send_initial_sync_pulses_();

  this->active_channel_ = 0;
  this->waiting_for_ready_ = false;
  this->initial_reads_completed_ = 0;

  ESP_LOGI(TAG, "HX711 Hardware erfolgreich auf Kanal A synchronisiert.");
}

void HX711MuxHub::loop() {
  uint32_t now = millis();
  
  if (this->waiting_for_ready_ || (now - this->last_read_ > this->get_poll_interval_ms_())) {
    if (!this->waiting_for_ready_) {
      this->last_read_ = now;
    }
    this->read_hardware_();
  }
}

// ====================================================================
// REALISIERUNG DER HUB-LESEMETHODE
// ====================================================================
void HX711MuxHub::read_hardware_() {
  if (!this->is_data_ready_()) {
    return;
  }

  this->waiting_for_ready_ = false;
  uint32_t value = this->read_raw_value_();

  // Vorzeichenererweiterung für 24-Bit-Zweierkomplement
  if (value & 0x800000ULL) {
    value |= 0xFF000000ULL;
  }

  int32_t final_value = static_cast<int32_t>(value);
  this->dispatch_raw_value_(final_value);
  this->active_channel_ = (this->active_channel_ == 0) ? 1 : 0;
}

uint32_t HX711MuxHub::required_total_samples_() const {
  return static_cast<uint32_t>(this->sensors_.size() * WARMUP_SAMPLES_PER_SENSOR);
}

uint32_t HX711MuxHub::get_poll_interval_ms_() const {
  return this->is_warmup_phase_() ? WARMUP_POLL_INTERVAL_MS : NORMAL_POLL_INTERVAL_MS;
}

bool HX711MuxHub::is_warmup_phase_() const {
  return this->initial_reads_completed_ < this->required_total_samples_();
}

void HX711MuxHub::initialize_pins_() {
  this->clk_pin_->setup();
  this->dout_pin_->setup();
  this->clk_pin_->digital_write(false);
}

void HX711MuxHub::wait_for_chip_ready_() {
  uint32_t start_w = millis();
  while (this->dout_pin_->digital_read() == 1 && (millis() - start_w < 300)) {
    delayMicroseconds(10);
  }
}

void HX711MuxHub::send_initial_sync_pulses_() {
  InterruptLock lock;

  for (int i = 0; i < 24; i++) {
    this->clk_pin_->digital_write(true);
    delayMicroseconds(2);
    this->clk_pin_->digital_write(false);
    delayMicroseconds(2);
  }

  int sync_pulses = (!this->is_a_high_) ? 3 : 1;
  for (int i = 0; i < sync_pulses; i++) {
    this->clk_pin_->digital_write(true);
    delayMicroseconds(2);
    this->clk_pin_->digital_write(false);
    delayMicroseconds(2);
  }
}

bool HX711MuxHub::is_data_ready_() {
  if (this->dout_pin_->digital_read() == 1) {
    if (!this->waiting_for_ready_) {
      this->timeout_start_ = millis();
      this->waiting_for_ready_ = true;
      return false;
    }

    if (millis() - this->timeout_start_ > DATA_READY_TIMEOUT_MS) {
      this->handle_data_ready_timeout_();
    }
    return false;
  }

  return true;
}

void HX711MuxHub::handle_data_ready_timeout_() {
  this->clk_pin_->digital_write(true);
  delayMicroseconds(70);
  this->clk_pin_->digital_write(false);
  delayMicroseconds(500);
  this->active_channel_ = 0;
  this->waiting_for_ready_ = false;
  ESP_LOGW(TAG, "Timeout beim Warten auf Data Ready! Hardware-Reset durchgeführt. Starte neu bei Kanal A.");
}

uint32_t HX711MuxHub::read_raw_value_() {
  uint32_t value = 0;
  {
    InterruptLock lock;

    for (int i = 0; i < 24; i++) {
      this->clk_pin_->digital_write(true);
      delayMicroseconds(2);
      value = (value << 1) | (this->dout_pin_->digital_read() ? 1 : 0);
      this->clk_pin_->digital_write(false);
      delayMicroseconds(2);
    }

    this->send_channel_switch_pulses_();
  }

  return value;
}

int HX711MuxHub::get_channel_switch_pulse_count_() const {
  if (this->active_channel_ == 0) {
    return 2;
  }
  return this->is_a_high_ ? 1 : 3;
}

void HX711MuxHub::send_channel_switch_pulses_() {
  int extra_pulses = this->get_channel_switch_pulse_count_();
  for (int i = 0; i < extra_pulses; i++) {
    this->clk_pin_->digital_write(true);
    delayMicroseconds(2);
    this->clk_pin_->digital_write(false);
    delayMicroseconds(2);
  }
}

void HX711MuxHub::dispatch_raw_value_(int32_t final_value) {
  for (auto *sensor : this->sensors_) {
    sensor->handle_raw_value(this->active_channel_, static_cast<float>(final_value));
  }
}

void HX711MuxTareLogic::load() {
  this->pref_ = global_preferences->make_preference<float>(this->sensor_->get_object_id_hash());
  if (!this->pref_.load(&this->tare_value_)) {
    this->tare_value_ = 0.0f;
  }
  ESP_LOGI(TAG, "'%s': Geladener Tara-Nullpunkt aus dem Flash: %.0f Ticks", this->sensor_->get_name().c_str(), this->tare_value_);
}

void HX711MuxTareLogic::perform_tare(float filtered_value) {
  this->tare_value_ = filtered_value;
  this->pref_.save(&this->tare_value_);
  global_preferences->sync();
}

float HX711MuxTareLogic::apply(float raw_value) const {
  return raw_value - this->tare_value_;
}

optional<float> MuxTareFilter::new_value(float value) {
  this->parent_->last_filtered_ticks_ = value;
  return value - this->parent_->current_tare_value();
}

HX711MuxSensor::HX711MuxSensor() : tare_logic_(this), tare_filter_(this) {}

void HX711MuxSensor::setup() {
  this->tare_logic_.load();
  this->add_filter(&this->tare_filter_);
}

void HX711MuxSensor::handle_raw_value(int current_channel, float raw_value) {
  if (current_channel != target_channel_) {
    return;
  }

  this->last_live_raw_ = raw_value;
  if (!this->has_received_first_val_) {
    this->has_received_first_val_ = true;
    this->handle_first_measurement(raw_value);
    return;
  }

  this->handle_regular_measurement(raw_value);
}

void HX711MuxSensor::handle_first_measurement(float raw_value) {
  // Internen Zustand vorbefüllen, damit nachfolgende YAML-Filter gefüttert werden
  this->last_filtered_ticks_ = this->tare_logic_.apply(raw_value);

  // Dem Hub ein verarbeitetes Sample melden
  this->hub_->notify_warmup_sample_received();
  ESP_LOGD(TAG, "'%s': Erster Warmup-Wert empfangen (%.0f). Publikation blockiert für Filter-Befüllung.", this->get_name().c_str(), raw_value);
}

void HX711MuxSensor::handle_regular_measurement(float raw_value) {
  // Inkrementiert das globale Sample-Tracking im Hub während der Turbo-Schleife
  this->hub_->notify_warmup_sample_received();
  this->publish_state(raw_value);
}

void HX711MuxSensor::persist_tare_value() {
  this->tare_logic_.perform_tare(this->last_filtered_ticks_);
}

void HX711MuxSensor::perform_tare() {
  this->persist_tare_value();
  
  this->publish_state(this->last_live_raw_);
  ESP_LOGI(TAG, "'%s': Kanal erfolgreich tariert. Neuer Nullpunkt: %.0f Ticks", this->get_name().c_str(), this->tare_logic_.current_tare_value());
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
  // Die Sensoren nach Kanal sortiert ablegen, damit die Summe nur einmal pro Messrunde
  // veröffentlicht wird, nämlich beim letzten Kanal der Runde.
  size_t insert_at = tracked_sensors_.size();
  for (size_t i = 0; i < tracked_sensors_.size(); ++i) {
    if (tracked_sensors_[i]->get_channel() > sensor->get_channel()) {
      insert_at = i;
      break;
    }
  }
  tracked_sensors_.insert(tracked_sensors_.begin() + insert_at, sensor);
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

  // Nur beim letzten Kanal der Runde die Summe berechnen.
  if (!ready || source != tracked_sensors_.back()) {
    return;
  }

  float gesamt_summe = 0.0f;
  for (auto *s : tracked_sensors_) {
    gesamt_summe += s->state;
  }
  
  this->publish_state(gesamt_summe);
}

}  // namespace hx711_mux
}  // namespace esphome
