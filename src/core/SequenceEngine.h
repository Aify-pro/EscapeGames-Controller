#pragma once
#include <Arduino.h>
#include <vector>
#include "core/EventBus.h"
#include "core/CatalogLock.h"
#include "core/AutomationEngine.h"   // réutilise ActionKind (TOGGLE/ON/OFF/PULSE)
#include "io/RelayManager.h"
#include "Config.h"
// ============================================================
//  SequenceEngine — ÉTAPE 6.
//  Une "séquence" est ici une SCÈNE : un état INSTANTANÉ
//  multi-relais (« Reset », « Ouverture », « Nettoyage »...).
//  Déclenchée par l'événement SEQUENCE_RUN (data1 = index),
//  elle applique sa liste d'actions relais EN UNE FOIS, via le
//  RelayManager — jamais de pilotage direct (cf. règle d'or).
//
//  ⚠️ Distinction avec l'Étape 7 (AnimationEngine) : ici tout est
//     appliqué d'un bloc, sans temporisation entre les pas. Les
//     timelines ÉCHELONNÉES dans le temps seront l'AnimationEngine.
//
//  Point d'entrée UNIQUE : trigger() publie SEQUENCE_RUN sur le
//  bus -> dashboard, console et (Étape 8) joystick passent tous
//  par là, sans connaître les détails d'application.
// ============================================================

struct SeqStep {
  uint8_t    relayIndex = 0;          // 0..31
  ActionKind action     = ActionKind::ON;
};

struct Sequence {
  char name[24] = "Sequence";
  bool enabled  = true;
  std::vector<SeqStep> steps;
};

class SequenceEngine {
public:
  static SequenceEngine& get() { static SequenceEngine s; return s; }

  void begin() {
    Bus().on([this](const Event& e) {
      if (e.type == Ev::SEQUENCE_RUN) run((uint8_t)e.data1);
    });
  }

  // --- gestion du catalogue (rempli par ConfigStore) ---
  void clear() { _seqs.clear(); }
  void add(const Sequence& s) { _seqs.push_back(s); }
  const std::vector<Sequence>& list() const { return _seqs; }
  uint8_t count() const { return (uint8_t)_seqs.size(); }

  // Déclenchement : TOUJOURS via le bus (un seul chemin).
  void trigger(uint8_t i) { Bus().publish(Ev::SEQUENCE_RUN, i); }

private:
  SequenceEngine() = default;
  std::vector<Sequence> _seqs;

  void run(uint8_t i) {
    CatalogLock lk;   // _seqs peut être reconstruit par un reload à chaud
    if (i >= _seqs.size()) return;
    const Sequence& s = _seqs[i];
    if (!s.enabled) return;
    apply(s);
    if (s.name[0]) {
      char msg[24];
      snprintf(msg, sizeof(msg), "Scene: %s", s.name);
      Bus().publish(Ev::LOG, 0, 0, msg);
    }
  }

  // Applique chaque pas via le RelayManager (donc via le StateManager).
  void apply(const Sequence& s) {
    for (const auto& st : s.steps) {
      if (st.relayIndex >= RELAY_COUNT) continue;
      switch (st.action) {
        case ActionKind::TOGGLE: Relays().toggle(st.relayIndex);         break;
        case ActionKind::ON:     Relays().command(st.relayIndex, true);  break;
        case ActionKind::OFF:    Relays().command(st.relayIndex, false); break;
        case ActionKind::PULSE:
          Relays().cfg(st.relayIndex).type = RelayType::MOMENTARY;
          Relays().command(st.relayIndex, true);
          break;
      }
    }
  }
};

inline SequenceEngine& Seq() { return SequenceEngine::get(); }
