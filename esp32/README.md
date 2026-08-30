# ESP32 Soil Irrigation Controller v2 — 2 RS-485 sensors / 4 watering zones

This starter project contains:

- ESP32 firmware for **2 RS-485 / Modbus RTU soil sensors**
- 2 watering valve outputs
- Automatic watering by soil-moisture threshold with hysteresis
- Weekly schedules stored persistently in ESP32 NVS
- Maximum valve run-time safety cutoff
- Wi-Fi setup using a captive portal
- REST/JSON API
- Android app for status, manual valve control, thresholds, and schedules

## Assumed sensor protocol

The AliExpress page could not be read directly when this project was generated. The firmware therefore uses the common VEM SEE / generic 3-in-1, 4-in-1, 7-in-1 soil sensor mapping:

| Register | Value | Scale |
|---|---|---|
| 0x0000 | Soil moisture | /10 = % |
| 0x0001 | Soil temperature | /10 = °C |
| 0x0002 | EC | 1 = µS/cm |
| 0x0003 | pH, if present | /10 |
| 0x07D0 | Modbus address | 1..254 |
| 0x07D1 | Baud selection | often 0=2400, 1=4800, 2=9600 |

Default baud is often **4800 8N1**, but some versions use 9600. Change `RS485_BAUD` and the register mapping in `include/config.h` if your exact manual differs.

## Recommended hardware

- ESP32 DevKit
- **3.3 V-compatible RS-485 transceiver**, e.g. MAX3485 / SP3485
- 2 × RS-485 soil probes
- 2 × DC solenoid valves
- 2 × logic-level MOSFET valve driver channels or an opto-isolated MOSFET/relay module
- Flyback diode across each DC solenoid coil if your driver module does not already contain one
- Suitable valve power supply (commonly 12 V or 24 V — match your valves)
- Sensor supply matching the sensor label/manual
- Common ground between ESP32 and non-isolated driver/transceiver circuitry

**Do not power a solenoid valve directly from an ESP32 GPIO.**

For long outdoor RS-485 cable runs, use twisted pair for A/B, consider 120-ohm termination at the physical ends of the bus, and use surge/ESD protection.

## Default ESP32 pinout

| Function | ESP32 pin |
|---|---:|
| RS485 RX (RO) | GPIO16 |
| RS485 TX (DI) | GPIO17 |
| RS485 DE + /RE | GPIO4 |
| Valve 1 | GPIO26 |
| Valve 2 | GPIO27 |

Edit `firmware/include/config.h` for your board.

## 1. Give the two sensors unique Modbus addresses

Most generic probes ship as address 1. You cannot reliably use two address-1 devices on one bus.

1. Disconnect sensor 1.
2. Connect only the sensor that will become sensor 2.
3. Upload `firmware/tools/change_sensor_address/change_sensor_address.ino`.
4. Set `OLD_ID`, `NEW_ID`, and baud at the top of that sketch.
5. Run it once and power-cycle the sensor.
6. Reconnect both sensors.

The normal firmware expects sensor IDs 1 and 2.

## 2. Build ESP32 firmware

This project uses PlatformIO.

```bash
cd firmware
pio run -t upload
pio device monitor
```

On the first boot, if stored Wi-Fi credentials are unavailable, the ESP32 creates:

- SSID: `SoilIrrigation-Setup`

Connect a phone to that AP and complete the captive-portal Wi-Fi setup.

After connection, the serial monitor prints the ESP32 IP address. The Android app lets you enter that IP, e.g.:

`http://192.168.1.70`

## 3. Automatic watering behavior

Each zone has:

- `autoEnabled`
- `startBelowPct` — if moisture is below this level, watering may start
- `stopAbovePct` — once watering due to moisture is active, it stops at/above this level
- `maxRunMinutes`
- weekly schedule entries

Example:

- Start below 28%
- Stop above 36%
- Maximum run 20 minutes

This hysteresis prevents rapid on/off cycling.

### Schedule behavior

A scheduled start is allowed only when:

- schedule entry is enabled
- current weekday matches
- current minute matches the scheduled start
- associated sensor is healthy
- valve is not already running

A schedule entry runs for its configured duration, subject to `maxRunMinutes`.

By default a schedule is a *watering permission/action* independent of the moisture threshold. If you want schedules to water only when soil is dry, set `SCHEDULE_REQUIRES_DRY_SOIL` to `true` in `config.h`.

## REST API

### GET `/api/status`

Returns Wi-Fi, time, sensors and valve state.

### GET `/api/config`

Returns thresholds and schedule configuration.

### POST `/api/config`

Send a JSON object containing one or both zone objects.

Example:

```json
{
  "zones": [
    {
      "id": 1,
      "autoEnabled": true,
      "startBelowPct": 28,
      "stopAbovePct": 36,
      "maxRunMinutes": 20,
      "schedules": [
        {
          "enabled": true,
          "daysMask": 127,
          "startMinute": 420,
          "durationMinutes": 10
        }
      ]
    }
  ]
}
```

`daysMask` bits are Sunday..Saturday, bit 0..6. `127` means every day.
`startMinute` is minutes after midnight. `420` = 07:00.

### POST `/api/valve?id=1&on=true`

Manual valve control. Manual ON is still limited by `maxRunMinutes`.

### POST `/api/valve?id=1&on=false`

Turns valve off immediately.

## Safety notes

This is an engineering starter project, not a certified irrigation controller. For unattended watering, add:

- normally-closed valves
- a master shutoff valve
- flow sensor / leak detection
- enclosure and cable glands suitable for outdoor use
- fuse on the valve supply
- watchdog/reboot testing
- brownout testing
- independent maximum-flow or maximum-runtime protection

The firmware intentionally switches both valves OFF during startup and refuses automatic moisture watering when the associated soil sensor is stale/unreadable.


---

# v2 enhancements

This enhanced version adds:

- 4 valve zones
- optional master valve/pump relay
- flow meter pulse input and liters/minute calculation
- latched no-flow / excessive-flow safety shutdown
- rain sensor input
- Home Assistant MQTT integration with automatic discovery
- Home Assistant control of all 4 valves
- editable Home Assistant threshold/max-runtime entities
- Home Assistant-driven weather irrigation block
- retained MQTT state and online/offline availability
- Home Assistant example automations
- safe architecture for remote control: access Home Assistant remotely, not the ESP32 directly

See `home_assistant/README.md`.

## New default pinout

| Function | GPIO |
|---|---:|
| Valve 1 | 26 |
| Valve 2 | 27 |
| Valve 3 | 25 |
| Valve 4 | 33 |
| Master valve/pump | 32 |
| Rain input | 34 |
| Flow pulse input | 35 |
| RS-485 RX/TX/DE | 16 / 17 / 4 |

Verify these pins against your exact ESP32 board before wiring.

GPIO34/35 are input-only and do not have normal internal pull-ups on classic ESP32 hardware,
so use external pull-up/down components where required by the actual rain/flow sensor interface.
