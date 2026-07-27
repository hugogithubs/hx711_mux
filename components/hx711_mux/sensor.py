import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.core as core
from esphome.components import sensor, button
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

CONF_HUB_ID = "hub_id"
CONF_TRACKS = "tracks"

# MULTI-TYP-SCHEMA: Unterscheidet anhand von "type" schon bei der Validierung den C++ Typen!
CONFIG_SCHEMA = cv.typed_schema(
    {
        # Typ 1: Normale Wägezelle (Standard)
        "cell": sensor.sensor_schema(
            HX711MuxSensor,
            icon="mdi:weight-kilogram",
            state_class=STATE_CLASS_MEASUREMENT,
        ).extend(
            {
                cv.Required(CONF_HUB_ID): cv.use_id(HX711MuxHub),
                cv.Required(CONF_CHANNEL): cv.one_of("A", "B", upper=True),
            }
        ).extend(cv.COMPONENT_SCHEMA),
        
        # Typ 2: Der synchrone Summensensor
        "sum": sensor.sensor_schema(
            HX711MuxSumSensor, # <-- Hier erfährt der Builder sofort den richtigen C++ Typen!
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

async def to_code(config):
    # Fall 1: Summen-Sensor
    if config[cv.CONF_TYPE] == "sum":
        var = cg.new_Pvariable(config[CONF_ID])
        await cg.register_component(var, config)
        await sensor.register_sensor(var, config)
        
        for tracker_id in config[CONF_TRACKS]:
            tracker = await cg.get_variable(tracker_id)
            cg.add(var.add_sensor(tracker))
        return

    # Fall 2: Normale Wägezelle
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await sensor.register_sensor(var, config)
    
    hub = await cg.get_variable(config[CONF_HUB_ID])
    cg.add(var.set_hub(hub))
    cg.add(hub.register_sensor(var))
    
    channel = 0 if config[CONF_CHANNEL] == "A" else 1
    cg.add(var.set_channel(channel))

    # Erzeugt das ID-Objekt für den Button
    button_id = core.ID(
        f"{config[CONF_ID]}_tare_button", 
        is_declaration=True, 
        type=HX711MuxTareButton
    )
    
    btn_var = cg.new_Pvariable(button_id)
    
    button_schema = button.button_schema(HX711MuxTareButton)
    button_config = button_schema({
        CONF_ID: button_id,
        CONF_NAME: f"{config[CONF_NAME]} Tarieren",
        "icon": "mdi:scale-balance",
    })
    
    cg.add(btn_var.set_sensor(var))
    await button.register_button(btn_var, button_config)
