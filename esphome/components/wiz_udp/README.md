# wiz_udp

An ESPHome external component for controlling **WiZ bulbs directly over their
local UDP API**, with no Home Assistant and no cloud. It's a small C++
helper library: adding it makes `wiz_udp::` functions available in every
lambda, and your own config decides when to send, poll and verify.

The use case it was built for: one ESPHome relay powers a group of WiZ
bulbs. A wall-switch click or a light entity on the relay device should
switch the bulbs **in software** (soft off), keeping them powered and
reachable, and cut the relay only as a fallback.

> **Experimental, but running.** Tested on one ESP8266 (a Shelly 2.5)
> driving six WiZ bulbs of two generations: on/off, brightness, white,
> colour, scenes, sync of changes made from outside, and the ARP workaround.
> Commands confirm in about 0.7 s, or about 10 s from power-up. Other
> setups are untested.

## Install

```yaml
external_components:
  - source: github://alexisml/smart-home-recipes@main
    components: [wiz_udp]
    refresh: 1d

wiz_udp:
  bulbs: my_bulbs        # optional: text entity holding "ip, ip, ..."
  light: my_light        # optional: adds the WiZ scenes as this light's effects
  scenes:                # optional: which scenes and their names (order kept)
    1: Ocean
    4: Party
```

- **`bulbs`**: the bulb list comes from this text entity and follows it at
  runtime. Without it, call `wiz_udp::set_ips(...)` yourself.
- **`light`**: the WiZ scenes are added as effects on that light. They're
  inert placeholders: picking one changes the light's state, and
  `target_from(light)` turns the effect name into the scene. Default: scenes
  1–36 and 40 with English names. Use **`scenes`** to rename them (any
  language) or pick a subset.
- The component reads bulb answers continuously. Sending, verifying and
  falling back stay in your config.

ESP8266 (Arduino) only, as that's what it was tested on.

## How it talks to the bulbs

- JSON over UDP: commands to each bulb's port 38899, answers come back to
  port 38900 on the ESP.
- Bulbs are a runtime list of addresses (`set_ips("a.b.c.d, ...")`), up to
  16. Feed it from a `text:` entity so a moved or replaced bulb can be fixed
  without a rebuild.
- **Commands at once, status one by one.** Action commands (on, off,
  colour, scene) go to every bulb in one fast sweep, 4 ms apart (about 25 ms
  for six bulbs). That's the fastest the ESP8266 manages: packets sent back to
  back are dropped silently, and in testing only the first of six went out.
  Status questions (`getPilot`) are queued and sent one every 60 ms from the
  component's loop, so answers arrive one by one. The core buffers only 4
  unread packets.
- **Verify and retry.** UDP drops packets, and weak-signal bulbs miss
  commands often; in testing one bulb needed six tries. After a command,
  verification rounds run every ~250 ms. A bulb answering in the wrong state
  gets the command again at once. A silent bulb (booting, or receiving
  commands while its answers are lost) gets it about once a second, each
  time after a fresh unicast ARP request. That's enough for a lost command,
  without flooding the ESP8266's small transmit queue or restarting a scene
  four times a second. Every unconfirmed bulb is also queued to be asked
  again.
- A bulb counts as confirmed only when it reports the exact target: on/off,
  brightness (±1 %), white temperature (±50 K) or colour (±3), or the scene.

**Bulbs that don't answer this device (ARP).** Before the first packet to
a bulb, the ESP has to learn its MAC with a broadcast ARP request. Some
older, power-saving WiZ bulbs never answer that from a device on their own
subnet. Every packet to them is then dropped before it leaves, even though
the bulb works fine from Home Assistant or a phone reaching it through a
router. `status_text()` marks such bulbs with `?`. Fix: list them as
`ip@mac` (`192.168.1.50@a8bb50aabbcc`; colons allowed). The component then
feeds lwIP that bulb's ARP answer itself before sending, so commands go
out. For the answers to come back, the bulb must know the ESP's MAC too,
and its own broadcast question for it doesn't get through either. So the component sends the bulb a real ARP request, unicast to its MAC:
every 3 s while the bulb isn't answering (e.g. still booting after its power
came on), every 15 s once it is, and before every command retry.
A bulb learns the asker's address from any request addressed to it (an
unsolicited ARP reply was ignored). `status_text()` counts these as
`arp sent/refused`. A bulb's MAC is in
its `getPilot` answer, its label, or your router.

Works with bulbs that accept unsigned `setPilot`. Newer firmware that
requires signed commands (`udp_signing_key`) is **not supported**.

## API

All in namespace `wiz_udp`, callable from any lambda.

