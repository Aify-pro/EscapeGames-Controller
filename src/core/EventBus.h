#pragma once
#include <Arduino.h>
#include <functional>
#include <vector>
// ============================================================
//  EventBus — coeur du système. TOUS les modules communiquent
//  uniquement via événements. Dispatch mono-thread (1 tâche)
//  -> pas de course de données entre modules.
// ============================================================

enum class Ev : uint8_t {
  NONE = 0,
  INPUT_TRIGGERED,     // data1 = index input (0..15)
  INPUT_RELEASED,
  RELAY_CHANGED,       // data1 = index relais, data2 = état (0/1)
  RELAY_CMD,           // demande de changement : data1=index, data2=état
  GAME_STARTED,
  GAME_STOPPED,
  ETH_UP,
  ETH_DOWN,
  WIFI_UP,
  WIFI_DOWN,
  ANIMATION_FINISHED,  // data1 = id animation
  ANIMATION_RUN,       // data1 = id animation (commande : démarrer)
  ANIMATION_STOP,      // data1 = id animation (255 = toutes)
  SEQUENCE_RUN,        // data1 = id séquence
  TAG_CHANGED,         // tag fourni via 'name'
  HEARTBEAT,
  HOUDINI_SEND,        // name = event, payload optionnel
  LOG                  // name = message à logger
};

struct Event {
  Ev      type = Ev::NONE;
  int32_t data1 = 0;
  int32_t data2 = 0;
  char    name[24] = {0};   // tag / event Houdini / message court
};

using EventHandler = std::function<void(const Event&)>;

class EventBus {
public:
  static EventBus& get() { static EventBus instance; return instance; }

  void begin(uint16_t queueLen = 32) {
    _queue = xQueueCreate(queueLen, sizeof(Event));
    xTaskCreatePinnedToCore(_taskTrampoline, "evbus", 6144, this, 5, &_task, 1);
  }

  // Publication thread-safe (utilisable depuis n'importe quelle tâche).
  bool publish(const Event& e) {
    if (!_queue) return false;
    return xQueueSend(_queue, &e, 0) == pdTRUE;
  }

  // Helpers de confort
  bool publish(Ev type, int32_t d1 = 0, int32_t d2 = 0, const char* name = nullptr) {
    Event e; e.type = type; e.data1 = d1; e.data2 = d2;
    if (name) strncpy(e.name, name, sizeof(e.name) - 1);
    return publish(e);
  }

  void on(EventHandler h) { _handlers.push_back(std::move(h)); }

private:
  EventBus() = default;
  QueueHandle_t _queue = nullptr;
  TaskHandle_t  _task  = nullptr;
  std::vector<EventHandler> _handlers;

  static void _taskTrampoline(void* arg) { static_cast<EventBus*>(arg)->_loop(); }

  void _loop() {
    Event e;
    for (;;) {
      if (xQueueReceive(_queue, &e, portMAX_DELAY) == pdTRUE) {
        for (auto& h : _handlers) h(e);   // dispatch séquentiel
      }
    }
  }
};

// Raccourci global
inline EventBus& Bus() { return EventBus::get(); }
