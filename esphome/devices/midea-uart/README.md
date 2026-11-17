# Midea UART extras

An ESPHome package for air conditioners that speak the Midea UART protocol.
It wraps ESPHome's [`midea`](https://esphome.io/components/climate/midea/)
climate component and adds what the IR remote has and the component doesn't.

Works with any ESP8266/ESP32 wired to the AC's UART port. Tested on the
**SMLIGHT SLWF-01Pro v1.x** (ESP-12E). The v2.1 (the one with an extra
USB-C port) uses different UART pins; see [Options](#options). Brands using the protocol include Midea,
Comfee, Carrier, Inventor, Electrolux and Qlima.

## Features

| Feature | Substitution | Default | Status |
|---|---|---|---|
| **LED** switch: the remote's display button, with real state read back from the AC | `midea_led` | on | Tested |
| **LED on power-on** setting: keep the display off every time the AC turns on | `midea_led_on_poweron` | on | Tested |
| **Turbo** / **Eco** switches: the boost and eco presets as plain switches | `midea_preset_switches` | off | Experimental |
| **Clean** button: Midea "active clean" | `midea_clean` | off | Experimental, never run on a unit |

Flags take unquoted `true` or `false` only. A quoted `"false"`, or anything
else, stops validation with an error naming the flag, so a typo can never
quietly turn a feature on.

A feature set to `false` is hidden from Home Assistant and does nothing on
the device. Its code is still compiled in: ESPHome can't leave a component
out based on a substitution.

**Experimental** means it hasn't been confirmed on real hardware. If you try
one, please open an issue saying whether it worked and on which unit.

## Install

Copy [`example.yaml`](example.yaml), change the substitutions, flash. The
package is pulled straight from this repo:

```yaml
packages:
  midea:
    url: https://github.com/alexisml/smart-home-recipes
    ref: main
    files: [esphome/devices/midea-uart/midea-uart.yaml]
    refresh: 1d
```

Your own config provides the board, `wifi`, `api` and `ota`. The package
brings the UART, the climate entity, and sets `logger: baud_rate: 0` because
the AC uses the only hardware UART.

Set `restore_from_flash: true` under `esp8266:`. Without it, the "LED on
power-on" setting is kept in RTC memory, and a power cut (the dongle is
powered by the AC) resets it to the default.

## Options

All are substitutions; set them in your own config to override.

| Substitution | Default | What |
|---|---|---|
| `midea_led_on_poweron_default` | `RESTORE_DEFAULT_ON` | Starting value of the setting. `RESTORE_DEFAULT_OFF` turns the LED off 2 s after every power-on. Changeable from HA afterwards. |
| `midea_beeper` | `"true"` | AC beeps when it accepts a command |
| `midea_tx_pin` / `midea_rx_pin` | `"1"` / `"3"` | UART pins. SLWF-01Pro v1.1 / v1.2: `1` / `3`. **SLWF-01Pro v2.1: `12` / `14`** (per [SMLIGHT's config](https://github.com/smlight-tech/slwf-01pro-esphome)). |
| `midea_climate_name`, `midea_led_name`, `midea_led_on_poweron_name`, `midea_turbo_name`, `midea_eco_name`, `midea_clean_name` | English | Entity names, e.g. for a non-English Home Assistant |

Changing an entity name later creates a new entity in Home Assistant: its
unique id is built from the name.

To change the climate entity itself (temperature range, swing modes, extra
sensors), extend it:

```yaml
climate:
  - id: !extend midea_climate
    supported_swing_modes: [VERTICAL, HORIZONTAL, BOTH]
    outdoor_temperature:
      name: Outdoor temperature
```

## How it works

**LED.** `midea_ac.display_toggle` only goes over UART when the AC reports
the LIGHT CONTROL capability. Many units don't, and then it falls back to IR
and sends nothing. This package writes the raw 0x41 toggle frame
(MideaUART's `DisplayToggleData`) to the UART instead. The AC only has a
toggle, so the state comes from sniffing its status replies (0xC0, body
byte 14, `0x70` = off) with `uart: debug:`, and the switch toggles only when
the state read back differs from the one asked for.

**LED on power-on.** The AC lights its display at every power-on, from HA or
the remote. When this setting is off, the device waits 2 s for a fresh
status frame and toggles the LED off if it's lit. It also runs at boot if
the AC is already on, so it relights correctly after a power cut. Turning the
LED on by hand afterwards sticks until the next power-on.

**Clean.** Writes a set-properties (0xB0) frame for property `0x0039`. The
frame's CRC8 and checksum code was checked against frames captured from a
real unit, but the button itself has never been pressed on one. Your AC must
list ACTIVE CLEAN in its capabilities: watch `esphome logs` at boot, where
autoconf prints them.

## Known

- **The LED switch shows `unknown` until the first status frame**, a second
  or so after boot.
- **With the AC off, the LED switch may show on.** Some units report the
  display as lit while off, even though the panel is dark.
- **Template switches act at boot unless `restore_mode: DISABLED`.** Their
  default runs `turn_off` at every boot, which here would send a blind LED
  toggle or cancel an active preset. All switches in this package use
  `DISABLED`. Keep that if you copy them.
- **Eco is cool-only** on Midea. In other modes the AC refuses it and the
  switch falls back to off.

## Credits

This package is built on other people's work:

- [smlight-tech/slwf-01pro-esphome](https://github.com/smlight-tech/slwf-01pro-esphome)
  (GPL-3.0): SMLIGHT's stock ESPHome config for the SLWF-01Pro. The base
  config (UART, logger, the `midea` climate block) started from it, and it
  documents the v1.x and v2.1 pin differences.
- ESPHome's [`midea` climate component](https://esphome.io/components/climate/midea/),
  by Sergey Dudanov, built on his
  [MideaUART](https://github.com/dudanov/MideaUART) library. The LED toggle
  frame is the one MideaUART's `DisplayToggleData` builds.
- [mill1000/midea-msmart](https://github.com/mill1000/midea-msmart)
  (msmart-ng, MIT): the decoding of the display state from status frames
  (body byte 14, `0x70` = off), and the active clean property (`0x0039`).

Original here: reading the LED state over UART, the LED switch and the
"LED on power-on" setting, the Turbo/Eco switches, and the raw Clean frame.

## License

**GPL-3.0** ([LICENSE](LICENSE)), because this package derives from SMLIGHT's
GPL-3.0 config. This applies to this folder only; the rest of the repository
is MIT.
