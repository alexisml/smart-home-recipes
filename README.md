# Smart Home Recipes

Standalone ESPHome packages and Home Assistant blueprints for real devices.
Each one is self-contained: take the one you need, nothing else comes with it.

## ESPHome

### Packages

| Package | For | What it adds |
|---|---|---|
| [`midea-uart`](esphome/devices/midea-uart/) | Midea-protocol ACs over UART (SLWF-01Pro and similar) | LED button with real state, LED off at power-on, Turbo/Eco switches, Clean button |
| [`xiaomi-bslamp2`](esphome/devices/xiaomi-bslamp2/) | Xiaomi Mijia Bedside Lamp 2 (MJCTD02YL) | On-device sunrise, alert/warning pulses, five scene effects, gentler defaults |
| [`resilience`](esphome/common/resilience/) | Any ESP8266/ESP32 device that must keep working without HA, Wi-Fi or power | Two clock sources, sane reboot timeouts, fallback hotspot, "clock synced" and "minutes without HA" signals. The README is a guide built around a pool pump controller. |
| [`diagnostics`](esphome/common/diagnostics/) | Any ESP8266/ESP32 device | Last boot, reset reason, ESPHome version, Wi-Fi signal, status |

Each package lives in `esphome/devices/<device>/` (needs that hardware) or
`esphome/common/<name>/` (goes on any device), with the package itself, an
`example.yaml` and a README. Include it from your own config:

```yaml
packages:
  midea:
    url: https://github.com/alexisml/smart-home-recipes
    ref: main
    files: [esphome/devices/midea-uart/midea-uart.yaml]
    refresh: 1d
```

### Components

C++ helpers loaded with `external_components`, for things YAML alone can't do.

| Component | For | What it adds |
|---|---|---|
| [`wiz_udp`](esphome/components/wiz_udp/) | ESP8266 devices that switch WiZ bulbs | Local UDP control of WiZ bulbs from lambdas: send to all at once, verify and retry, scenes, state polling. [Example](esphome/components/wiz_udp/example-light.yaml): the bulbs behind a relay as one light, with soft off and relay fallback. **Experimental.** |

```yaml
external_components:
  - source: github://alexisml/smart-home-recipes@main
    components: [wiz_udp]
    refresh: 1d
```

## Conventions

- **Features are switched with substitutions**, unquoted `true` / `false`.
  Anything else fails validation. A feature set to `false` is hidden from
  Home Assistant and does nothing on the device.
- **Entity names are English by default**, and each one can be renamed with a
  substitution.
- **Experimental** features are off by default and marked as such: they
  haven't been confirmed on real hardware yet. Reports welcome.
- **Ids are prefixed** with the package name (`midea_...`), so packages don't
  clash with each other or with your own config.

## Versioning

There are no releases yet: `main` is the only version. ESPHome caches remote
packages, so `refresh: 1d` picks up fixes once a day.

## Credits

These recipes are edits and additions on top of other people's work. Each
package's README has a Credits section saying exactly what came from where.

- [ESPHome](https://esphome.io) and [Home Assistant](https://www.home-assistant.io)
- [smlight-tech/slwf-01pro-esphome](https://github.com/smlight-tech/slwf-01pro-esphome):
  SMLIGHT's stock SLWF-01Pro config, the base of `midea-uart`
- [dudanov/MideaUART](https://github.com/dudanov/MideaUART) and
  [mill1000/midea-msmart](https://github.com/mill1000/midea-msmart): Midea
  protocol details used by `midea-uart`
- [sbidy/pywizlight](https://github.com/sbidy/pywizlight): WiZ local
  protocol details used by `wiz_udp`
- [mmakaay/esphome-xiaomi_bslamp2](https://github.com/mmakaay/esphome-xiaomi_bslamp2):
  the Bedside Lamp 2 component and packages `xiaomi-bslamp2` builds on

## License

[MIT](LICENSE), except where a folder has its own `LICENSE`:

- [`esphome/devices/midea-uart`](esphome/devices/midea-uart/) is **GPL-3.0**, because it
  derives from SMLIGHT's GPL-3.0 config.
