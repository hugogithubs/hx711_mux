import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.const import CONF_ID, CONF_CLK_PIN

# C++ Namespace und Hub-Klasse deklarieren
hx711_mux_ns = cg.esphome_ns.namespace("hx711_mux")
HX711MuxHub = hx711_mux_ns.class_("HX711MuxHub", cg.Component)

# ERZWINGUNGS-TRICK: Sagt dem ESPHome-Builder, dass dieses Projekt zwingend 
# das button- und sensor-Framework einkompilieren muss!
DEPENDENCIES = ["sensor", "button"]

CONF_DOUT_PIN = "dout_pin"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(HX711MuxHub),
        cv.Required(CONF_CLK_PIN): pins.gpio_output_pin_schema,
        cv.Required(CONF_DOUT_PIN): pins.gpio_input_pin_schema,
    }
).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    
    clk = await cg.gpio_pin_expression(config[CONF_CLK_PIN])
    cg.add(var.set_clk_pin(clk))
    
    dout = await cg.gpio_pin_expression(config[CONF_DOUT_PIN])
    cg.add(var.set_dout_pin(dout))
