#pragma once
#include <Arduino.h>
#include <vector>
#include "core/EventBus.h"
#include "core/CatalogLock.h"
#include "io/RelayManager.h"
// ============================================================
//  AutomationEngine — la brique EVENT -> ACTION du brief.
//  Pour l'instant volontairement simple : un input déclenche
//  une action sur un relais. Les SÉQUENCES (étape 6) et la
//  logique par TAGS viendront se brancher ici sans tout casser,
//  parce que tout passe déjà par l'EventBus.
// ============================================================

enum class ActionKind : uint8_t { TOGGLE, ON, OFF, PULSE };

struct Automation {
  uint8_t    inputIndex;   // 0..15
  ActionKind action;
  uint8_t    relayIndex;   // 0..31
};

class AutomationEngine {
public:
  static AutomationEngine& get() { static AutomationEngine a; return a; }

  void begin() {
    Bus().on([this](const Event& e) {
      if (e.type == Ev::INPUT_TRIGGERED) onInput((uint8_t)e.data1);
    });
  }

  void clear() { _rules.clear(); }
  void add(const Automation& a) { _rules.push_back(a); }
  const std::vector<Automation>& rules() const { return _rules; }

private:
  AutomationEngine() = default;
  std::vector<Automation> _rules;

  void onInput(uint8_t input) {
    CatalogLock lk;   // _rules peut être reconstruit par un reload à chaud
    for (auto& r : _rules) {
      if (r.inputIndex != input) continue;
      switch (r.action) {
        case ActionKind::TOGGLE: Relays().toggle(r.relayIndex);        break;
        case ActionKind::ON:     Relays().command(r.relayIndex, true); break;
        case ActionKind::OFF:    Relays().command(r.relayIndex, false);break;
        case ActionKind::PULSE:
          Relays().cfg(r.relayIndex).type = RelayType::MOMENTARY;
          Relays().command(r.relayIndex, true);
          break;
      }
    }
  }
};

inline AutomationEngine& Autos() { return AutomationEngine::get(); }
