#pragma once
// ============================================================
//  Config.h — mapping GPIO + constantes matérielles
//  Cible : ESP32 WT32-ETH01 (LAN8720 RMII)
//
//  ⚠️ Valeurs CONFIRMÉES par scan/test physique (cf. CONTEXTE §2).
//     L'ancienne Config.h (I2C 14/15, adresses 0x20/21/22) était
//     fausse pour ce montage. NE PAS y revenir.
// ============================================================

// ---- I2C (bus partagé : OLED + 3x MCP23017) ----
// Sur le WT32-ETH01 avec Ethernet ACTIF, seuls GPIO32/33 marchent
// pour le I2C (contrainte hardware de la carte, pas un choix soft).
static constexpr int PIN_I2C_SDA = 33;
static constexpr int PIN_I2C_SCL = 32;

// ---- Joystick HW-504 ----
// VRx/VRy sur ADC1 (ADC2 inutilisable avec le réseau actif).
static constexpr int PIN_JOY_X  = 36;  // ADC1, input-only
static constexpr int PIN_JOY_Y  = 39;  // ADC1, input-only
// SW sur IO35 (remplace IO12 = strapping flash, instable à l'appui).
// ⚠️ IO35 est input-only et SANS pull-up interne -> prévoir un
//    pull-up EXTERNE (10k vers 3.3V) au câblage du bouton (Étape 8).
static constexpr int PIN_JOY_SW = 35;

// ---- Adresses I2C MCP23017 (Soldered, confirmées par scan) ----
static constexpr uint8_t I2C_RELAY_BANK_0 = 0x27; // relais 1..16  (aucun jumper)
static constexpr uint8_t I2C_RELAY_BANK_1 = 0x26; // relais 17..32 (JP5 -> GND)
static constexpr uint8_t I2C_INPUT_BANK   = 0x25; // entrées 1..16 (JP6 -> GND)
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
