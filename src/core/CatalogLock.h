#pragma once
#include <Arduino.h>
// ============================================================
//  CatalogLock — mutex protégeant les "catalogues" dynamiques
//  (automations, scènes, animations : des std::vector) pendant
//  un rechargement de config À CHAUD.
//
//  Sans lui : POST /api/config -> Cfg().load() reconstruit ces
//  vecteurs (clear + push_back -> réallocation) DANS la tâche web,
//  pendant que la tâche d'animation et le dispatch du bus les
//  parcourent -> use-after-free / crash. Pris brièvement côté
//  lecteurs (run/onInput/tick) et côté écriture (reload).
//
//  Ordre de verrou : CatalogLock peut être pris AVANT le mutex
//  I²C (jamais l'inverse) -> pas d'inter-blocage.
// ============================================================
inline SemaphoreHandle_t catalogMutex() {
  static SemaphoreHandle_t m = xSemaphoreCreateMutex();
  return m;
}
struct CatalogLock {
  CatalogLock()  { xSemaphoreTake(catalogMutex(), portMAX_DELAY); }
  ~CatalogLock() { xSemaphoreGive(catalogMutex()); }
};
