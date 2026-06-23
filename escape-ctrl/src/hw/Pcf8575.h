#pragma once
#include <Arduino.h>
#include <Wire.h>
// ============================================================
//  Pcf8575 — driver minimal et autonome (pas de lib externe).
//  16 bits quasi-bidirectionnels. Au power-up : toutes lignes
//  à HIGH (donc relais LOW-trigger = OFF -> boot sûr).
// ============================================================

// Mutex partagé par TOUS les PCF8575 (+ OLED plus tard) : le bus I2C
// est utilisé par plusieurs tâches FreeRTOS (relais, inputs...).
inline SemaphoreHandle_t i2cMutex() {
  static SemaphoreHandle_t m = xSemaphoreCreateMutex();
  return m;
}
struct I2cLock {
  I2cLock()  { xSemaphoreTake(i2cMutex(), portMAX_DELAY); }
  ~I2cLock() { xSemaphoreGive(i2cMutex()); }
};

class Pcf8575 {
public:
  explicit Pcf8575(uint8_t addr) : _addr(addr) {}

  bool begin() {
    I2cLock lock;
    Wire.beginTransmission(_addr);
    bool ok = (Wire.endTransmission() == 0);
    if (ok) writeRaw(_out);   // applique l'état par défaut (tout HIGH)
    return ok;
  }

  // Écrit le mot 16 bits complet (verrou pris)
  bool write16(uint16_t value) {
    I2cLock lock;
    return writeRaw(value);
  }

  // Modifie un seul bit (pour les sorties relais)
  void writePin(uint8_t pin, bool high) {
    if (pin > 15) return;
    if (high) _out |= (1 << pin); else _out &= ~(1 << pin);
    write16(_out);
  }

  // Lecture du mot 16 bits (pour les entrées). Les pins à lire
  // doivent rester à HIGH (état "input" du PCF8575, c'est le défaut).
  uint16_t read16() {
    I2cLock lock;
    if (Wire.requestFrom((int)_addr, 2) != 2) return _in;
    uint8_t lo = Wire.read();
    uint8_t hi = Wire.read();
    _in = (hi << 8) | lo;
    return _in;
  }

  uint16_t lastOut() const { return _out; }

private:
  uint8_t  _addr;
  uint16_t _out = 0xFFFF;   // power-up = tout HIGH
  uint16_t _in  = 0xFFFF;

  // Écriture brute SANS verrou (appelée par les méthodes déjà verrouillées).
  bool writeRaw(uint16_t value) {
    _out = value;
    Wire.beginTransmission(_addr);
    Wire.write(value & 0xFF);
    Wire.write((value >> 8) & 0xFF);
    return Wire.endTransmission() == 0;
  }
};
