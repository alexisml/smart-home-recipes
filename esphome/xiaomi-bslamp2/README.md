# Xiaomi Bedside Lamp 2 extras

An ESPHome package for the **Xiaomi Mijia Bedside Lamp 2** (MJCTD02YL, also
sold as Yeelight bslamp2). It's a layer on top of
[mmakaay/esphome-xiaomi_bslamp2](https://github.com/mmakaay/esphome-xiaomi_bslamp2),
which does all the hardware work. This package pulls that in and adds an
on-device sunrise, notification pulses, scene effects and gentler defaults.

## Features

| Feature | Substitution | Default | Status |
|---|---|---|---|
| **Sunrise** button, with duration and max brightness settings | `bslamp2_sunrise` | on | In daily use |
| **Alert** (red) / **Warning** (amber) buttons: four pulses, then back to what the lamp was doing | `bslamp2_alerts` | on | In daily use |
| **Effects**: Candlelight, RGB fade, Movie night, Fireplace, Velvet | always on | | In daily use |
| **Defaults**: 1500 ms transitions, warm white at 50% on first boot, last state restored after a power cut | always on, values overridable | | In daily use |

Flags take unquoted `true` or `false` only. A quoted `"false"`, or anything
else, stops validation with an error naming the flag.

A feature set to `false` is hidden from Home Assistant and does nothing on
the device. Its code is still compiled in. Effects have no flag: they aren't
entities, so they can't be hidden, only renamed.

## Install

The lamp has to run ESPHome already; the first flash is over serial (see
[mmakaay's flashing guide](https://github.com/mmakaay/esphome-xiaomi_bslamp2/blob/dev/doc/flashing.md)).
Then copy [`example.yaml`](example.yaml), change the substitutions, flash:

```yaml
packages:
  bslamp2:
    url: https://github.com/alexisml/smart-home-recipes
    ref: main
    files: [esphome/xiaomi-bslamp2/xiaomi-bslamp2.yaml]
    refresh: 1d
```

The package brings mmakaay's `core`, `behavior_default`, `ota_feedback` and
`activate_preset_svc` packages (pinned to their `release/2026.10.0` branch),
plus `captive_portal`. Your own config provides `name`, `friendly_name`,
`wifi` and `ota`.

## Options

| Substitution | Default | What |
|---|---|---|
| `default_transition_length` | `1500ms` | Every toggle and dim. Upstream's default is 800 ms. |
| `bslamp2_restore_mode` | `RESTORE_DEFAULT_OFF` | After a power cut: last colour and on/off state. `RESTORE_AND_OFF` always comes back off. |
| `bslamp2_sunrise_duration_min` | `"4.5"` | Starting value, then changeable from HA (1–60 min) |
| `bslamp2_sunrise_max_brightness_pct` | `"40"` | Starting value, then changeable from HA (5–100 %) |
| `light_name`, `bslamp2_*_name` | English | Light, button, setting and effect names. See the top of [`xiaomi-bslamp2.yaml`](xiaomi-bslamp2.yaml) for the full list. |

mmakaay's own substitutions (night light colour, light mode sensor name)
work too. Changing an entity name later creates a new entity in Home
Assistant: its unique id is built from the name.

## Sunrise

Starts from off (it turns the lamp off first if needed) and ramps up to
the max brightness over the set duration, from deep red to warm white.
Everything runs on the lamp, so it doesn't depend on Home Assistant being up.

At the 4.5 min default:

| Stage | ~Time | What |
|---|---|---|
| Ember | 8 s | night light at 1%, instant, no transition |
| 1 | 49 s | → 1.5% white at 588 mireds: red turns orange here |
| 2 | 81 s | → 2.5%, still 588 mireds: the orange → warm stretch |
| 3 | 132 s | 20 steps to max, squared curve, 588 → 333 mireds (~1700 → 3000 K) |

Most of the time is spent in light you can barely see. That's deliberate.

**Interrupting:** turning the lamp off (front panel, HA, anything) stops the
sunrise, and the next turn-on restores what the lamp was showing before it
started. A long press on the panel (night light, from upstream's behaviour)
also stops it, leaving the lamp at 1%.

### Hardware constraints — don't "simplify" these away

Both were found by pushing values to the lamp one at a time and watching the
room. Changing the low end of the sunrise without redoing that is how it
breaks.

- **Below ~1.2% brightness (`< 0.012`) the lamp switches to night light
  mode.** It's the only way to get visible light below ~2%, which is why the
  sunrise opens with an instant night-light ember instead of a fade.
- **A long transition starting from off crawls under the LEDs' turn-on
  threshold and then pops red.** The RGB channels are inverted, so red has
  the biggest jump and lights first. That's why the bottom of the ramp is
  three separate stages, not one fade from zero.

## Notes

- Effects and pulses use `set_publish(false)` / `set_save(false)` and
  `disco_on`, so they don't flood Home Assistant with state updates or wear
  out the flash, and never overwrite the saved colour.
- `effect:` and `transition_length:` can't be used together on
  `light.turn_on` ("two or more values in the same group of exclusion").
- `Base MAC address from BLK0 of EFUSE CRC error` at boot is harmless on some
  batches of this lamp.
- `Bootloader too old for OTA rollback` needs one serial flash to clear. OTA
  works regardless.
- The `Slow Random` / `Fast Random` effects from upstream stay in the list
  (effects are appended, not replaced), though they appear to do nothing on
  this lamp.

## Known

- **A brief flash was seen at the start of some sunrise runs** and could not
  be reproduced. The runs where it showed up had a long transition cut off
  mid-flight by a test script, which a real sunrise never does. If you see
  it in normal use, please open an issue.
- **Tested on mmakaay's `dev` branch** (October 2026); this package pins
  `release/2026.10.0`, which differs by a few commits.

## Credits

- [mmakaay/esphome-xiaomi_bslamp2](https://github.com/mmakaay/esphome-xiaomi_bslamp2)
  by Maurice Makaay: the hardware component, board setup, default behaviour
  and the packages this one builds on. Follows the ESPHome licence (MIT for
  YAML/Python, GPLv3 for C++). This package only references it; none of its
  code is copied here.
- The sunrise, pulses, effects and defaults in this folder are original work
  on top of it.
