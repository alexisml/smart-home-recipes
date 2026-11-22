"""wiz_udp: control WiZ bulbs over their local UDP API.

  wiz_udp:
    bulbs: my_bulbs_text      # optional: text entity with "ip, ip, ..."
    light: my_light           # optional: adds the WiZ scenes as its effects
    scenes:                   # optional: which scenes, named how (order kept)
      1: Ocean
      4: Party

The component reads bulb answers in its loop. Everything else (when to send,
verify, fall back) is up to your config, through the wiz_udp:: helpers in
wiz_udp.h, which are available in every lambda.
"""

import esphome.codegen as cg
from esphome.components import light
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_LIGHT

CODEOWNERS = ["@alexisml"]
DEPENDENCIES = ["wifi", "network"]
AUTO_LOAD = ["json", "light"]

CONF_BULBS = "bulbs"
CONF_SCENES = "scenes"

wiz_udp_ns = cg.global_ns.namespace("wiz_udp")
WizUdp = wiz_udp_ns.class_("WizUdp", cg.Component)

# WiZ scenes 1-36 and 40 (pywizlight's list). Rhythm (1000), custom (256+)
# and sync (201-204) scenes need more than a sceneId and are left out.
DEFAULT_SCENES = {
    1: "Ocean",
    2: "Romance",
    3: "Sunset",
    4: "Party",
    5: "Fireplace",
    6: "Cozy",
    7: "Forest",
    8: "Pastel colors",
    9: "Wake-up",
    10: "Bedtime",
    11: "Warm white",
    12: "Daylight",
    13: "Cool white",
    14: "Night light",
    15: "Focus",
    16: "Relax",
    17: "True colors",
    18: "TV time",
    19: "Plantgrowth",
    20: "Spring",
    21: "Summer",
    22: "Fall",
    23: "Deep dive",
    24: "Jungle",
    25: "Mojito",
    26: "Club",
    27: "Christmas",
    28: "Halloween",
    29: "Candlelight",
    30: "Golden white",
    31: "Pulse",
    32: "Steampunk",
    33: "Diwali",
    34: "White",
    35: "Alarm",
    36: "Snowy sky",
    40: "Dim-to-warm",
}


def _scenes(value):
    value = cv.Schema({cv.int_range(min=1, max=999): cv.string_strict})(value)
    if not value:
        raise cv.Invalid("scenes needs at least one 'sceneId: name' entry")
    names = list(value.values())
    if len(set(names)) != len(names):
        raise cv.Invalid("scene names must be unique")
    return value


def _text_id(value):
    # Imported lazily so configs without a text entity don't need it.
    from esphome.components import text

    return cv.use_id(text.Text)(value)


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(WizUdp),
            cv.Optional(CONF_BULBS): _text_id,
            cv.Optional(CONF_LIGHT): cv.use_id(light.LightState),
            cv.Optional(CONF_SCENES): _scenes,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    # Uses Arduino's WiFiUdp; only tested on ESP8266.
    cv.only_on(["esp8266"]),
)


async def to_code(config):
    cg.add_global(cg.RawStatement('#include "esphome/components/wiz_udp/wiz_udp.h"'))
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    if CONF_BULBS in config:
        bulbs = await cg.get_variable(config[CONF_BULBS])
        cg.add(var.set_bulbs_text(bulbs))

    if CONF_LIGHT in config:
        light_var = await cg.get_variable(config[CONF_LIGHT])
        for scene, name in config.get(CONF_SCENES, DEFAULT_SCENES).items():
            cg.add(var.add_scene(scene, name))
        cg.add(var.attach_scenes(light_var))
