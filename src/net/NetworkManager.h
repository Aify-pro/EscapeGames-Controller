#pragma once
#include <Arduino.h>
#include <ETH.h>
#include <WiFi.h>
#include "core/EventBus.h"
#include "core/StateManager.h"
#include "Config.h"
// ============================================================
//  NetworkManager — Ethernet PRIORITAIRE, Wi-Fi en SECOURS.
//  Règle d'or : jamais les deux interfaces actives ensemble.
//   - ETH UP   -> Wi-Fi OFF
//   - ETH DOWN -> Wi-Fi ON (même IP statique)
//   - ETH back -> Wi-Fi OFF
// ============================================================

struct NetCfg {
  IPAddress ip{192,168,3,50};
  IPAddress gw{192,168,3,1};
  IPAddress mask{255,255,255,0};
  IPAddress dns{192,168,3,1};
  char ssid[33] = "";
  char pass[65] = "";
};

class NetworkManager {
public:
  static NetworkManager& get() { static NetworkManager n; return n; }

  NetCfg& cfg() { return _cfg; }

  void begin() {
    // arduino-esp32 2.0.x : le callback doit prendre 2 args (event, info).
    WiFi.onEvent([this](WiFiEvent_t ev, WiFiEventInfo_t info){ (void)info; onEvent(ev); });
    // Ordre des args en 2.0.x : (addr, power, mdc, mdio, type, clk_mode).
    ETH.begin(ETH_PHY_ADDR, ETH_PHY_POWER, ETH_PHY_MDC, ETH_PHY_MDIO,
              ETH_PHY_TYPE, ETH_CLK_MODE);
    ETH.config(_cfg.ip, _cfg.gw, _cfg.mask, _cfg.dns);
    // Le Wi-Fi reste éteint tant qu'on n'a pas confirmé la perte ETH.
    WiFi.mode(WIFI_OFF);
  }

private:
  NetworkManager() = default;
  NetCfg _cfg;
  bool   _ethUp = false;

  void startWifiBackup() {
    if (_cfg.ssid[0] == '\0') return;          // pas de secours configuré
    WiFi.mode(WIFI_STA);
    WiFi.config(_cfg.ip, _cfg.gw, _cfg.mask, _cfg.dns); // même IP
    WiFi.begin(_cfg.ssid, _cfg.pass);
    Bus().publish(Ev::LOG, 0, 0, "WiFi backup ON");
  }

  void stopWifi() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  void onEvent(WiFiEvent_t ev) {
    switch (ev) {
      case ARDUINO_EVENT_ETH_GOT_IP:
        _ethUp = true;
        stopWifi();                            // ETH prioritaire
        State().setLink(true, false);
        Bus().publish(Ev::ETH_UP);
        break;

      case ARDUINO_EVENT_ETH_DISCONNECTED:
      case ARDUINO_EVENT_ETH_STOP:
        if (_ethUp) {
          _ethUp = false;
          Bus().publish(Ev::ETH_DOWN);
          startWifiBackup();                   // bascule secours
        }
        break;

      case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        State().setLink(false, true);
        Bus().publish(Ev::WIFI_UP);
        break;

      case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        if (!_ethUp) {
          State().setLink(false, false);
          Bus().publish(Ev::WIFI_DOWN);
          WiFi.reconnect();
        }
        break;

      default: break;
    }
  }
};

inline NetworkManager& Net() { return NetworkManager::get(); }
