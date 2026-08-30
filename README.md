# Smart Irrigation

Smart Irrigation is an engineering prototype for a four-zone ESP32 irrigation controller. It combines two Modbus/RS-485 soil probes, four 24 VAC zone outputs, an optional master-valve output, rain and flow inputs, MQTT/Home Assistant integration, and an Android control app.

## Repository layout

| Directory | Contents |
|---|---|
| `board/` | KiCad project, PCB placement starter, design notes, BOM, and connectivity table |
| `esp32/` | PlatformIO firmware for sensing, automation, MQTT, and the REST API |
| `home_assistant/` | Example Home Assistant automations |
| `android/` | Minimal Android controller application |

## Hardware architecture

- The ESP32 and logic are powered by a separate isolated 5 V USB supply.
- Soil probes use a 3.3 V MAX3485-compatible RS-485 interface and an external sensor supply.
- Five isolated MOC3063M/BTA08-600SW channels switch four zones and one master valve from a 24 VAC irrigation transformer.
- The valve supply is protected by a replaceable fuse and bidirectional TVS.

Never connect a DC-only sensor to the 24 VAC valve supply. The PCB is a Rev-A placement starter and is not routed or ready for fabrication; verify footprints, complete the electrical schematic and routing, and run ERC/DRC before ordering boards.

## Getting started

1. Review [`board/README.md`](board/README.md), the BOM, and the connectivity table.
2. Open `board/smartirrigation.kicad_pro` in KiCad 9 or newer.
3. Copy `esp32/include/config.h` to a safe backup, then adjust Wi-Fi/MQTT and hardware settings as needed.
4. Build the firmware:

   ```bash
   cd esp32
   pio run
   ```

5. Follow [`esp32/README.md`](esp32/README.md) to assign unique Modbus addresses and provision the controller.
6. Configure Home Assistant from [`home_assistant/README.md`](home_assistant/README.md), or build the Android app with Gradle/Android Studio.

## Default GPIO map

| Function | GPIO |
|---|---:|
| Zone 1–4 | 26, 27, 25, 33 |
| Master valve | 32 |
| RS-485 RX / TX / DE+RE | 16 / 17 / 4 |
| Rain / flow | 34 / 35 |

## Safety

This repository is not a certified production design. Use normally closed valves, a correctly sized transformer and fuse, weatherproof enclosures and glands, and suitable surge/ESD protection. Bench-test every output with the actual valve load and validate failure behavior before unattended operation.
