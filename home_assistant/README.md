# Home Assistant integration

The ESP32 v2 firmware connects directly to the same MQTT broker used by Home Assistant.

## Recommended topology

ESP32 -> Wi-Fi LAN -> MQTT broker -> Home Assistant

Do not expose the ESP32 HTTP API or MQTT broker directly to the public Internet.
For remote phone access, expose Home Assistant through your existing secure remote-access method
(Home Assistant Cloud, VPN/WireGuard, etc.) and control irrigation through Home Assistant.

## 1. Install/configure MQTT in Home Assistant

The easiest common setup is the Mosquitto broker app/add-on plus the Home Assistant MQTT integration.

In Home Assistant:
1. Settings -> Devices & services.
2. Add/configure MQTT.
3. Ensure MQTT discovery is enabled.
4. Reconfigure the ESP32 Wi-Fi portal and enter the MQTT broker IP, port, username and password.

The ESP32 publishes retained Home Assistant MQTT discovery records and listens for
`homeassistant/status`, so entities are republished after Home Assistant starts.

Expected entities include:
- Soil 1/2 moisture
- Soil 1/2 temperature
- Soil 1/2 EC
- rain detected
- irrigation flow
- irrigation fault
- zone 1..4 valve switches
- zone 1..4 start moisture threshold
- zone 1..4 stop moisture threshold
- zone 1..4 maximum run time

## Weather-aware watering

Weather intelligence should live in Home Assistant, not in the ESP32.
The ESP32 subscribes to:

`soil_irrigation/<device-id>/weather_block/set`

Payload:
- `ON` = block automatic/scheduled irrigation
- `OFF` = allow it

This lets Home Assistant combine any weather provider, rain forecast, temperature, season,
or rainfall sensor without placing Internet API credentials in the controller.

Example automation snippets are in `automations.yaml`.

## Fault handling

The controller stops all zones if:
- measured flow is above the configured maximum
- a valve has been running long enough but essentially no flow is detected

It then latches a safety fault.

Clear it only after checking the irrigation system by publishing:

`soil_irrigation/<device-id>/fault/clear`

payload `CLEAR`

A Home Assistant button can be added later if desired.

## Zones 3 and 4

The hardware supports four valve outputs. Only two soil sensors are configured in this project,
so zones 3 and 4 are manual/scheduled zones by default.

`ZONE_SENSOR_MAP` in `firmware/include/config.h` controls mapping:
- `{0,1,-1,-1}` = zone 1 uses sensor 1, zone 2 uses sensor 2, zones 3/4 have no sensor.
- `{0,1,0,1}` = zones 1/3 use sensor 1, zones 2/4 use sensor 2.

If you later add four RS-485 probes, expand `SENSOR_COUNT`, add IDs, and map one sensor per zone.

## Suggested HA dashboard

Create a dashboard card with:
- four valve switches
- moisture gauges for sensor 1 and sensor 2
- current flow
- rain binary sensor
- irrigation fault binary sensor
- start/stop thresholds
- maximum run values

For normal daily use, the Android app is optional once Home Assistant is integrated.
