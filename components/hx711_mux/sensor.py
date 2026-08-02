import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.core as core
from esphome.components import sensor, button, switch

from esphome.const import (
    CONF_ID,
    CONF_CHANNEL,
    CONF_NAME,
    CONF_TYPE,
    STATE_CLASS_MEASUREMENT,
)

hx711_mux_ns = cg.esphome_ns.namespace("hx711_mux")
HX711MuxHub = hx711_mux_ns.class_("HX711MuxHub", cg.Component)
HX711MuxSensor = hx711_mux_ns.class_("HX711MuxSensor", sensor.Sensor, cg.Component)
HX711MuxTareButton = hx711_mux_ns.class_("HX711MuxTareButton", button.Button, cg.Component)
HX711MuxSumSensor = hx711_mux_ns.class_("HX711MuxSumSensor", sensor.Sensor, cg.Component)

# Hier zwingen wir ESPHome, die korrekte TemplateSwitch-Klasse in C++ zu nutzen!
TemplateSwitch = cg.esphome_ns.namespace("template_").class_("TemplateSwitch", switch.Switch, cg.Component)

CONF_HUB_ID = "hub_id"
CONF_TRACKS = "tracks"
CONF_GAIN = "gain"

CONFIG_SCHEMA = cv.typed_schema(
    {
        "cell": sensor.sensor_schema(
            HX711MuxSensor,
            unit_of_measurement="kg",
            icon="mdi:weight-kilogram",
            state_class=STATE_CLASS_MEASUREMENT,
        ).extend(
            {
                cv.Required(CONF_HUB_ID): cv.use_id(HX711MuxHub),
                cv.Required(CONF_CHANNEL): cv.one_of("A", "B", upper=True),
                # Für Kanal A wird der Gain gesetzt; Kanal B lässt den Hub-Pfad unverändert.
                cv.Optional(CONF_GAIN, default="HIGH"): cv.one_of("HIGH", "LOW", upper=True),
            }
        ).extend(cv.COMPONENT_SCHEMA),
        
        "sum": sensor.sensor_schema(
            HX711MuxSumSensor,
            unit_of_measurement="kg",
            icon="mdi:weight-kilogram",
            state_class=STATE_CLASS_MEASUREMENT,
        ).extend(
            {
                cv.Required(CONF_TRACKS): cv.ensure_list(cv.use_id(HX711MuxSensor)),
            }
        ).extend(cv.COMPONENT_SCHEMA),
    },
    default_type="cell",
)

async def register_sensor_component(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await sensor.register_sensor(var, config)
    return var

async def create_tare_unlock_switch(config):
    unlock_id = core.ID(f"{config[CONF_ID]}_tare_unlock", is_declaration=True, type=TemplateSwitch)
    unlock_var = cg.new_Pvariable(unlock_id)

    unlock_schema = switch.switch_schema(TemplateSwitch)
    unlock_config = unlock_schema({
        CONF_ID: unlock_id,
        CONF_NAME: f"{config[CONF_NAME]} Freigabe",
        "icon": "mdi:lock",
    })
    await switch.register_switch(unlock_var, unlock_config)
    cg.add(unlock_var.set_optimistic(True))
    return unlock_var

async def create_tare_button(var, unlock_var, config):
    button_id = core.ID(
        f"{config[CONF_ID]}_tare_button",
        is_declaration=True,
        type=HX711MuxTareButton,
    )
    btn_var = cg.new_Pvariable(button_id)

    button_schema = button.button_schema(HX711MuxTareButton)
    button_config = button_schema({
        CONF_ID: button_id,
        CONF_NAME: f"{config[CONF_NAME]} Tarieren",
        "icon": "mdi:scale-balance",
        "device_class": "restart",
    })
    await button.register_button(btn_var, button_config)
    cg.add(btn_var.set_sensor(var))
    cg.add(btn_var.set_unlock_switch(unlock_var))
    return btn_var

async def register_sum_sensor(config):
    var = await register_sensor_component(config)
    for tracker_id in config[CONF_TRACKS]:
        tracker = await cg.get_variable(tracker_id)
        cg.add(var.add_sensor(tracker))
    return var

async def register_cell_sensor(config):
    var = await register_sensor_component(config)
    hub = await cg.get_variable(config[CONF_HUB_ID])

    if config[CONF_CHANNEL] == "A":
        cg.add(var.set_channel(0))
        cg.add(hub.set_channel_a_gain_high(config[CONF_GAIN] == "HIGH"))
    else:
        cg.add(var.set_channel(1))

    cg.add(var.set_hub(hub))
    cg.add(hub.register_sensor(var))

    unlock_var = await create_tare_unlock_switch(config)
    await create_tare_button(var, unlock_var, config)
    return var

async def to_code(config):
    if config[cv.CONF_TYPE] == "sum":
        await register_sum_sensor(config)
        return

    await register_cell_sensor(config)
