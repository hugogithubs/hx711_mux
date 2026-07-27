import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, button
from esphome.const import (
    CONF_ID,
    CONF_CHANNEL,
    CONF_NAME,
    ICON_SCALE_BALANCE,
    ICON_WEIGHT_KILOGRAM,
    STATE_CLASS_MEASUREMENT,
)

# Namespace aus der __init__.py importieren
from . import hx711_mux_ns, HX711MuxHub

# Weitere Klassen deklarieren
HX711MuxSensor = hx711_mux_ns.class_("HX711MuxSensor", sensor.Sensor, cg.Component)
HX711MuxTareButton = hx711_mux_ns.class_("HX711MuxTareButton", button.Button, cg.Component)
HX711MuxSumSensor = hx711_mux_ns.class_("HX711MuxSumSensor", sensor.Sensor, cg.Component)

CONF_HUB_ID = "hub_id"
CONF_TRACKS = "tracks"

# Validierung für die Sensoren (platform: hx711_mux)
# Unterscheidet sauber zwischen "type: cell" und "type: sum"
SENSOR_SCHEMA = cv.typed_schema(
    {
        "cell": sensor.sensor_schema(
            icon=ICON_WEIGHT_KILOGRAM,
            state_class=STATE_CLASS_MEASUREMENT,
        ).extend(
            {
                cv.GenerateID(): cv.declare_id(HX711MuxSensor),
                cv.Required(CONF_HUB_ID): cv.use_id(HX711MuxHub),
                cv.Required(CONF_CHANNEL): cv.one_of("A", "B", upper=True),
            }
        ).extend(cv.COMPONENT_SCHEMA),
        
        "sum": sensor.sensor_schema(
            icon=ICON_WEIGHT_KILOGRAM,
            state_class=STATE_CLASS_MEASUREMENT,
        ).extend(
            {
                cv.GenerateID(): cv.declare_id(HX711MuxSumSensor),
                cv.Required(CONF_TRACKS): cv.ensure_list(cv.use_id(HX711MuxSensor)),
            }
        ).extend(cv.COMPONENT_SCHEMA),
    },
    default_type="cell",
)

async def to_code(config):
    # Weiche für den Summen-Sensor
    if config[cv.CONF_TYPE] == "sum":
        var = cg.new_PComponent(HX711MuxSumSensor, config[CONF_ID])
        await cg.register_component(var, config)
        await sensor.register_sensor(var, config)
        
        for tracker_id in config[CONF_TRACKS]:
            tracker = await cg.get_variable(tracker_id)
            cg.add(var.add_sensor(tracker))
        return

    # Standard-Weiche für eine normale Wägezelle (cell)
    var = cg.new_PComponent(HX711MuxSensor, config[CONF_ID])
    await cg.register_component(var, config)
    await sensor.register_sensor(var, config)
    
    hub = await cg.get_variable(config[CONF_HUB_ID])
    cg.add(var.set_hub(hub))
    cg.add(hub.register_sensor(var))
    
    channel = 0 if config[CONF_CHANNEL] == "A" else 1
    cg.add(var.set_channel(channel))

    # Die automatische Button-Erstellung im Frontend
    button_id = config[CONF_ID] + "_tare_button"
    btn = cg.new_PComponent(HX711MuxTareButton, button_id)
    await cg.register_component(btn, {})
    
    cg.add(btn.set_name(f"{config[CONF_NAME]} Tarieren"))
    cg.add(btn.set_icon(ICON_SCALE_BALANCE))
    cg.add(btn.set_sensor(var))
    await button.register_button(btn, {})
