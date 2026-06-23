# Escape Game Central Controller — WT32-ETH01

Contrôleur central d'automation pour escape game. Architecture **event-driven** :
une seule couche détient l'état (`StateManager`), tout le reste écoute l'`EventBus`.
Aucun module ne pilote un relais directement — l'input émet un événement,
l'automation décide, le `RelayManager` exécute.

## État actuel — ÉTAPES 1 → 5

```
CORE
├── EventBus         ✅  bus d'événements (1 tâche de dispatch)
├── StateManager     ✅  vérité unique du système (relais, inputs, tags, jeu)
├── RelayManager     ✅  32 relais (2x PCF8575), ON/OFF + impulsion + fail-safe
├── InputManager     ✅  16 entrées (PCF8575 0x22), debounce + NO/NC   [Étape 2]
├── AutomationEngine ✅  EVENT -> ACTION (input -> relais)             [Étape 3]
├── NetworkManager   ✅  Ethernet prioritaire / WiFi secours
├── ConfigStore      ✅  LittleFS + JSON + config_version
├── WebManager       ✅  serveur + WebSocket temps réel                [Étape 4]
├── Dashboard web    ✅  data/index.html, tuiles relais + inputs live  [Étape 5]
├── SequenceEngine   ⬜  Étape 6
├── AnimationEngine  ⬜  Étape 7
├── OLED UI          ⬜  Étape 8
├── HoudiniConnector ⬜  Étape 9
└── OTA + Watchdog   ⬜  Étape 10 (partitions déjà prêtes ✅)
```

## Câblage (voir src/Config.h)

| Fonction | GPIO |
|---|---|
| I²C SDA / SCL | IO14 / IO15 |
| Joystick X / Y / SW | IO35 / IO39 / IO5 |
| PCF8575 relais 1-16 / 17-32 | I²C 0x20 / 0x21 |
| PCF8575 entrées 1-16 | I²C 0x22 |
| OLED SSD1306 | I²C 0x3C |

> ⚠️ Jamais IO12 (strapping flash 1.8 V → boot fail).
> Les capteurs se câblent entre la pin et GND (pull-up faible interne du PCF8575).

## Build & flash

```bash
pio run                 # compile (télécharge AsyncTCP + ESPAsyncWebServer)
pio run -t upload       # flash firmware (USB la 1ère fois)
pio run -t uploadfs     # flash data/ -> LittleFS (IMPORTANT : envoie index.html + config.json)
pio device monitor      # console série 115200 (optionnel)
```

> Le `uploadfs` est obligatoire : sans lui, LittleFS est vide et le dashboard
> renvoie 404. À refaire à chaque modif de `data/`.

## Utilisation

1. Branche l'Ethernet, ouvre `http://192.168.1.50` dans un navigateur du LAN.
2. Le dashboard se connecte en WebSocket et affiche l'état en direct.
3. Clique une tuile relais pour la basculer (impulsion auto pour les relais momentary).
4. Le point « Battement » pulse tant que la carte répond.

## Protocole WebSocket (/ws)

Client → serveur : `{"cmd":"toggle","i":4}` · `{"cmd":"relay","i":4,"on":true}` ·
`{"cmd":"pulse","i":2}` · `{"cmd":"game","running":true}` · `{"cmd":"snapshot"}`

Serveur → client : `snapshot` (état complet à la connexion) puis les deltas
`relay` / `input` / `net` / `game` / `hb` / `log`.

## Prochaine étape

**Étape 6 : SequenceEngine** — états instantanés multi-relais (« Nettoyage »,
« Reset », « Ouverture ») déclenchables depuis le dashboard et le joystick.
