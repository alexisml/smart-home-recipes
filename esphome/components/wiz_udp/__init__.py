"""wiz_udp: helpers to control WiZ bulbs over their local UDP API.

Adding `wiz_udp:` to a config makes the wiz_udp:: functions in wiz_udp.h
available to every lambda. There are no entities and nothing runs on its
own: your config decides when to send, poll and verify.
"""

import esphome.codegen as cg
import esphome.config_validation as cv

CODEOWNERS = ["@alexisml"]
DEPENDENCIES = ["wifi", "network"]
# JSON parsing of bulb replies; light helpers (target_from) need the light types.
AUTO_LOAD = ["json", "light"]

CONFIG_SCHEMA = cv.All(
    cv.Schema({}),
    # Uses Arduino's WiFiUdp; only tested on ESP8266.
    cv.only_on(["esp8266"]),
)


async def to_code(config):
    cg.add_global(cg.RawStatement('#include "esphome/components/wiz_udp/wiz_udp.h"'))
