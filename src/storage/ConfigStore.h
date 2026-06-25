#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "net/NetworkManager.h"
#include "io/RelayManager.h"
#include "io/InputManager.h"
#include "core/AutomationEngine.h"
#include "core/SequenceEngine.h"
#include "core/AnimationEngine.h"
#include "core/CatalogLock.h"
#include "Config.h"
// ============================================================
//  ConfigStore — persistance JSON sur LittleFS.
//  'config_version' présent dès le départ pour gérer les
//  migrations lors des futures OTA (cf. brief).
// ============================================================

static constexpr int CONFIG_VERSION = 1;
static const char*    CONFIG_PATH    = "/config.json";

class ConfigStore {
public:
  static ConfigStore& get() { static ConfigStore c; return c; }

  bool begin() {
    if (!LittleFS.begin(true)) {            // format si vierge
      Serial.println("[cfg] LittleFS mount FAIL");
      return false;
    }
    if (!LittleFS.exists(CONFIG_PATH)) {
      Serial.println("[cfg] no config -> defaults");
      return save();                        // crée le fichier par défaut
    }
    return load();
  }

  bool load() {
    File f = LittleFS.open(CONFIG_PATH, "r");
    if (!f) return false;
    JsonDocument doc;
    if (deserializeJson(doc, f)) { f.close(); return false; }
    f.close();

    // --- migration éventuelle ---
    int v = doc["config_version"] | 1;
    if (v < CONFIG_VERSION) migrate(doc, v);

    // --- réseau ---
    auto& nc = Net().cfg();
    if (doc["net"]["ip"].is<const char*>())   nc.ip.fromString(doc["net"]["ip"].as<const char*>());
    if (doc["net"]["gw"].is<const char*>())   nc.gw.fromString(doc["net"]["gw"].as<const char*>());
    if (doc["net"]["mask"].is<const char*>()) nc.mask.fromString(doc["net"]["mask"].as<const char*>());
    if (doc["net"]["dns"].is<const char*>())  nc.dns.fromString(doc["net"]["dns"].as<const char*>());
    strlcpy(nc.ssid, doc["net"]["ssid"] | "", sizeof(nc.ssid));
    strlcpy(nc.pass, doc["net"]["pass"] | "", sizeof(nc.pass));

    // --- relais ---
    JsonArray relays = doc["relays"].as<JsonArray>();
    uint8_t i = 0;
    for (JsonObject r : relays) {
      if (i >= RELAY_COUNT) break;
      auto& rc = Relays().cfg(i);
      strlcpy(rc.name, r["name"] | "Relay", sizeof(rc.name));
      rc.type = (RelayType)(r["type"] | 0);
      rc.defaultState = r["default_state"] | false;
      rc.durationMs   = r["duration"] | 3000;
      rc.enabled      = r["enabled"] | true;
      i++;
    }

    // --- inputs ---
    JsonArray inputs = doc["inputs"].as<JsonArray>();
    i = 0;
    for (JsonObject in : inputs) {
      if (i >= INPUT_COUNT) break;
      auto& ic = Inputs().cfg(i);
      strlcpy(ic.name, in["name"] | "Input", sizeof(ic.name));
      ic.mode       = (InputMode)(in["mode"] | 0);
      ic.debounceMs = in["debounce"] | 50;
      ic.enabled    = in["enabled"] | true;
      i++;
    }

    // --- catalogues dynamiques : verrou pour un reload À CHAUD sûr ---
    // (lecteurs concurrents : tâche anim + dispatch bus pour autos/scènes)
    {
      CatalogLock lk;

      // automations (input -> action relais)
      Autos().clear();
      for (JsonObject a : doc["automations"].as<JsonArray>()) {
        Automation au;
        au.inputIndex = a["input"] | 0;
        au.action     = (ActionKind)(a["action"] | 0);
        au.relayIndex = a["relay"] | 0;
        Autos().add(au);
      }

      // séquences / scènes multi-relais (Étape 6)
      Seq().clear();
      for (JsonObject s : doc["sequences"].as<JsonArray>()) {
        Sequence seq;
        strlcpy(seq.name, s["name"] | "Sequence", sizeof(seq.name));
        seq.enabled = s["enabled"] | true;
        for (JsonObject st : s["steps"].as<JsonArray>()) {
          SeqStep step;
          step.relayIndex = st["relay"]  | 0;
          step.action     = (ActionKind)(st["action"] | 0);
          seq.steps.push_back(step);
        }
        Seq().add(seq);
      }

      // animations / timelines temporisées (Étape 7)
      Anim().clear();
      for (JsonObject an : doc["animations"].as<JsonArray>()) {
        Animation a;
        strlcpy(a.name, an["name"] | "Animation", sizeof(a.name));
        a.enabled  = an["enabled"] | true;
        a.loop     = an["loop"]    | false;
        a.periodMs = an["period"]  | 1000;
        for (JsonObject st : an["steps"].as<JsonArray>()) {
          AnimStep step;
          step.atMs       = st["at"]    | 0;
          step.relayIndex = st["relay"] | 0;
          step.action     = (ActionKind)(st["action"] | 0);
          a.steps.push_back(step);
        }
        Anim().add(a);
      }
    }
    Serial.printf("[cfg] loaded (v%d)\n", v);
    return true;
  }

