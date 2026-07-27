import esphome.config_validation as cv
import esphome.codegen as cg
from esphome.components import sensor, button
from esphome.const import CONF_CLK_PIN, CONF_DOUT_PIN, CONF_ID, CONF_CHANNEL, CONF_NAME

hx711_mux_ns = cg.esphome_ns.namespace('hx711_mux')
HX711MuxHub = hx711_mux_ns.class_('HX711MuxHub', cg.Component)
HX711MuxSensor = hx711_mux_ns.class_('HX711MuxSensor', sensor.Sensor, cg.Component)
HX711MuxTareButton = hx711_mux_ns.class_('HX711MuxTareButton', button.Button, cg.Component)

CONF_HUB_ID = "hub_id"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(HX711MuxHub),
    cv.Required(CONF_CLK_PIN): cv.InternalGPIOPin,
    cv.Required(CONF_DOUT_PIN): cv.InternalGPIOPin,
}).extend(cv.COMPONENT_SCHEMA)

SENSOR_SCHEMA = sensor.sensor_schema().extend({
    cv.GenerateID(): cv.declare_id(HX711MuxSensor),
    cv.Required(CONF_HUB_ID): cv.use_id(HX711MuxHub),
    cv.Required(CONF_CHANNEL): cv.one_of("A", "B", upper=True),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    if CONF_CLK_PIN in config:
        var = cg.new_Pvariable(config[CONF_ID])
        await cg.register_component(var, config)
        clk = await cg.gpio_pin_expression(config[CONF_CLK_PIN])
        cg.add(var.set_clk_pin(clk))
        dout = await cg.gpio_pin_expression(config[CONF_DOUT_PIN])
        cg.add(var.set_dout_pin(dout))
    else:
        var = cg.new_Pvariable(config[CONF_ID])
        await cg.register_component(var, config)
        await sensor.register_sensor(var, config)
        
        hub = await cg.get_variable(config[CONF_HUB_ID])
        cg.add(var.set_hub(hub))
        cg.add(hub.register_sensor(var))
        
        channel = 0 if config[CONF_CHANNEL] == "A" else 1
        cg.add(var.set_channel(channel))

        # MAGIE: Erzeuge hier vollautomatisch den passenden Tarier-Button im Frontend!
        button_id = cg.ComponentID(f"{config[CONF_ID]}_tare_button")
        btn = cg.new_Pvariable(button_id)
        await cg.register_component(btn, {})
        
        # Vergebe dem Button automatisch den Namen des Sensors + " Tarieren"
        cg.add(btn.set_name(f"{config[CONF_NAME]} Tarieren"))
        cg.add(btn.set_sensor(var))
        await button.register_button(btn, {})
