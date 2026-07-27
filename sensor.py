import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, button, gpio
from esphome.const import (
    CONF_CLK_PIN,
    CONF_DOUT_PIN,
    CONF_ID,
    CONF_CHANNEL,
    CONF_NAME,
    ICON_SCALE_BALANCE,
    ICON_WEIGHT_KILOGRAM,
    STATE_CLASS_MEASUREMENT,
)

# C++ Namespace und Klassen-Zuweisung (Exakt passend zur hx711_mux.h)
hx711_mux_ns = cg.esphome_ns.namespace("hx711_mux")
HX711MuxHub = hx711_mux_ns.class_("HX711MuxHub", cg.Component)
HX711MuxSensor = hx711_mux_ns.class_("HX711MuxSensor", sensor.Sensor, cg.Component)
HX711MuxTareButton = hx711_mux_ns.class_("HX711MuxTareButton", button.Button, cg.Component)
HX711MuxSumSensor = hx711_mux_ns.class_("HX711MuxSumSensor", sensor.Sensor, cg.Component)

CONF_HUB_ID = "hub_id"
CONF_TRACKS = "tracks"

# 1. VALIDIERUNG FÜR DEN HARDWARE-HUB (hx711_mux:)
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(HX711MuxHub),
        cv.Required(CONF_CLK_PIN): gpio.gpio_output_pin_schema,
        cv.Required(CONF_DOUT_PIN): gpio.gpio_input_pin_schema,
    }
).extend(cv.COMPONENT_SCHEMA)

# 2. VALIDIERUNG FÜR DIE SENSOREN (platform: hx711_mux)
# Unterstützt nun normale Wägezellen (Kanal A/B) ODER einen Summen-Sensor
SENSOR_SCHEMA = cv.typed_schema(
    {
        # Typ 1: Die normale Wägezelle am Hub
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
        
        # Typ 2: Der synchrone Summen-Sensor
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
    default_type="cell", # Wenn der Nutzer nichts angibt, ist es eine normale Wägezelle
)

async def to_code(config):
    # WEICHE 1: Es handelt sich um den Hardware-Hub (hx711_mux:)
    if CONF_CLK_PIN in config:
        var = cg.new_PComponent(HX711MuxHub, config[CONF_ID])
        await cg.register_component(var, config)
        
        clk = await cg.gpio_pin_expression(config[CONF_CLK_PIN])
        cg.add(var.set_clk_pin(clk))
        
        dout = await cg.gpio_pin_expression(config[CONF_DOUT_PIN])
        cg.add(var.set_dout_pin(dout))
        return

    # WEICHE 2: Es handelt sich um den Summen-Sensor (type: sum)
    if config[cv.CONF_TYPE] == "sum":
        var = cg.new_PComponent(HX711MuxSumSensor, config[CONF_ID])
        await cg.register_component(var, config)
        await sensor.register_sensor(var, config)
        
        for tracker_id in config[CONF_TRACKS]:
            tracker = await cg.get_variable(tracker_id)
            cg.add(var.add_sensor(tracker))
        return

    # WEICHE 3: Es handelt sich um eine normale Wägezelle (type: cell)
    var = cg.new_PComponent(HX711MuxSensor, config[CONF_ID])
    await cg.register_component(var, config)
    await sensor.register_sensor(var, config)
    
    hub = await cg.get_variable(config[CONF_HUB_ID])
    cg.add(var.set_hub(hub))
    cg.add(hub.register_sensor(var))
    
    channel = 0 if config[CONF_CHANNEL] == "A" else 1
    cg.add(var.set_channel(channel))

    # DEINE MAGIE: Vollautomatische Erzeugung des Tarier-Buttons im Frontend!
    button_id = config[CONF_ID] + "_tare_button"
    btn = cg.new_PComponent(HX711MuxTareButton, button_id)
    await cg.register_component(btn, {})
    
    # Automatischen Namen und Icon vergeben
    cg.add(btn.set_name(f"{config[CONF_NAME]} Tarieren"))
    cg.add(btn.set_icon(ICON_SCALE_BALANCE))
    cg.add(btn.set_sensor(var))
    await button.register_button(btn, {})