| Function | What it does |
|---|---|
| `begin_command(target, window_ms)` | Send a target to every bulb at once and start verifying it. |
| `command_step()` | One verification round: resends to bulbs in the wrong state and asks unconfirmed ones again. Returns true while the command is still pending; call it every ~250 ms, e.g. as a `while:` condition. |
| `command_confirmed()`, `command_unconfirmed()`, `command_age()` | Outcome of the current command: confirmed by every bulb, addresses still missing, ms since it was sent. |
| `target_off()`, `target_on(brightness, color, r, g, b, mireds)` | Build a target (ESPHome light units: 0..1 and mireds). |
| `target_from(light)` | Target from a light's current state, including its active scene effect. |
| `sync_light(light, relay_on)` | Mirror the bulbs into a light (quietly), e.g. after changes from the WiZ app. Returns true if it changed the light. |
| `perform_quietly(call)`, `quiet()` | Run a LightCall marked as internal. Gate your on_state logic with `if (!wiz_udp::quiet())` so internal updates don't loop back as commands. |
| `get_pilot_all()`, `ask(i)` | Queue a status question for every bulb / bulb *i*. Sent one at a time by the component's loop. |
| `online_count(max_age_ms)`, `any_fresh(since)`, `all_fresh(since)` | How many bulbs answered recently; whether some/all answered since a time (e.g. after power-up). |
| `group(max_age_ms)` | Combined view: on if any bulb is on, plus the first lit bulb's brightness, colour or scene. |
| `status_text()` | One-line diagnostics for a template text sensor: packets sent/failed, answers received/matched/from unknown addresses/unparseable, and seconds since each bulb last answered. |
| `take_snapshot()`, `restore_round(since)` | Save every bulb's state and put it back with verification, e.g. around a measurement. |
| `arp_known(ip)`, `arp_prime(bulb)`, `arp_announce(bulb)` | Whether the ESP has resolved a bulb's MAC; feed lwIP the ARP answer of a bulb listed as `ip@mac`; send that bulb a unicast ARP request so it learns the ESP's MAC. |
| `set_ips(str)`, `send_all(msg)`, `pilot(target)`, `verify_round(since, target)`, `all_confirmed(since, target)` | Lower-level building blocks. |

### Why `perform_quietly`

ESPHome fires a light's `on_state` synchronously inside `perform()`. If your
`on_state` runs a `mode: restart` script, and that script changes the light
again (or sync does), the nested call restarts the script and **kills the run
that was in progress**. Marking internal changes as quiet, and skipping them
in `on_state`, avoids that entirely.

## Scenes

WiZ scenes 1–36 and 40 by default (Ocean, Romance, Sunset, Party, Fireplace,
Cozy, Forest, Pastel colors, Wake-up, Bedtime, Warm white, Daylight, Cool
white, Night light, Focus, Relax, True colors, TV time, Plantgrowth, Spring,
Summer, Fall, Deep dive, Jungle, Mojito, Club, Christmas, Halloween,
Candlelight, Golden white, Pulse, Steampunk, Diwali, White, Alarm, Snowy sky,
Dim-to-warm). Rhythm (1000), custom (256+) and sync (201–204) scenes are left
out.

Scene 11 ("Warm white") is also what a bulb reports in plain warm white, for
example right after power-up. It's treated as white, not as a running scene.
Picking it still sends the scene.

A scene counts as confirmed when the bulb reports that scene at the sent
brightness (±2). Wake-up, Bedtime, Pulse, Alarm and Dim-to-warm change their
own brightness, so for them the scene id alone is enough. Resending a scene
restarts its animation, so a check that never matches would show as
flicker. Colours are compared as proportions, so bulbs that scale or round
their channels still match.

To leave a scene, set a colour or white temperature, or choose no effect.
ESPHome keeps an effect running when a colour is set during it, so your
`on_state` logic has to end it (see the example in this README's API notes).
Setting exactly the colour the light already had before the scene changes
nothing, so it can't be told apart from no change.

## Example

[`example.yaml`](example.yaml) is a minimal relay device. The bulb list is a
`text:` entity wired through `bulbs:`. It counts the bulbs online and has
"all off" / "all on" buttons that use `begin_command` / `command_step`,
logging any bulb that didn't confirm.

## Credits

- Protocol details (ports, `getPilot`/`setPilot`, scene ids) from
  [sbidy/pywizlight](https://github.com/sbidy/pywizlight) (MIT), the library
  behind Home Assistant's WiZ integration. No code copied.
- [ESPHome](https://esphome.io) for the light, JSON and component framework.
