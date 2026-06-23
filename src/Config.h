#pragma once
// ============================================================
//  Config.h — mapping GPIO + constantes matérielles
//  Cible : ESP32 WT32-ETH01 (LAN8720 RMII)
// ============================================================

// ---- I2C (bus partagé : OLED + 3x PCF8575) ----
// IO14/IO15 choisis car libres et sans conflit ETH.
// IO12 INTERDIT (strapping flash 1.8V -> boot fail si tiré haut).
static constexpr int PIN_I2C_SDA = 14;
static constexpr int PIN_I2C_SCL = 15;

// ---- Joystick HW-504 ----
// VRx/VRy doivent être sur ADC1 (ADC2 inutilisable avec le réseau actif).
static constexpr int PIN_JOY_X  = 35;  // ADC1, input-only
static constexpr int PIN_JOY_Y  = 39;  // ADC1, input-only
static constexpr int PIN_JOY_SW = 5;   // pull-up interne dispo

// ---- Adresses I2C ----
static constexpr uint8_t I2C_RELAY_BANK_0 = 0x20; // relais 1..16
static constexpr uint8_t I2C_RELAY_BANK_1 = 0x21; // relais 17..32
static constexpr uint8_t I2C_INPUT_BANK   = 0x22; // entrées 1..16
static constexpr uint8_t I2C_OLED         = 0x3C; // SSD1306 0.96"

// ---- Capacités ----
static constexpr uint8_t RELAY_COUNT = 32;
static constexpr uint8_t INPUT_COUNT = 16;

// ---- Réseau Ethernet WT32-ETH01 (NE PAS modifier) ----
#define ETH_PHY_TYPE   ETH_PHY_LAN8720
#define ETH_PHY_ADDR   1
#define ETH_PHY_MDC    23
#define ETH_PHY_MDIO   18
#define ETH_PHY_POWER  16
#define ETH_CLK_MODE   ETH_CLOCK_GPIO0_IN

// ---- Logique relais ----
// Modules optocouplés LOW-LEVEL trigger : niveau BAS = relais activé.
static constexpr bool RELAY_ACTIVE_LOW = true;

#ifndef FW_VERSION
#define FW_VERSION "0.0.0"
#endif
