#pragma once
#include <Arduino.h>
#include "core/EventBus.h"
#include "core/StateManager.h"
#include "hw/Mcp23017.h"
#include "Config.h"
// ============================================================
//  RelayManager — SEUL module autorisé à piloter physiquement
//  les relais. Écoute RELAY_CHANGED (émis par le StateManager)
//  et applique sur les MCP23017. Gère ON/OFF, impulsion (momentary)
//  et fail-safe/inversé. Tâche dédiée pour les temporisations
//  (NON BLOQUANT, jamais de delay()).
//
//  Le mapping port B/A des modules relais est géré dans le driver
//  (Mcp23017 construit avec swapPorts=true) : ici on raisonne en
//  index relais simples 0..15 par banque.
// ============================================================

enum class RelayType : uint8_t { ONOFF, MOMENTARY, FAILSAFE };

struct RelayCfg {
  char      name[24] = "Relay";
  RelayType type     = RelayType::ONOFF;
  bool      defaultState = false;  // état logique au boot
  uint32_t  durationMs   = 3000;   // pour MOMENTARY
  bool      enabled      = true;
};

class RelayManager {
public:
  static RelayManager& get() { static RelayManager r; return r; }

  void begin() {
    _bank[0].begin();
    _bank[1].begin();
    // État sûr au démarrage : applique les default_state.
    for (uint8_t i = 0; i < RELAY_COUNT; i++)
      State().setRelay(i, _cfg[i].defaultState, true);

    // Abonnement : on applique sur le HW quand l'état logique change.
    Bus().on([this](const Event& e) {
      if (e.type == Ev::RELAY_CHANGED) applyHardware(e.data1, e.data2 != 0);
    });

    xTaskCreatePinnedToCore(_taskTrampoline, "relay", 3072, this, 4, nullptr, 1);
  }

  RelayCfg& cfg(uint8_t i) { return _cfg[i]; }

  // Commande publique : passe TOUJOURS par le StateManager.
  void command(uint8_t i, bool on) {
    if (i >= RELAY_COUNT || !_cfg[i].enabled) return;
    if (_cfg[i].type == RelayType::MOMENTARY && on) {
      State().setRelay(i, true);
      _pulseEnd[i] = millis() + _cfg[i].durationMs;   // retour OFF auto
    } else {
      State().setRelay(i, on);
      _pulseEnd[i] = 0;
    }
  }

  void toggle(uint8_t i) { command(i, !State().relay(i)); }

private:
  // Banques relais : OUTPUT + mapping port B/A (swapPorts=true).
  RelayManager()
    : _bank{ Mcp23017(I2C_RELAY_BANK_0, true, true),
             Mcp23017(I2C_RELAY_BANK_1, true, true) } {}

  Mcp23017 _bank[2];
  RelayCfg _cfg[RELAY_COUNT];
  uint32_t _pulseEnd[RELAY_COUNT] = {0};

  // Traduit l'état LOGIQUE -> niveau électrique et écrit le MCP23017.
  void applyHardware(uint8_t i, bool logicalOn) {
    if (i >= RELAY_COUNT) return;
    bool physicalOn = logicalOn;
    if (_cfg[i].type == RelayType::FAILSAFE) physicalOn = !logicalOn; // inversé
    // LOW-level trigger : actif = niveau BAS.
    bool pinHigh = RELAY_ACTIVE_LOW ? !physicalOn : physicalOn;
    uint8_t bank = i / 16;
    uint8_t pin  = i % 16;
    _bank[bank].writePin(pin, pinHigh);   // le swap B/A est fait dans le driver
  }

  static void _taskTrampoline(void* arg) { static_cast<RelayManager*>(arg)->_loop(); }

  void _loop() {
    for (;;) {
      uint32_t now = millis();
      for (uint8_t i = 0; i < RELAY_COUNT; i++) {
        if (_pulseEnd[i] && now >= _pulseEnd[i]) {
          _pulseEnd[i] = 0;
          State().setRelay(i, false);   // fin d'impulsion
        }
      }
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
};

inline RelayManager& Relays() { return RelayManager::get(); }
