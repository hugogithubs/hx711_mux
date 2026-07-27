import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, button
from esphome.const import (
    CONF_ID,
    CONF_CHANNEL,
    CONF_NAME,
    CONF_TYPE,
    ICON_SCALE_BALANCE,
    ICON_WEIGHT_KILOGRAM,
    STATE_CLASS_MEASUREMENT,
)

hx711_mux_ns = cg.esphome_ns.namespace("hx711_mux")
HX711MuxHub = hx711_mux_ns.class_("HX711MuxHub", cg.Component)
HX711MuxSensor = hx711_mux_ns.class_("HX711MuxSensor", sensor.Sensor, cg.Component)
HX711MuxTareButton = hx711_mux_ns.class_("HX711MuxTareButton", button.Button, cg.Component)
HX711MuxSumSensor = hx711_mux_ns.class_("HX711MuxSumSensor", sensor.Sensor, cg.Component)

CONF_HUB_ID = "hub_id"
CONF_TRACKS = "tracks"

# Das primäre SENSOR_SCHEMA (Muss flach sein für den Core-Validator!)
CONFIG_SCHEMA = sensor.sensor_schema(
    icon=ICON_WEIGHT_KILOGRAM,
    state_class=STATE_CLASS_MEASUREMENT,
).extend(
    {
        cv.GenerateID(): cv.declare_id(HX711MuxSensor),
        cv.Optional(CONF_HUB_ID): cv.use_id(HX711MuxHub),
        cv.Optional(CONF_CHANNEL): cv.one_of("A", "B", upper=True),
        cv.Optional(CONF_TRACKS): cv.ensure_list(cv.use_id(HX711MuxSensor)),
        cv.Optional(CONF_TYPE, default="cell"): cv.one_of("cell", "sum"),
    }
).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    # FALL 1: Es ist ein Summen-Sensor
    if config[CONF_TYPE] == "sum" or CONF_TRACKS in config:
        var = cg.new_PComponent(HX711MuxSumSensor, config[CONF_ID])
        await cg.register_component(var, config)
        await sensor.register_sensor(var, config)
        
        for tracker_id in config[CONF_TRACKS]:
            tracker = await cg.get_variable(tracker_id)
            cg.add(var.add_sensor(tracker))
        return

    # FALL 2: Es ist eine normale Wägezelle
    var = cg.new_PComponent(HX711MuxSensor, config[CONF_ID])
    await cg.register_component(var, config)
    await sensor.register_sensor(var, config)
    
    hub = await cg.get_variable(config[CONF_HUB_ID])
    cg.add(var.set_hub(hub))
    cg.add(hub.register_sensor(var))
    
    channel = 0 if config[CONF_CHANNEL] == "A" else 1
    cg.add(var.set_channel(channel))

    # Button automatisch erzeugen
    button_id = config[CONF_ID] + "_tare_button"
    btn = cg.new_PComponent(HX711MuxTareButton, button_id)
    await cg.register_component(btn, {})
    
    cg.add(btn.set_name(f"{config[CONF_NAME]} Tarieren"))
    cg.add(btn.set_icon(ICON_SCALE_BALANCE))
    cg.add(btn.set_sensor(var))
    await button.register_button(btn, {})
