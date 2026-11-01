import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c
from esphome.components.lvgl import defines as lv
from esphome.const import CONF_ID

DEPENDENCIES = ["i2c", "web_server", "mqtt", "wifi", "lvgl"]
AUTO_LOAD = ["json"]
ns = cg.esphome_ns.namespace("desk_buddy")
DeskBuddy = ns.class_("DeskBuddy", cg.PollingComponent, i2c.I2CDevice)
def enable_eye_widgets(config):
    lv.add_lv_use("CANVAS", "IMAGE", "LABEL", "LINE")
    return config

CONFIG_SCHEMA = cv.All(cv.Schema({cv.GenerateID(): cv.declare_id(DeskBuddy)}).extend(
    cv.polling_component_schema("1s")
).extend(i2c.i2c_device_schema(0x34)), enable_eye_widgets)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
