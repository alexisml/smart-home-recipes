# Smart Home Recipes

Standalone ESPHome packages and Home Assistant blueprints for real devices.
Each one is self-contained: take the one you need, nothing else comes with it.

## ESPHome

| Package | For | What it adds |
|---|---|---|
| [`midea-uart`](esphome/midea-uart/) | Midea-protocol ACs over UART (SLWF-01Pro and similar) | LED button with real state, LED off at power-on, Turbo/Eco switches, Clean button |
| [`xiaomi-bslamp2`](esphome/xiaomi-bslamp2/) | Xiaomi Mijia Bedside Lamp 2 (MJCTD02YL) | On-device sunrise, alert/warning pulses, five scene effects, gentler defaults |

Each package lives in `esphome/<device>/` with the package itself, an
`example.yaml` and a README. Include it from your own config:

```yaml
packages:
  midea:
    url: https://github.com/alexisml/smart-home-recipes
    ref: main
    files: [esphome/midea-uart/midea-uart.yaml]
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
- [mmakaay/esphome-xiaomi_bslamp2](https://github.com/mmakaay/esphome-xiaomi_bslamp2):
  the Bedside Lamp 2 component and packages `xiaomi-bslamp2` builds on

## License

[MIT](LICENSE), except where a folder has its own `LICENSE`:

- [`esphome/midea-uart`](esphome/midea-uart/) is **GPL-3.0**, because it
  derives from SMLIGHT's GPL-3.0 config.
