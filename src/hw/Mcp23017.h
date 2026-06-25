#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MCP23X17.h>
// ============================================================
//  Mcp23017 — driver basé sur Adafruit_MCP23X17.
//  Remplace l'ancien driver maison Pcf8575.h (migration HW).
//
//  Conserve la MÊME interface publique que Pcf8575 :
//    begin() / writePin() / write16() / read16() / lastOut()
//  -> RelayManager et InputManager n'ont quasiment rien à changer.
//
//  Différences MCP23017 vs PCF8575 (cf. CONTEXTE §5.11) :
//   - le MCP fonctionne par REGISTRES (IODIR/OLAT/GPIO), pas en
//     écriture 16 bits brute -> on passe par la lib Adafruit.
//   - la DIRECTION des pins est explicite : on déclare OUTPUT
//     (relais) ou INPUT_PULLUP (entrées) au begin().
//
//  Mapping port B/A (cf. CONTEXTE §5.12) : sur les modules relais,
//  le connecteur est câblé port B avant port A. Le swap logique->
//  physique est géré ICI, à la demande (banques relais uniquement),
//  pour que RelayManager raisonne en index relais simples 0..15.
// ============================================================

// Mutex partagé par TOUS les expandeurs (+ OLED plus tard) : le bus
// I2C est utilisé par plusieurs tâches FreeRTOS (relais, inputs...).
inline SemaphoreHandle_t i2cMutex() {
  static SemaphoreHandle_t m = xSemaphoreCreateMutex();
  return m;
}
struct I2cLock {
  I2cLock()  { xSemaphoreTake(i2cMutex(), portMAX_DELAY); }
  ~I2cLock() { xSemaphoreGive(i2cMutex()); }
};

class Mcp23017 {
public:
  // outputs   : true  -> 16 pins en OUTPUT (banque relais), HIGH au boot
  //             false -> 16 pins en INPUT_PULLUP (banque entrées)
  // swapPorts : true  -> applique le mapping port B/A des modules relais
  //             false -> mapping direct (entrées)
  Mcp23017(uint8_t addr, bool outputs, bool swapPorts = false)
    : _addr(addr), _outputs(outputs), _swap(swapPorts) {}

  bool begin() {
    I2cLock lock;
    _ok = _mcp.begin_I2C(_addr, &Wire);
    if (!_ok) return false;

    if (_outputs) {
      // Relais LOW-trigger : OUTPUT HIGH = relais OFF -> boot sûr.
      for (uint8_t p = 0; p < 16; p++) {
        _mcp.pinMode(p, OUTPUT);
        _mcp.digitalWrite(p, HIGH);
      }
      _out = 0xFFFF;
    } else {
      // Capteur entre la pin et GND : pull-up interne -> repos = HIGH.
      for (uint8_t p = 0; p < 16; p++)
        _mcp.pinMode(p, INPUT_PULLUP);
    }
    return true;
  }

  bool ok() const { return _ok; }

  // Écrit le mot 16 bits complet (logique, avant swap). Verrou pris.
  bool write16(uint16_t value) {
    if (!_ok) return false;
    I2cLock lock;
    _out = value;
    for (uint8_t p = 0; p < 16; p++) {
      bool high = (value >> p) & 0x01;
      _mcp.digitalWrite(map(p), high ? HIGH : LOW);
    }
    return true;
  }

  // Modifie un seul bit (sortie relais). 'pin' = index LOGIQUE 0..15.
  void writePin(uint8_t pin, bool high) {
    if (pin > 15 || !_ok) return;
    if (high) _out |= (1 << pin); else _out &= ~(1 << pin);
    I2cLock lock;
    _mcp.digitalWrite(map(pin), high ? HIGH : LOW);
  }

  // Lecture du mot 16 bits (entrées). Port A = bits 0..7, port B = 8..15.
  // Pas de swap : la banque d'entrées est câblée en direct.
  uint16_t read16() {
    if (!_ok) return _in;
    I2cLock lock;
    _in = _mcp.readGPIOAB();
    return _in;
  }

  uint16_t lastOut() const { return _out; }

private:
  uint8_t  _addr;
  bool     _outputs;
  bool     _swap;
  bool     _ok  = false;
  uint16_t _out = 0xFFFF;   // état logique des sorties (tout HIGH = tout OFF)
  uint16_t _in  = 0xFFFF;   // dernière lecture (repos = HIGH)
  Adafruit_MCP23X17 _mcp;

  // Mapping logique -> physique pour le câblage relais (port B avant port A).
  //   logique 0..7  -> pins 8..15   |   logique 8..15 -> pins 0..7
  uint8_t map(uint8_t pin) const {
    if (!_swap) return pin;
    return (pin < 8) ? pin + 8 : pin - 8;
  }
};
