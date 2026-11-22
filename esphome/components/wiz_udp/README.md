# wiz_udp

An ESPHome external component for controlling **WiZ bulbs directly over their
local UDP API**, with no Home Assistant and no cloud. It's a small C++
helper library: adding it makes `wiz_udp::` functions available in every
lambda, and your own config decides when to send, poll and verify.

The use case it was built for: one ESPHome relay powers a group of WiZ
bulbs. A wall-switch click or a light entity on the relay device should
switch the bulbs **in software** (soft off), keeping them powered and
reachable, and cut the relay only as a fallback.

> **Experimental.** It compiles, and the protocol behaviour described here
> (unsigned commands, missed packets, power-up state) was tested against
> real bulbs from a computer. The component itself hasn't run on an ESP yet.

## Install

```yaml
external_components:
  - source: github://alexisml/smart-home-recipes@main
    components: [wiz_udp]
    refresh: 1d

wiz_udp:
```

ESP8266 (Arduino) only, as that's what it was tested on.

## How it talks to the bulbs

- JSON over UDP: commands to each bulb's port 38899, answers come back to
  port 38900 on the ESP.
- Bulbs are a runtime list of addresses (`set_ips("a.b.c.d, ...")`), up to
  16. Feed it from a `text:` entity so a moved or replaced bulb can be fixed
  without a rebuild.
- **Send to all at once, then verify.** UDP drops packets, and weak-signal
  bulbs miss commands often. In testing, one bulb needed six tries. So every
  command goes to all bulbs back to back, and then *verification rounds*
  (every ~250 ms) ask the unconfirmed bulbs again and resend the command to
  any bulb that answers in the wrong state.
- A bulb counts as confirmed only when it reports the exact target: on/off,
  brightness (±1 %), white temperature (±50 K) or colour (±3), or the scene.

Works with bulbs that accept unsigned `setPilot`. Newer firmware that
requires signed commands (`udp_signing_key`) is **not supported**.

## API

| Function | What it does |
|---|---|
| `set_ips(str)` | Set the bulb list from `"ip, ip, ..."`. Keeps the last known state of addresses that stay. |
| `poll()` | Read the bulbs' answers. Call often, e.g. a 30 ms `interval:`. |
| `get_pilot_all()` | Ask every bulb for its state, all at once. |
| `send_all(msg)` | Send a raw JSON command to every bulb, all at once. |
| `target_off()`, `target_on(brightness, color, r, g, b, mireds)` | Build a target state (values as ESPHome lights use them, 0..1 and mireds). |
| `target_from(light_state)` | Target from a light entity's current state; its active effect maps to a WiZ scene (see Scenes). |
| `pilot(target)` | The `setPilot` JSON for a target. |
| `verify_round(since, target)` | One verification round for a command sent at `since` (millis). |
| `all_confirmed(since, target)` | Every bulb reported the target since `since`. |
| `unconfirmed_list(since, target)` | Addresses still not confirmed, for logs. |
| `any_fresh(since)`, `all_fresh(since)` | Some / every bulb answered since `since`, e.g. after power-up. |
| `online_count(max_age_ms)` | How many bulbs answered recently. |
| `group(max_age_ms)` | Combined view: on if any bulb is on, attributes of the first lit one, and its scene. |
| `take_snapshot()`, `restore_round(since)` | Save every bulb's state and restore it with verification, e.g. around a measurement. |

## Scenes

`wiz_udp::SCENES` lists WiZ scenes 1–36 and 40 (Ocean, Romance, Sunset, Party,
Fireplace, Cozy, Forest, Pastel colors, Wake-up, Bedtime, Warm white,
Daylight, Cool white, Night light, Focus, Relax, True colors, TV time,
Plantgrowth, Spring, Summer, Fall, Deep dive, Jungle, Mojito, Club,
Christmas, Halloween, Candlelight, Golden white, Pulse, Steampunk, Diwali,
White, Alarm, Snowy sky, Dim-to-warm). Declare your light's `effects:` in
**that order**, named in any language. Effect *n* maps to `SCENES[n-1]`,
through `scene_for_effect()` and `effect_for_scene()`.

Note: bulbs powering up in plain warm white report scene 11 together with
their temperature.

## Example

[`example.yaml`](example.yaml) is a minimal relay device. It keeps the bulb
list in a `text:` entity, counts the bulbs online, and has "all off" / "all
on" buttons that send to every bulb and then verify for 3 s, logging any
bulb that didn't confirm.

## Credits

- Protocol details (ports, `getPilot`/`setPilot`, scene ids) from
  [sbidy/pywizlight](https://github.com/sbidy/pywizlight) (MIT), the library
  behind Home Assistant's WiZ integration. No code copied.
- [ESPHome](https://esphome.io) for the light, JSON and component framework.
