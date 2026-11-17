# Diagnostics

An ESPHome package that adds the entities you wish you'd had the day a
device started misbehaving. Works on any ESP8266 or ESP32.

| Entity | What it tells you |
|---|---|
| **Last boot** | When the device last started. A timestamp, not a counting uptime, so its history in Home Assistant *is* the list of reboots, and it doesn't write to the database every minute. |
| **Reset reason** | Why it restarted: power on, watchdog, crash, software reset. Without it a reboot leaves no trace, because restored values make the device look like it never went away. |
| **ESPHome version** | Version and build time. HA's device page shows this too, but only an entity keeps history, so you can see when each firmware went on. |
| **WiFi signal** / **WiFi signal percent** | RSSI in dBm, and the same as 0–100 % for dashboards. |
| **Status** | Whether the device is connected to Home Assistant. |

All are diagnostic entities, so they stay off auto-generated dashboards.
There are no feature flags: if you don't want one, disable it in Home
Assistant.

## Install

Add to your device's existing config (see [`example.yaml`](example.yaml)):

```yaml
packages:
  diagnostics:
    url: https://github.com/alexisml/smart-home-recipes
    ref: main
    files: [esphome/diagnostics/diagnostics.yaml]
    refresh: 1d
```

**Needs a clock.** "Last boot" is a timestamp, so your config must have a
`time:` platform. Any will do: `- platform: homeassistant`, `sntp`, or the
[`resilience`](../resilience/) package, which brings both. Without one,
validation stops with "This option requires component time".

## Options

| Substitution | Default |
|---|---|
| `diag_last_boot_name` | `Last boot` |
| `diag_reset_reason_name` | `Reset reason` |
| `diag_version_name` | `ESPHome version` |
| `diag_wifi_signal_name` | `WiFi signal` |
| `diag_wifi_signal_percent_name` | `WiFi signal percent` |
| `diag_status_name` | `Status` |

Changing a name later creates a new entity in Home Assistant: its unique id
is built from the name.

## Notes

- **Conflicts.** If your config already has `debug:` that's fine, the two
  merge. If it already has an `uptime`, `wifi_signal`, `version` or `status`
  entity, you'll get a second one; remove yours.
- **Why it exists.** These came out of a water meter box that was
  rebooting every 15 minutes during Home Assistant outages (the default
  `api: reboot_timeout`). Nothing showed it: the daily counters restore from
  flash, so the device looked fine. "Last boot" and "Reset reason" made it
  visible at once. The fix for the reboots themselves is in
  [`resilience`](../resilience/).
