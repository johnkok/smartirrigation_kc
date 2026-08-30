#pragma once

// Rev A PCB GPIO assignment
static constexpr int VALVE_PINS[4] = {26, 27, 25, 33};
static constexpr int MASTER_VALVE_PIN = 32;
static constexpr bool VALVE_ACTIVE_HIGH = true;
static constexpr bool MASTER_ACTIVE_HIGH = true;

static constexpr int RS485_RX_PIN = 16;
static constexpr int RS485_TX_PIN = 17;
static constexpr int RS485_DE_RE_PIN = 4;

static constexpr int RAIN_PIN = 34;
static constexpr bool RAIN_ACTIVE_LOW = true;

static constexpr int FLOW_PIN = 35;

// GPIO34/35 have PCB external pull-ups.
// Do not depend on INPUT_PULLUP for these pins.
