# One 24 VAC valve channel

Repeat this circuit five times.

## Logic side

ESP32 GPIO ---- RLED 220R ---- MOC3063 pin 1
ESP32 GND ------------------- MOC3063 pin 2

At 3.3 V and approximately 1.2-1.4 V LED Vf, 220R gives roughly 8.6-9.5 mA.

## Isolated 24 VAC side

TRIAC MT2 / ZONE_OUT -------- MOC3063 pin 6
MOC3063 pin 4 ---- RG 180R --- TRIAC GATE
TRIAC GATE -------- RGM 10k -- TRIAC MT1
TRIAC MT1 ------------------- AC2

Snubber across MT2-MT1:
MT2 ---- RS 100R / 0.5W ---- CS 100nF X2 ---- MT1

External valve:
AC_COM ---- valve coil ---- ZONE_OUT

Recommended triac:
BTA08-600SW (sensitive gate, insulated tab)

Recommended optotriac:
MOC3063M (zero-cross)

Do NOT put a DC flyback diode across a 24 VAC solenoid.
