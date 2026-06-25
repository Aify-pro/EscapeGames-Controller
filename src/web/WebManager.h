#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ETH.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include "core/EventBus.h"
#include "core/StateManager.h"
#include "io/RelayManager.h"
#include "io/InputManager.h"
#include "core/SequenceEngine.h"
#include "core/AnimationEngine.h"
#include "storage/ConfigStore.h"
#include "Config.h"
// ============================================================
//  WebManager — ÉTAPES 4 & 5.
//   - sert le dashboard (LittleFS, /index.html) sur le port 80
//   - WebSocket /ws : pousse l'état en TEMPS RÉEL et reçoit
//     les commandes du dashboard.
//  Le web n'est qu'un OBSERVATEUR de l'EventBus : il n'invente
//  aucun état, il reflète le StateManager. (cf. règle "une seule
//  couche possède la vérité").
// ============================================================

class WebManager {
public:
  static WebManager& get() { static WebManager w; return w; }

  void begin() {
    _ws.onEvent([this](AsyncWebSocket*, AsyncWebSocketClient* c, AwsEventType t,
                       void* arg, uint8_t* data, size_t len) {
      onWs(c, t, arg, data, len);
    });
    _server.addHandler(&_ws);
    // Racine : sert index.html si présent, sinon page de diagnostic FS.
    _server.on("/", HTTP_GET, [](AsyncWebServerRequest* r){
      if (LittleFS.exists("/index.html")) r->send(LittleFS, "/index.html", "text/html");
      else r->send(200, "text/html", fsDiag());
    });

    // --- API config (menu de configuration du dashboard) ---
    // GET : renvoie la config COMPLÈTE live (32 relais, 16 entrées, scènes, anims, réseau).
    _server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* r){
      r->send(200, "application/json", Cfg().exportJson());
    });
    // POST : reçoit la config complète, persiste sur LittleFS, recharge à chaud, diffuse.
    auto* cfgPost = new AsyncCallbackJsonWebHandler("/api/config",
      [this](AsyncWebServerRequest* req, JsonVariant& json){
        File f = LittleFS.open("/config.json", "w");
        if (!f) { req->send(500, "application/json", "{\"ok\":false}"); return; }
        serializeJsonPretty(json, f);
        f.close();
        Cfg().load();             // ré-applique noms/types/enabled/modes/scènes/anims à chaud
        broadcastSnapshot();      // tous les dashboards se resynchronisent
        Bus().publish(Ev::LOG, 0, 0, "Config mise a jour");
        req->send(200, "application/json", "{\"ok\":true}");
      });
    _server.addHandler(cfgPost);

    _server.serveStatic("/", LittleFS, "/");   // autres assets éventuels
    _server.onNotFound([](AsyncWebServerRequest* r){ r->send(404, "text/plain", "404"); });
    _server.begin();

    subscribe();

    // Nettoyage périodique des clients WS déconnectés
    xTaskCreatePinnedToCore([](void*){
      for (;;) { WebManager::get().poll(); vTaskDelay(pdMS_TO_TICKS(2000)); }
    }, "wsclean", 2048, nullptr, 1, nullptr, 1);
  }

  void poll() { _ws.cleanupClients(); }

  // Page de secours quand le FS est vide / non monté (debug sans console).
  static String fsDiag() {
    size_t total = LittleFS.totalBytes(), used = LittleFS.usedBytes();
    int n = 0; String names;
    File root = LittleFS.open("/");
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
      n++; names += "<li>"; names += f.name(); names += "</li>";
    }
    String h = "<!doctype html><meta charset=utf-8><body style='font:14px monospace;background:#0a0e14;color:#e6edf3;padding:24px'>";
    h += "<h2>Escape CTRL — FS vide</h2>";
    h += "<p>Le dashboard n'est pas sur LittleFS. Lance :</p>";
    h += "<pre style='background:#111722;padding:12px;border-radius:8px'>pio run -t uploadfs</pre>";
    h += "<p>Partition spiffs : " + String(used) + " / " + String(total) + " octets · " + String(n) + " fichier(s)</p>";
    h += n ? ("<ul>" + names + "</ul>") : "<p>(aucun fichier)</p>";
    h += "</body>";
    return h;
  }

