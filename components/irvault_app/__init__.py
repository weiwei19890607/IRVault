import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["api", "esp32", "wifi"]

irvault_ns = cg.esphome_ns.namespace("irvault")
AppController = irvault_ns.class_("AppController", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {cv.GenerateID(): cv.declare_id(AppController)}
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
