# ESP32 Irrigation Controller — 24 VAC Rev A

Engineering-prototype PCB package for:

- ESP32 DevKit / ESP32-WROOM development module
- 2x RS-485 Modbus soil probes
- 4x 24 VAC irrigation zone valves
- 1x 24 VAC master valve / 24 VAC relay-coil output
- flow-meter input
- rain-switch input
- MQTT / Home Assistant firmware pinout compatible with the enhanced project

## Important architecture decision

The **24 VAC transformer is used only for the irrigation valve field circuit**.

The ESP32/controller is powered from a separate isolated **5 V USB supply**.
The RS-485 soil probes use a separate DC sensor supply connector (`VSENS+ / GND`).
Do not connect a sensor to 24 VAC unless its exact label/manual explicitly specifies AC input.

This separation is intentional: it improves noise immunity and avoids a rectifier/buck power
stage sharing the same transformer that is switching inductive solenoids.

## Valve output topology

Each of the five outputs is:

ESP32 GPIO -> 220 ohm -> MOC3063M zero-cross optotriac -> BTA08-600SW triac -> 24 VAC valve

Each power channel also has:
- 180 ohm triac gate resistor
- 10 kOhm gate-to-MT1 resistor
- 100 ohm + 100 nF X2 RC snubber across the triac
- optocoupler galvanic separation from ESP32 logic

24 VAC input protection:
- replaceable 3.15 A slow-blow fuse (adjust to transformer and valve current)
- bidirectional SMBJ36CA TVS across the 24 VAC input

## Connector concept

- J1: +5V / GND controller power
- J2: RS485 A / B / GND / VSENS+
- J3: Rain / GND / 3V3
- J4: Flow / GND / 3V3
- J5: 24VAC-1 / 24VAC-2 transformer input
- J6: 24VAC-COM / Z1 / Z2 / Z3 / Z4 / MASTER

External valve wiring:
- one wire of every valve -> `24VAC-COM`
- second wire -> its corresponding `Z1..Z4` or `MASTER`

## ESP32 pin map

| Function | GPIO |
|---|---:|
| Zone 1 | 26 |
| Zone 2 | 27 |
| Zone 3 | 25 |
| Zone 4 | 33 |
| Master | 32 |
| RS485 TX | 17 |
| RS485 RX | 16 |
| RS485 DE+/RE | 4 |
| Rain | 34 |
| Flow | 35 |

GPIO34 and GPIO35 are input-only. The PCB therefore provides external input conditioning /
pull-up components rather than relying on ESP32 internal pull-ups.

## PCB zones

The proposed board is approximately 160 x 100 mm, 2 layers, 1.6 mm FR-4.

- left: 5 V logic, ESP32, inputs, RS-485
- center: optocoupler isolation barrier
- right: 24 VAC triacs, snubbers and valve terminals

Keep at least 3 mm copper clearance across the optocoupler barrier. 24 VAC is SELV, but the
separation is still useful for EMC/noise.

## Manufacturing status

This is a **Rev A engineering design package**, not a certified production controller.
Before ordering a production run:

1. Open the board in KiCad and run ERC/DRC.
2. Confirm the exact ESP32 DevKit header spacing used by your module.
3. Confirm the exact terminal-block pitch and fuse-holder footprint you will buy.
4. Confirm each valve's steady-state and inrush current.
5. Adjust the 24 VAC fuse accordingly.
6. Verify the MOC3063/BTA08 channel with one real valve on the bench.
7. Perform surge/ESD testing appropriate for outdoor cabling.

The package includes a layout drawing, BOM, connectivity table, firmware pin header and a
KiCad board starter file.
