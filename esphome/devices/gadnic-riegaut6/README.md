# Gadnic RIEGAUT6: 8-zone Wi-Fi irrigation controller (teardown)

**Status: teardown notes only. No ESPHome package yet, and nothing has been
flashed.** This page records what is inside the unit so the next step can be
planned. Anything marked *unconfirmed* is read off the photos and not yet
checked with a meter or a datasheet.

## The product

- Sold as **Gadnic RIEGAUT6** ("Programador de riego"). The board is silkscreened
  `SH_07X`, which matches an OEM controller sold as **SH-07X**: for example
  Diivoo SH-07X ([eBay](https://www.ebay.com/itm/394978566224)) and Forrinx
  SH-07-X ([Made-in-China](https://www.made-in-china.com/showroom/doreen-ssg/product-detailIvbEukoOrdWn/China-Forrinx-Sh-07-X-APP-Water-Valve-Timer-Control-Alexa-Google-Home-Voice-Control-WiFi-Sprinkler-Controller.html)).
  The screenshots in the Gadnic manual come from a Forrinx account. An
  **SH-07X PRO** variant adds master valve and rain/freeze sensor terminals
  ([manual](https://manuals.plus/yangguang/sh-07x-pro-smart-wifi-sprinkler-controller-manual),
  FCC ID 2AR3P-SH-07XPRO, Shenzhen Shenan Yangguang Electronics). The same case
  is also sold under other names as a generic "Tuya 8-zone sprinkler controller".
- Manual (Spanish, PDF):
  [gadnic.com.ar/descargas/RIEGAUT6.pdf](https://www.gadnic.com.ar/descargas/RIEGAUT6.pdf)
- **Stock app:** Smart Life / Tuya. It pairs as "Smart Watering Timer" and needs
  the Tuya cloud.
- **Power:** an included AC-to-AC adapter, 230 V 50 Hz in, **24 VAC 2000 mA** out,
  into a barrel jack on the board.
- **Outputs:** 8 zones for standard **24 VAC** irrigation solenoids, one valve
  per terminal. Not for DC valves: the outputs are triacs (see below).
- **Front:** power LED, Wi-Fi LED, 8 zone LEDs, buzzer, and three buttons: SELECT
  (circle, top), UP (left arrow), DOWN (right arrow).
- **Rated** -10 to 55 °C, 10–90 % RH, non-condensing. Indoor case, no IP rating.

What the stock firmware does, from the manual:

| Action | Buttons |
|---|---|
| Select a zone | Short press UP or DOWN (the zone LED blinks) |
| Zone on / off | SELECT |
| Run all zones | Hold UP about 5 s |
| Cancel all zones | Hold DOWN |
| Wi-Fi pairing mode | Hold SELECT until the Wi-Fi LED blinks fast |
| Reset | Hold UP while powering on, until 2 beeps |

App features: up to 10 timers, cycle and soak, a "Smart Weather" rain skip from
the cloud. **Only one zone runs at a time.**

## Inside

![Board](board.jpg)

PCB silkscreen: **`SH_07X_DS_1348C50_V1`**.

| Ref | Part | Notes |
|---|---|---|
| U15 | **Tuya CB3S** Wi-Fi module (Beken **BK7231N**) | ![CB3S module](wifi-module.jpg) |
| U14 | **Nations N32G452CCL7** MCU (Arm Cortex-M4F), LQFP-48 | A separate microcontroller next to the Wi-Fi module. Marking: `N32G452 / CCL7 / TBD48002` (the last line looks like a lot code). |
| U13 + X1 + BT1 | **Holtek HT1381** (marked `HT1381 / Y6T07HG1`), 32.768 kHz-style crystal, **CR1220** coin cell | A **battery-backed real-time clock** (serial timekeeper, DS1302-style), so the MCU can keep time and schedules without Wi-Fi. |
| U5–U12 | **8 × ST T405Q-600** triacs | One per zone, 4 A / 600 V. Plenty of margin for a 0.3 A valve. |
| S1–S3 | Tactile buttons | Pressed by the front pad |
| — | Buzzer | Centre of the board |
| DC1 | Barrel jack | 24 VAC in from the adapter |
| RV1 + yellow disc (marked XF250) | Input protection | Varistor and what looks like a PTC fuse. *Unconfirmed.* |
| — | Buck converter (inductor "101") with 50 V electrolytics | Makes the low-voltage rail from the 24 VAC |
| CON1 (3-pin), CON2 (2-pin) | Unpopulated headers next to the module | Probably UART/programming. *Pinout unconfirmed.* |

Terminal block (push-in, top to bottom): **COM, COM, Z1–Z8, IN, IN**. The manual
doesn't mention the **IN** pair. It is probably a rain or sensor input.
*Unconfirmed.*

## What this means for ESPHome

This is **not** the TY-W-8L / TY-W-6L board that
[gnacho/esphome-tuya-8zone-valve](https://github.com/gnacho/esphome-tuya-8zone-valve)
supports. On that board the Wi-Fi chip drives the zones directly through shift
registers. Here a separate MCU (with what looks like its own clock) sits between
the Wi-Fi module and the triacs.

That points to a **TuyaMCU** design: the CB3S only talks serial to the MCU, and
the MCU runs the zones, buttons, LEDs and buzzer.

A matching board is documented on Elektroda:
[4/6/8 channel WiFi Smart Garden irrigation controller with TuyaMCU](https://www.elektroda.com/rtvforum/topic3991977.html)
(July 2023, the 4-channel version). It has the same **CB3S + Nations N32G452**
pair, and reports:

- **Zones are TuyaMCU dpIDs 101, 102, 103, 104** on the 4-channel unit, found
  with [TuyaMCU Analyzer](https://github.com/openshwprojects/TuyaMCUAnalyzer).
  The 8-zone unit likely continues to 108. *Unconfirmed.*
- The author **had to desolder the CB3S** and power it from an external 3.3 V
  to flash it, because the MCU disturbed the UART. They resoldered it
  afterwards.
- They ran OpenBeken with the TuyaMCU driver, and added a 30-minute auto-off per
  zone in case Wi-Fi or MQTT drops.

The plan for ESPHome is:

1. Back up the CB3S flash first, with
   [ltchiptool](https://github.com/libretiny-eu/ltchiptool) over UART.
2. Flash ESPHome on the CB3S (`bk72xx`, board `cb3s`) with the
   [`tuya:` component](https://esphome.io/components/tuya.html). On boot,
   ESPHome logs every datapoint the MCU reports. Map those to valve switches.
3. Keep the MCU as it is: buttons, LEDs and probably its offline schedule keep
   working.

Things to sort out before flashing:

- **UART sharing.** The CB3S's flashing UART is wired to the MCU. On the
  Elektroda unit this blocked flashing, and desoldering the module was what
  worked. Holding the MCU in reset or lifting its TX line may avoid that.
  *Untested here.*
- **Power.** Run the board from its own 24 VAC adapter while flashing. Don't also
  feed 3.3 V from the USB adapter.
- If the CB3S can't be flashed, a small ESP32-C3 board wired to its pads (3V3,
  GND, TX, RX) could replace it, with ESPHome's `tuya:` component doing the
  same job. Check first that the board's 3.3 V regulator can supply an ESP32's
  Wi-Fi peaks.

## Credits

- nkrt on Elektroda: the
  [CB3S + N32G452 irrigation controller write-up](https://www.elektroda.com/rtvforum/topic3991977.html)
  with dpIDs and flashing notes

- [gnacho/esphome-tuya-8zone-valve](https://github.com/gnacho/esphome-tuya-8zone-valve):
  the TY-W-8L board this one turned out *not* to be, and a useful reference for
  flashing BK7231N irrigation controllers
- [LibreTiny](https://github.com/libretiny-eu/libretiny) and
  [ltchiptool](https://github.com/libretiny-eu/ltchiptool): ESPHome on Beken chips
