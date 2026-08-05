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

class HX711MuxTareLogic {
 public:
  explicit HX711MuxTareLogic(HX711MuxSensor *sensor) : sensor_(sensor) {}

  void load();
  void perform_tare(float filtered_value);
  float apply(float raw_value) const;
  float current_tare_value() const { return tare_value_; }

 private:
  HX711MuxSensor *sensor_;
  float tare_value_{0.0f};
  ESPPreferenceObject pref_;
};

// Timing- und Warmup-Konstanten
static constexpr size_t WARMUP_SAMPLES_PER_SENSOR = 20;
static constexpr uint32_t WARMUP_POLL_INTERVAL_MS = 500;
static constexpr uint32_t NORMAL_POLL_INTERVAL_MS = 1000;
static constexpr uint32_t DATA_READY_TIMEOUT_MS = 250;

// ====================================================================
// 1. DER HARDWARE-HUB
// ====================================================================
class HX711MuxHub : public Component {
 public:
  void set_clk_pin(InternalGPIOPin *pin) { clk_pin_ = pin; }
  void set_dout_pin(InternalGPIOPin *pin) { dout_pin_ = pin; }
  void register_sensor(HX711MuxSensor *sensor) { sensors_.push_back(sensor); }
  void set_channel_a_gain_high(bool is_high) { is_a_high_ = is_high; }

  void notify_warmup_sample_received();
  void setup() override;
  void loop() override;

 protected:
  uint32_t required_total_samples_() const;
  uint32_t get_poll_interval_ms_() const;
  bool is_warmup_phase_() const;

  void initialize_pins_();
  void wait_for_chip_ready_();
  void send_initial_sync_pulses_();
  bool is_data_ready_();
  void handle_data_ready_timeout_();
  uint32_t read_raw_value_();
  int get_channel_switch_pulse_count_() const;
  void send_channel_switch_pulses_();
  void dispatch_raw_value_(int32_t final_value);

  void read_hardware_();

  InternalGPIOPin *clk_pin_;
  InternalGPIOPin *dout_pin_;
  uint32_t last_read_{0};
  int active_channel_{0};
  bool is_a_high_{true};
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
  HX711MuxSensor();

  void set_hub(HX711MuxHub *hub) { hub_ = hub; }
  void set_channel(int channel) { target_channel_ = channel; }
  int get_channel() const { return target_channel_; }

  void setup() override;
  void handle_raw_value(int current_channel, float raw_value);
  void perform_tare();

 protected:
  void persist_tare_value();
  void handle_first_measurement(float raw_value);
  void handle_regular_measurement(float raw_value);

 public:
  float last_filtered_ticks_{0.0f};
  float current_tare_value() const { return tare_logic_.current_tare_value(); }

 protected:
  HX711MuxHub *hub_{nullptr};
  int target_channel_{-1};
  float last_live_raw_{0.0f};
  bool has_received_first_val_{false}; 
  
  HX711MuxTareLogic tare_logic_;
  MuxTareFilter tare_filter_; 
};

// ====================================================================
// 4. DER TARIER-BUTTON
// ====================================================================
class HX711MuxTareButton : public button::Button, public Component {
 public:
  void set_sensor(HX711MuxSensor *sensor) { sensor_ = sensor; }
  void set_unlock_switch(switch_::Switch *unlock_switch) { unlock_switch_ = unlock_switch; }

 protected:
  void press_action() override;

  HX711MuxSensor *sensor_{nullptr};
  switch_::Switch *unlock_switch_{nullptr};
};

// ====================================================================
// 5. DER FLEXIBLE N:M SUMMEN-SENSOR
// ====================================================================
class HX711MuxSumSensor : public sensor::Sensor, public Component {
 public:
  void add_sensor(HX711MuxSensor *sensor);
  void setup() override;

 protected:
  void on_sensor_update_(HX711MuxSensor *source, float value);

  std::vector<HX711MuxSensor *> tracked_sensors_;
};

}  // namespace hx711_mux
}  // namespace esphome