private:
  WebManager() : _server(80), _ws("/ws") {}
  AsyncWebServer _server;
  AsyncWebSocket _ws;

  static String currentIp() {
    if (State().ethUp())  return ETH.localIP().toString();
    if (State().wifiUp()) return WiFi.localIP().toString();
    return String("0.0.0.0");
  }

  // ---------- PUSH (serveur -> clients) ----------
  void subscribe() {
    Bus().on([this](const Event& e) {
      switch (e.type) {
        case Ev::RELAY_CHANGED: pushRelay(e.data1, e.data2 != 0); break;
        case Ev::INPUT_TRIGGERED: pushInput(e.data1, true);  break;
        case Ev::INPUT_RELEASED:  pushInput(e.data1, false); break;
        case Ev::ETH_UP: case Ev::ETH_DOWN:
        case Ev::WIFI_UP: case Ev::WIFI_DOWN: pushNet(); break;
        case Ev::GAME_STARTED: case Ev::GAME_STOPPED: pushGame(); break;
        case Ev::ANIMATION_RUN:      pushAnim(e.data1, true);  break;
        case Ev::ANIMATION_FINISHED: pushAnim(e.data1, false); break;
        case Ev::HEARTBEAT: { JsonDocument h; h["t"] = "hb"; send(h); } break;
        case Ev::LOG: pushLog(e.name); break;
        default: break;
      }
    });
  }

  void pushRelay(int i, bool on) {
    JsonDocument d; d["t"] = "relay"; d["i"] = i; d["on"] = on;
    send(d);
  }
  void pushInput(int i, bool active) {
    JsonDocument d; d["t"] = "input"; d["i"] = i; d["high"] = active;
    send(d);
  }
  void pushNet() {
    JsonDocument d; d["t"] = "net";
    d["eth"] = State().ethUp(); d["wifi"] = State().wifiUp(); d["ip"] = currentIp();
    send(d);
  }
  void pushGame() {
    JsonDocument d; d["t"] = "game"; d["running"] = State().gameRunning();
    send(d);
  }
  void pushAnim(int i, bool running) {
    JsonDocument d; d["t"] = "anim"; d["i"] = i; d["running"] = running;
    send(d);
  }
  void pushLog(const char* msg) {
    JsonDocument d; d["t"] = "log"; d["msg"] = msg;
    send(d);
  }
  void send(JsonDocument& d) {
    String s; serializeJson(d, s); _ws.textAll(s);
  }

  // ---------- Snapshot complet ----------
  String snapshotJson() {
    JsonDocument d;
    d["t"] = "snapshot";
    d["fw"] = FW_VERSION;
    JsonObject net = d["net"].to<JsonObject>();
    net["eth"] = State().ethUp(); net["wifi"] = State().wifiUp(); net["ip"] = currentIp();
    d["game"] = State().gameRunning();

    JsonArray relays = d["relays"].to<JsonArray>();
    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
      auto& rc = Relays().cfg(i);
      JsonObject o = relays.add<JsonObject>();
      o["i"] = i; o["name"] = rc.name; o["on"] = State().relay(i);
      o["type"] = (int)rc.type; o["en"] = rc.enabled;
    }
    JsonArray inputs = d["inputs"].to<JsonArray>();
    for (uint8_t i = 0; i < INPUT_COUNT; i++) {
      auto& ic = Inputs().cfg(i);
      JsonObject o = inputs.add<JsonObject>();
      o["i"] = i; o["name"] = ic.name; o["high"] = State().input(i); o["en"] = ic.enabled;
    }
    JsonArray seqs = d["sequences"].to<JsonArray>();
    for (uint8_t i = 0; i < Seq().count(); i++) {
      const auto& s = Seq().list()[i];
      JsonObject o = seqs.add<JsonObject>();
      o["i"] = i; o["name"] = s.name; o["en"] = s.enabled;
    }
    JsonArray anims = d["animations"].to<JsonArray>();
    for (uint8_t i = 0; i < Anim().count(); i++) {
      const auto& a = Anim().list()[i];
      JsonObject o = anims.add<JsonObject>();
      o["i"] = i; o["name"] = a.name; o["en"] = a.enabled; o["loop"] = a.loop;
    }
    String s; serializeJson(d, s); return s;
  }

  void sendSnapshot(AsyncWebSocketClient* c) { c->text(snapshotJson()); }
  void broadcastSnapshot() { _ws.textAll(snapshotJson()); }

  // ---------- Commandes entrantes (client -> serveur) ----------
  void onWs(AsyncWebSocketClient* c, AwsEventType t, void* arg, uint8_t* data, size_t len) {
    if (t == WS_EVT_CONNECT) { sendSnapshot(c); return; }
    if (t != WS_EVT_DATA) return;

    AwsFrameInfo* info = (AwsFrameInfo*)arg;
    if (!(info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)) return;

    char buf[200];
    size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, data, n); buf[n] = 0;

    JsonDocument d;
    if (deserializeJson(d, buf)) return;
    const char* cmd = d["cmd"] | "";
    int i = d["i"] | -1;

    if      (!strcmp(cmd, "toggle") && i >= 0) Relays().toggle(i);
    else if (!strcmp(cmd, "relay")  && i >= 0) Relays().command(i, d["on"] | false);
    else if (!strcmp(cmd, "pulse")  && i >= 0) { Relays().cfg(i).type = RelayType::MOMENTARY; Relays().command(i, true); }
    else if (!strcmp(cmd, "seq")    && i >= 0) Seq().trigger(i);
    else if (!strcmp(cmd, "anim")   && i >= 0) Anim().trigger(i);
    else if (!strcmp(cmd, "animstop") && i >= 0) Anim().stop(i);
    else if (!strcmp(cmd, "animstopall"))      Anim().stopAll();
    else if (!strcmp(cmd, "game"))             State().setGameRunning(d["running"] | false);
    else if (!strcmp(cmd, "snapshot"))         sendSnapshot(c);
  }
};

inline WebManager& Web() { return WebManager::get(); }
