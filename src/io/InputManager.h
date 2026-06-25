#pragma once
#include <Arduino.h>
#include "core/EventBus.h"
#include "core/StateManager.h"
#include "hw/Mcp23017.h"
#include "Config.h"
// ============================================================
//  InputManager — ÉTAPE 2. Lit les 16 entrées (MCP23017 0x25),
//  applique debounce + logique NO/NC, et n'émet QUE des
//  événements (INPUT_TRIGGERED / INPUT_RELEASED). Il ne pilote
//  jamais un relais directement (c'est le rôle de l'Automation).
//
//  Câblage : capteur entre la pin et GND. Pull-up interne du
//  MCP23017 (pinMode INPUT_PULLUP) -> ligne au repos = HIGH.
//    NO (normalement ouvert) : actif quand la ligne passe LOW
//    NC (normalement fermé)   : actif quand la ligne passe HIGH
//  Pas de mapping port B/A ici : la banque d'entrées est câblée
//  en direct (read16() lit GPIOA=bits0..7, GPIOB=bits8..15).
// ============================================================

enum class InputMode : uint8_t { NO, NC };

struct InputCfg {
  char      name[24]  = "Input";
  InputMode mode      = InputMode::NO;
  uint16_t  debounceMs = 50;
  bool      enabled   = true;
};

class InputManager {
public:
  static InputManager& get() { static InputManager m; return m; }

  void begin() {
    _bank.begin();   // 0x25, pins en INPUT_PULLUP -> repos = HIGH
    for (uint8_t i = 0; i < INPUT_COUNT; i++) _lastRaw[i] = true; // repos = HIGH
    xTaskCreatePinnedToCore(_taskTrampoline, "input", 3072, this, 4, nullptr, 1);
  }

  InputCfg& cfg(uint8_t i) { return _cfg[i]; }

private:
  // Banque entrées : INPUT_PULLUP (outputs=false), pas de swap.
  InputManager() : _bank(I2C_INPUT_BANK, false, false) {}

  Mcp23017 _bank;
  InputCfg _cfg[INPUT_COUNT];
  bool     _stable[INPUT_COUNT]  = {false};   // état logique stable (actif/inactif)
  bool     _lastRaw[INPUT_COUNT] = {true};    // dernière lecture brute (HIGH au repos)
  uint32_t _lastChange[INPUT_COUNT] = {0};

  static void _taskTrampoline(void* arg) { static_cast<InputManager*>(arg)->_loop(); }

  void _loop() {
    for (;;) {
      uint16_t word = _bank.read16();
      uint32_t now  = millis();

      for (uint8_t i = 0; i < INPUT_COUNT; i++) {
        if (!_cfg[i].enabled) continue;
        bool rawHigh = (word >> i) & 0x01;

        // Front détecté -> on (re)démarre le timer de debounce
        if (rawHigh != _lastRaw[i]) {
          _lastRaw[i]    = rawHigh;
          _lastChange[i] = now;
        }

        // Stable assez longtemps -> on valide
        if (now - _lastChange[i] >= _cfg[i].debounceMs) {
          bool active = (_cfg[i].mode == InputMode::NO) ? !rawHigh : rawHigh;
          if (active != _stable[i]) {
            _stable[i] = active;
            State().setInput(i, active);   // -> publie INPUT_TRIGGERED / RELEASED
            if (active && _cfg[i].name[0])
              Bus().publish(Ev::LOG, i, 0, _cfg[i].name);
          }
        }
      }
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
};

inline InputManager& Inputs() { return InputManager::get(); }