  bool save() {
    JsonDocument doc;
    buildDoc(doc);
    File f = LittleFS.open(CONFIG_PATH, "w");
    if (!f) return false;
    serializeJsonPretty(doc, f);
    f.close();
    Serial.println("[cfg] saved");
    return true;
  }

  // Sérialise la config COMPLÈTE live (32 relais, 16 entrées, scènes,
  // animations, réseau) en String -> servie par l'API GET /api/config.
  String exportJson() {
    JsonDocument doc;
    buildDoc(doc);
    String s;
    serializeJsonPretty(doc, s);
    return s;
  }

private:
  // Remplit 'doc' à partir de l'état live (source unique de save/export).
  void buildDoc(JsonDocument& doc) {
    doc["config_version"] = CONFIG_VERSION;
    doc["fw_version"]     = FW_VERSION;

    auto& nc = Net().cfg();
    doc["net"]["ip"]   = nc.ip.toString();
    doc["net"]["gw"]   = nc.gw.toString();
    doc["net"]["mask"] = nc.mask.toString();
    doc["net"]["dns"]  = nc.dns.toString();
    doc["net"]["ssid"] = nc.ssid;
    doc["net"]["pass"] = nc.pass;

    JsonArray relays = doc["relays"].to<JsonArray>();
    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
      auto& rc = Relays().cfg(i);
      JsonObject r = relays.add<JsonObject>();
      r["id"]            = i + 1;
      r["name"]          = rc.name;
      r["type"]          = (int)rc.type;
      r["default_state"] = rc.defaultState;
      r["duration"]      = rc.durationMs;
      r["enabled"]       = rc.enabled;
    }

    JsonArray inputs = doc["inputs"].to<JsonArray>();
    for (uint8_t i = 0; i < INPUT_COUNT; i++) {
      auto& ic = Inputs().cfg(i);
      JsonObject in = inputs.add<JsonObject>();
      in["id"]       = i + 1;
      in["name"]     = ic.name;
      in["mode"]     = (int)ic.mode;
      in["debounce"] = ic.debounceMs;
      in["enabled"]  = ic.enabled;
    }

    JsonArray autos = doc["automations"].to<JsonArray>();
    for (auto& a : Autos().rules()) {
      JsonObject o = autos.add<JsonObject>();
      o["input"]  = a.inputIndex;
      o["action"] = (int)a.action;
      o["relay"]  = a.relayIndex;
    }

    JsonArray seqs = doc["sequences"].to<JsonArray>();
    uint8_t si = 0;
    for (auto& s : Seq().list()) {
      JsonObject o = seqs.add<JsonObject>();
      o["id"]      = si + 1;
      o["name"]    = s.name;
      o["enabled"] = s.enabled;
      JsonArray steps = o["steps"].to<JsonArray>();
      for (auto& st : s.steps) {
        JsonObject so = steps.add<JsonObject>();
        so["relay"]  = st.relayIndex;
        so["action"] = (int)st.action;
      }
      si++;
    }

    JsonArray anims = doc["animations"].to<JsonArray>();
    uint8_t ai = 0;
    for (auto& a : Anim().list()) {
      JsonObject o = anims.add<JsonObject>();
      o["id"]      = ai + 1;
      o["name"]    = a.name;
      o["enabled"] = a.enabled;
      o["loop"]    = a.loop;
      o["period"]  = a.periodMs;
      JsonArray steps = o["steps"].to<JsonArray>();
      for (auto& st : a.steps) {
        JsonObject so = steps.add<JsonObject>();
        so["at"]     = st.atMs;
        so["relay"]  = st.relayIndex;
        so["action"] = (int)st.action;
      }
      ai++;
    }
  }

  ConfigStore() = default;

  // Place pour les futures migrations de schéma (v1->v2...).
  void migrate(JsonDocument& /*doc*/, int /*fromVersion*/) {
    Serial.println("[cfg] migration appliquée");
  }
};

inline ConfigStore& Cfg() { return ConfigStore::get(); }
