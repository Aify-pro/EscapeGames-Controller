#pragma once
#include <Arduino.h>
#include <map>
#include "core/EventBus.h"
#include "Config.h"
// ============================================================
//  StateManager — LA vérité unique du système.
//  Personne ne stocke d'état ailleurs. On set ici -> un
//  événement RELAY_CHANGED / TAG_CHANGED part, et web/OLED/
//  Houdini/relais écoutent. Fini les désyncs bouton/relais.
// ============================================================

class StateManager {
public:
  static StateManager& get() { static StateManager s; return s; }

  // ---- Relais (état LOGIQUE, pas le niveau électrique) ----
  void setRelay(uint8_t i, bool on, bool notify = true) {
    if (i >= RELAY_COUNT || _relay[i] == on) return;
    _relay[i] = on;
    if (notify) Bus().publish(Ev::RELAY_CHANGED, i, on ? 1 : 0);
  }
  bool relay(uint8_t i) const { return (i < RELAY_COUNT) ? _relay[i] : false; }

  // ---- Inputs ----
  void setInput(uint8_t i, bool high) {
    if (i >= INPUT_COUNT || _input[i] == high) return;
    _input[i] = high;
    Bus().publish(high ? Ev::INPUT_TRIGGERED : Ev::INPUT_RELEASED, i);
  }
  bool input(uint8_t i) const { return (i < INPUT_COUNT) ? _input[i] : false; }

  // ---- Tags / logique booléenne (puzzle_1_done, alarm_mode...) ----
  void setTag(const String& key, bool val) {
    if (_tags[key] == val) return;
    _tags[key] = val;
    Bus().publish(Ev::TAG_CHANGED, val ? 1 : 0, 0, key.c_str());
  }
  bool tag(const String& key) { return _tags.count(key) ? _tags[key] : false; }
  const std::map<String,bool>& tags() const { return _tags; }

  // ---- État jeu ----
  void setGameRunning(bool run) {
    if (_gameRunning == run) return;
    _gameRunning = run;
    Bus().publish(run ? Ev::GAME_STARTED : Ev::GAME_STOPPED);
  }
  bool gameRunning() const { return _gameRunning; }

  // ---- Réseau (renseigné par NetworkManager) ----
  void setLink(bool eth, bool wifi) { _eth = eth; _wifi = wifi; }
  bool ethUp()  const { return _eth; }
  bool wifiUp() const { return _wifi; }

private:
  StateManager() = default;
  bool _relay[RELAY_COUNT] = {false};
  bool _input[INPUT_COUNT] = {false};
  std::map<String,bool> _tags;
  bool _gameRunning = false;
  bool _eth = false, _wifi = false;
};

inline StateManager& State() { return StateManager::get(); }
