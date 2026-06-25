#include <Arduino.h>
#include <Wire.h>
#include "Config.h"
#include "core/EventBus.h"
#include "core/StateManager.h"
#include "core/AutomationEngine.h"
#include "core/SequenceEngine.h"
#include "net/NetworkManager.h"
#include "io/RelayManager.h"
#include "io/InputManager.h"
#include "web/WebManager.h"
#include "storage/ConfigStore.h"
// ============================================================
//  Escape Game Central Controller — main
//  ÉTAPES 1-6 : coeur événementiel + relais + inputs + réseau
//               + config + web temps réel + scènes + OTA-ready
//  HW : MCP23017 (I2C 33/32, adresses 0x27/0x26/0x25).
//
//  Console série de test (115200 baud) :
//    r<n>      -> toggle relais n   (ex: r5)
//    p<n>      -> impulsion relais n
//    q<n>      -> déclenche la scène n (ex: q1 = 1ère séquence)
//    s         -> dump états
//    save      -> sauvegarde config
// ============================================================

// --- Logger : écoute les événements LOG et trace en série ---
static void setupLogger() {
  Bus().on([](const Event& e) {
    if (e.type == Ev::LOG)
      Serial.printf("[%lu] %s\n", millis(), e.name);
    else if (e.type == Ev::RELAY_CHANGED)
      Serial.printf("[%lu] RELAY %d -> %s\n", millis(), e.data1 + 1, e.data2 ? "ON" : "OFF");
    else if (e.type == Ev::ETH_UP)
      Serial.printf("[%lu] ETH UP  %s\n", millis(), ETH.localIP().toString().c_str());
    else if (e.type == Ev::ETH_DOWN)
      Serial.printf("[%lu] ETH DOWN -> bascule WiFi\n", millis());
    else if (e.type == Ev::WIFI_UP)
      Serial.printf("[%lu] WIFI UP %s\n", millis(), WiFi.localIP().toString().c_str());
  });
}

// --- Heartbeat : preuve de vie + base watchdog réseau ---
static void heartbeatTask(void*) {
  for (;;) {
    Bus().publish(Ev::HEARTBEAT);
    Serial.printf("[hb] up=%lus  heap=%u  eth=%d wifi=%d game=%d\n",
                  millis() / 1000, ESP.getFreeHeap(),
                  State().ethUp(), State().wifiUp(), State().gameRunning());
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

// --- Console série de test (pratique avant le dashboard web) ---
static void consoleTask(void*) {
  String line;
  for (;;) {
    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        line.trim();
        if (line.startsWith("r")) {
          int n = line.substring(1).toInt() - 1;
          Relays().toggle(n);
        } else if (line.startsWith("p")) {
          int n = line.substring(1).toInt() - 1;
          Relays().cfg(n).type = RelayType::MOMENTARY;
          Relays().command(n, true);
        } else if (line.startsWith("q")) {
          int n = line.substring(1).toInt() - 1;   // 1-based comme r/p
          Seq().trigger(n);
        } else if (line == "s") {
          for (uint8_t i = 0; i < RELAY_COUNT; i++)
            if (State().relay(i)) Serial.printf("  R%d ON (%s)\n", i + 1, Relays().cfg(i).name);
        } else if (line == "save") {
          Cfg().save();
        }
        line = "";
      } else line += c;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\n=== Escape CTRL  fw %s ===\n", FW_VERSION);

  // Bus I2C (OLED + 3x MCP23017). Pins 33/32 obligatoires avec ETH actif.
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);

  // File d'événements à 64 : une scène multi-relais publie en rafale
  // jusqu'à 32 RELAY_CHANGED d'un coup -> marge pour ne rien perdre.
  Bus().begin(64);       // 1. coeur événementiel d'abord
  setupLogger();          // 2. logs
  Cfg().begin();          // 3. charge config (réseau + relais + inputs + automations + scènes)
  Net().begin();          // 4. Ethernet prioritaire / WiFi secours
  Relays().begin();       // 5. MCP23017 sorties (0x27/0x26) + états sûrs au boot
  Autos().begin();        // 6. moteur event->action (s'abonne au bus)
  Seq().begin();          // 7. moteur de scènes (écoute SEQUENCE_RUN)
  Inputs().begin();       // 8. lecture des 16 entrées (MCP23017 0x25)
  Web().begin();          // 9. serveur web + WebSocket temps réel

  xTaskCreatePinnedToCore(heartbeatTask, "hb",   3072, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(consoleTask,   "cons", 3072, nullptr, 2, nullptr, 1);

  Bus().publish(Ev::LOG, 0, 0, "Boot OK");
}

void loop() {
  // Tout tourne sous FreeRTOS (cf. brief). loop() reste vide.
  vTaskDelay(pdMS_TO_TICKS(1000));
}
