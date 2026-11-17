# Resilience

An ESPHome package, plus a set of practices, for devices that must keep
doing their job when Home Assistant, Wi-Fi, the internet or the power goes
away. Works on ESP8266 and ESP32.

Most ESPHome devices are fine being dumb while HA is down. This is for the
ones that aren't: anything that acts on a schedule or protects something
(a pump, a heater, a valve).

## What the package does

| Piece | Why |
|---|---|
| **Two clock sources**: NTP and Home Assistant | Either one keeps the clock right. HA down → NTP. Internet down → HA. Both down → the chip keeps time from the last sync (drift: seconds per day). |
| **NTP by IP** as one of the servers | Time still syncs when DNS is broken. |
| **`api: reboot_timeout: 2h`** (default 15min) | ESPHome reboots a device after 15 minutes without HA. During an HA update or host reboot that's a reboot every 15 minutes, which fixes nothing and wipes anything kept in RAM. |
| **Fallback hotspot + captive portal** | If the device can't join your Wi-Fi it opens its own network, named after the device, so you can still reach it. |
| **`resilience_ha_down_minutes`** global | Consecutive minutes without HA. Your own logic can act on it (see [Taking over from HA](#taking-over-from-ha)). |
| **Clock synced** binary sensor | Whether the device knows the real time. Anything that acts at a time of day should check it. |
| **Device time** text sensor | The device's own clock, to spot a wrong timezone or a clock that never synced. |

## Install

Add to your device's existing config (see [`example.yaml`](example.yaml)):

```yaml
substitutions:
  resilience_timezone: America/Montevideo            # required
  resilience_ap_password: !secret wifi_password # required

packages:
  resilience:
    url: https://github.com/alexisml/smart-home-recipes
    ref: main
    files: [esphome/common/resilience/resilience.yaml]
    refresh: 1d
```

Both substitutions are required; without them validation stops and names
the missing one. Remove from your config anything the package now covers:
`captive_portal:`, `wifi: ap:`, `api: reboot_timeout` and any `time:`
platforms.

| Substitution | Default | What |
|---|---|---|
| `resilience_timezone` | *required* | IANA name, e.g. `America/Montevideo` |
| `resilience_ap_password` | *required* | Fallback hotspot password |
| `resilience_api_reboot_timeout` | `2h` | Reboot after this long without HA. `0s` disables. |
| `resilience_wifi_reboot_timeout` | `15min` | Reboot after this long without Wi-Fi (ESPHome's default) |
| `resilience_ntp_server_1` … `_3` | `pool.ntp.org`, `time.cloudflare.com`, `162.159.200.1` | Three is the maximum. Keep one as a raw IP. |
| `resilience_clock_synced_name`, `resilience_device_time_name` | English | Entity names |

**Picking the reboot timeout.** Longer than your longest HA outage, short
enough to recover a wedged network stack. `2h` outlasted every outage seen
on the device it came from (longest: 92 min). A device whose RAM state
matters a lot (counters, a fallback mode) can go to `24h`.

## The use case: a pool pump

This all came out of a pool filtration controller: a Sonoff 4CH Pro R3 that
runs the pump for a set number of hours a day, in time windows editable from
Home Assistant. The schedule used to be an HA automation. Moving it onto the
device made the pool independent of HA, and then every failure mode had to
be thought through, because **an unfiltered pool goes green in days**.

The failure table it ended up with:

| Failure | Schedule still runs? | Why |
|---|---|---|
| HA down, Wi-Fi up | Yes | NTP keeps the clock; after 10 min the device also takes the schedule back from HA |
| Wi-Fi down, power on | Yes | The clock keeps running from the last sync |
| Power cut, Wi-Fi back on return | Yes | NTP or HA syncs within seconds; the next tick starts the pump if it's mid-window |
| **Power cut and no Wi-Fi on return** | **Degraded** | No battery clock, so no time at all. Falls back to running blind (below). |

The package covers the clock and reboot rows. The rest are practices, below,
with the pool's code as the example. The pool runs its own full config,
not this package; the snippets are adapted to the package's ids.

### Principle: fail toward the cheap mistake

Every ambiguous case should resolve toward the mistake that costs less.
Running a pump too long costs some kWh; not running it costs the pool. So:

- A manual "off" is **not** remembered across a reboot. After a restart the
  schedule takes over again. Remembering it would be memory pointed in the
  dangerous direction.
- When the device doesn't know the time, it still runs the pump.
- When HA has taken control and then disappears, the device takes control
  back.

Your device's cheap mistake may be the opposite (a heater: better off than
on). Decide which one it is first; the rest follows.

### Settings that survive a power cut

On ESP8266, `restore_value: true` keeps values in RTC memory by default,
which a power cut wipes. Schedule times, targets and switches set from HA
would all reset. Store them in flash instead:

```yaml
esp8266:
  restore_from_flash: true
```

Not in the package because it's ESP8266-only (ESP32 always uses flash).

**Keep flash writes rare.** Writing a counter every minute wears the flash
out. The pool keeps its runtime counter in RAM and saves a copy every 10
minutes of pump runtime: about 39 writes a day while running, none while
idle, and a reboot loses at most 9 minutes.

### A web UI that works without HA

```yaml
web_server:
  port: 80
  version: 3
```

With HA down, the device's own web page (or its fallback hotspot) is the
only way to change anything. Version 2 can't show `datetime` or `number`
entities, so the schedule couldn't be edited. Version 3 can, but costs RAM:
on an ESP8285 drop it first if the device boot-loops. Not in the package
for that reason.

### Physical buttons that need nothing

```yaml
binary_sensor:
  - platform: gpio
    pin: {number: GPIO14, mode: {input: true, pullup: true}, inverted: true}
    name: Pump button
    filters: [delayed_on: 50ms]
    on_press:
      switch.toggle: pump
```

Wired straight to the relay: no API, no network, no clock. Manual control
works with everything else dead.

### Taking over from HA

HA is allowed to pause the on-device schedule (a switch) and run the pump
from its own automations. The risk: HA dies while holding the schedule
paused. After 10 minutes without HA the device takes it back:

```yaml
interval:
  - interval: 1min
    then:
      - lambda: |-
          if (id(resilience_ha_down_minutes) == 10 && !id(schedule_enabled).state) {
            ESP_LOGW("schedule", "Home Assistant gone 10 min - taking the schedule back");
            id(schedule_enabled).turn_on();
          }
```

`== 10` fires once per outage, not every minute after. The counter resets
when HA reconnects, ready for the next outage. Caveat: if someone pauses the
schedule from the device's web page in the first 10 minutes of an outage,
it's switched back on at the 10-minute mark.

### Running blind: no clock after a power cut

The one failure the package can't fix: power comes back, the network
doesn't, and the chip has no idea what time it is, so the time windows never
come. The pool then runs **blind**: after 10 minutes without a clock (enough
for any normal Wi-Fi + NTP boot), it runs the pump on a fixed cycle that
delivers the same hours per day, at the wrong times. Wrong hours, right
total.

```yaml
interval:
  - interval: 1min
    then:
      - lambda: |-
          static int blind_minutes = 0;
          if (id(resilience_time).now().is_valid()) {
            blind_minutes = 0;
            // clock known: run the real schedule here
            return;
          }
          blind_minutes += 1;
          if (blind_minutes < 10) return;          // grace for a normal boot
          const int target = 390;                  // minutes per day
          const int phase = (blind_minutes - 10) % 480;   // three 8 h cycles
          if (phase < target / 3) id(pump).turn_on();
          else id(pump).turn_off();
```

It leaves blind mode by itself the moment any clock syncs. Expose a sensor
for it (the pool has a "Running blind" diagnostic), so you can see
afterwards that it happened.

On the pool this has never actually triggered: it's compiled and reasoned
through, not observed.

**The real fix is a battery clock.** A DS3231 module keeps true time across
power cuts with no network. On the 4CH Pro R3, GPIO1/GPIO3 (TX/RX) are free
on the programming header:

```yaml
i2c:
  sda: GPIO1
  scl: GPIO3

time:
  - platform: ds1307   # DS3231 is register-compatible for timekeeping
    id: rtc_time

logger:
  baud_rate: 0         # TX/RX are now I2C
```

With it as a third clock source, NTP and HA just correct it when reachable.

### Counting minutes with ticks

The pool counts time in 1-minute interval ticks, and so does the package's
`resilience_ha_down_minutes`. Everything comparing those counters to a
number of minutes assumes `1 tick == 1 minute`. Change an interval to 5
minutes and every threshold is silently five times longer. If you need a
different cadence, compute from `millis()` instead.

## Build notes

- `api.connected` is a YAML condition only. It doesn't compile inside a
  lambda (`expected primary-expression before '.' token`). Use it in an
  `if:` block, as the package does, or read `resilience_ha_down_minutes`.
- On ESP8266, avoid `isnan()` / `std::isnan()` in lambdas: `isnan` is a
  macro there. `x > 0` is false for NaN too, which usually covers it.
