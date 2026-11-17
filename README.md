# Smart Home Recipes

Standalone ESPHome packages and Home Assistant blueprints for real devices.
Each one is self-contained: take the one you need, nothing else comes with it.

## ESPHome

| Package | For | What it adds |
|---|---|---|
| [`midea-uart`](esphome/midea-uart/) | Midea-protocol ACs over UART (SLWF-01Pro and similar) | LED button with real state, LED off at power-on, Turbo/Eco switches, Clean button |

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

## License

[MIT](LICENSE)
