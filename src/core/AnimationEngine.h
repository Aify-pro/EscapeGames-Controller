#pragma once
#include <Arduino.h>
#include <vector>
#include "core/EventBus.h"
#include "core/CatalogLock.h"
#include "core/AutomationEngine.h"   // réutilise ActionKind
#include "io/RelayManager.h"
#include "Config.h"
// ============================================================
//  AnimationEngine — ÉTAPE 7.
//  Une "animation" est une TIMELINE non bloquante : des pas
//  (relais + action) horodatés à un offset en ms depuis le
//  début. Contrairement aux SCÈNES (Étape 6, instantanées),
//  les pas sont ÉCHELONNÉS dans le temps. Cas d'usage : clignotement,
//  ouverture séquencée, effet lumineux progressif.
//
//  Jamais de delay() : une tâche FreeRTOS dédiée fait avancer un
//  curseur sur chaque animation active selon millis(). Plusieurs
//  animations peuvent tourner en parallèle. 'loop' rejoue le cycle.
//
//  Concurrence : la tâche est SEULE propriétaire de la liste des
//  animations actives (_active). Le démarrage/arrêt depuis le bus
//  ne touche pas _active directement -> il passe par une petite
//  file de commandes (_cmdQ) drainée par la tâche. Pas de mutex,
//  pas de course de données.
//
//  Application des pas : via Relays().command() (donc StateManager),
//  comme partout. Aucun pilotage direct.
// ============================================================

struct AnimStep {
  uint32_t   atMs       = 0;          // offset depuis t0 du cycle
  uint8_t    relayIndex = 0;          // 0..31
  ActionKind action     = ActionKind::ON;
};

struct Animation {
  char name[24]      = "Animation";
  bool enabled       = true;
  bool loop          = false;         // rejouer en boucle ?
  uint32_t periodMs  = 1000;          // durée d'un cycle (pour loop / fin)
  std::vector<AnimStep> steps;        // supposés triés par atMs croissant
};

class AnimationEngine {
public:
  static AnimationEngine& get() { static AnimationEngine a; return a; }

  void begin() {
    _cmdQ = xQueueCreate(8, sizeof(Cmd));
    Bus().on([this](const Event& e) {
      if      (e.type == Ev::ANIMATION_RUN)  pushCmd(true,  (uint8_t)e.data1);
      else if (e.type == Ev::ANIMATION_STOP) pushCmd(false, (uint8_t)e.data1);
    });
    xTaskCreatePinnedToCore(_taskTrampoline, "anim", 3072, this, 4, nullptr, 1);
  }

  // --- catalogue (rempli par ConfigStore) ---
  void clear() { _anims.clear(); }
  void add(const Animation& a) { _anims.push_back(a); }
  const std::vector<Animation>& list() const { return _anims; }
  uint8_t count() const { return (uint8_t)_anims.size(); }

  // --- déclenchement (toujours via le bus) ---
  void trigger(uint8_t i) { Bus().publish(Ev::ANIMATION_RUN,  i); }
  void stop(uint8_t i)    { Bus().publish(Ev::ANIMATION_STOP, i); }
  void stopAll()          { Bus().publish(Ev::ANIMATION_STOP, 255); }

private:
  AnimationEngine() = default;

  struct Cmd      { bool start; uint8_t id; };
  struct Playback { uint8_t idx; uint32_t startMs; uint16_t cursor; };

  std::vector<Animation> _anims;
  std::vector<Playback>  _active;     // propriété EXCLUSIVE de la tâche
  QueueHandle_t _cmdQ = nullptr;

  void pushCmd(bool start, uint8_t id) {
    if (!_cmdQ) return;
    Cmd c{start, id};
    xQueueSend(_cmdQ, &c, 0);
  }

  static void _taskTrampoline(void* arg) { static_cast<AnimationEngine*>(arg)->_loop(); }

  void _loop() {
    for (;;) {
      {
        CatalogLock lk;   // _anims lu ici ; protégé d'un reload à chaud
        drainCommands();
        advance();
      }
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }

  // --- traitement des commandes start/stop (dans la tâche) ---
  void drainCommands() {
    Cmd c;
    while (xQueueReceive(_cmdQ, &c, 0) == pdTRUE) {
      if (c.start) startAnim(c.id);
      else         stopAnim(c.id);
    }
  }

  void startAnim(uint8_t id) {
    if (id >= _anims.size() || !_anims[id].enabled) {
      Bus().publish(Ev::ANIMATION_FINISHED, id);   // rien à jouer -> notifie quand même l'UI
      return;
    }
    // déjà active ? on la redémarre proprement.
    for (auto& p : _active) {
      if (p.idx == id) { p.startMs = millis(); p.cursor = 0; return; }
    }
    _active.push_back({ id, millis(), 0 });
    char msg[24]; snprintf(msg, sizeof(msg), "Anim: %s", _anims[id].name);
    Bus().publish(Ev::LOG, 0, 0, msg);
  }

  void stopAnim(uint8_t id) {
    for (size_t k = 0; k < _active.size(); ) {
      if (id == 255 || _active[k].idx == id) {
        uint8_t finished = _active[k].idx;
        _active.erase(_active.begin() + k);
        Bus().publish(Ev::ANIMATION_FINISHED, finished);
      } else {
        k++;
      }
    }
  }

  // --- avancement temporel des animations actives ---
  void advance() {
    uint32_t now = millis();
    for (size_t k = 0; k < _active.size(); ) {
      Playback& p = _active[k];
      const Animation& a = _anims[p.idx];
      uint32_t elapsed = now - p.startMs;

      // applique tous les pas dont l'offset est atteint
      while (p.cursor < a.steps.size() && a.steps[p.cursor].atMs <= elapsed) {
        apply(a.steps[p.cursor]);
        p.cursor++;
      }

      bool cycleDone = (p.cursor >= a.steps.size());
      if (cycleDone) {
        uint32_t period = a.periodMs ? a.periodMs
                          : (a.steps.empty() ? 0 : a.steps.back().atMs + 1);
        if (a.loop) {
          if (elapsed >= period) { p.startMs += period; p.cursor = 0; }
          k++;                                  // reste active
        } else {
          uint8_t finished = p.idx;
          _active.erase(_active.begin() + k);
          Bus().publish(Ev::ANIMATION_FINISHED, finished);
        }
      } else {
        k++;
      }
    }
  }

  void apply(const AnimStep& st) {
    if (st.relayIndex >= RELAY_COUNT) return;
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
};

inline AnimationEngine& Anim() { return AnimationEngine::get(); }
