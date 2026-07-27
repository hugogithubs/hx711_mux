import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import gpio
from esphome.const import CONF_ID, CONF_CLK_PIN, CONF_DOUT_PIN

# C++ Namespace festlegen
hx711_mux_ns = cg.esphome_ns.namespace("hx711_mux")
HX711MuxHub = hx711_mux_ns.class_("HX711MuxHub", cg.Component)

# Validierung für den Hardware-Hub (hx711_mux:)
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(HX711MuxHub),
        cv.Required(CONF_CLK_PIN): gpio.gpio_output_pin_schema,
        cv.Required(CONF_DOUT_PIN): gpio.gpio_input_pin_schema,
    }
).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_PComponent(HX711MuxHub, config[CONF_ID])
    await cg.register_component(var, config)
    
    clk = await cg.gpio_pin_expression(config[CONF_CLK_PIN])
    cg.add(var.set_clk_pin(clk))
    
    dout = await cg.gpio_pin_expression(config[CONF_DOUT_PIN])
    cg.add(var.set_dout_pin(dout))
