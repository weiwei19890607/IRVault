import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["esp32"]

CONF_TEST_MODE = "test_mode"
CONF_BUTTON_PULL = "button_pull"

sticks3_ns = cg.esphome_ns.namespace("sticks3_hardware")
StickS3Hardware = sticks3_ns.class_("StickS3Hardware", cg.Component)

TestMode = sticks3_ns.enum("TestMode")
TEST_MODES = {
    "A": TestMode.TEST_A,
    "B": TestMode.TEST_B,
    "C": TestMode.TEST_C,
    "D": TestMode.TEST_D,
}

ButtonPull = sticks3_ns.enum("ButtonPull")
BUTTON_PULLS = {
    "NONE": ButtonPull.PULL_NONE,
    "UP": ButtonPull.PULL_UP,
    "DOWN": ButtonPull.PULL_DOWN,
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(StickS3Hardware),
        cv.Required(CONF_TEST_MODE): cv.enum(TEST_MODES, upper=True),
        cv.Optional(CONF_BUTTON_PULL, default="NONE"): cv.enum(
            BUTTON_PULLS, upper=True
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_test_mode(config[CONF_TEST_MODE]))
    cg.add(var.set_button_pull(config[CONF_BUTTON_PULL]))
