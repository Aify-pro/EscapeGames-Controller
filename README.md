# Escape Game Central Controller — WT32-ETH01

Contrôleur central d'automation pour escape game. Architecture **event-driven** :
une seule couche détient l'état (`StateManager`), tout le reste écoute l'`EventBus`.
Aucun module ne pilote un relais directement — l'input émet un événement,
l'automation décide, le `RelayManager` exécute.

> **Migration HW faite** : expandeurs **MCP23017** (driver `Adafruit_MCP23X17`),
> I²C sur **GPIO33/32**, adresses **0x27/0x26/0x25**. L'ancien driver PCF8575 maison
> est retiré. Cf. `2_CONTEXTE_TECHNIQUE.md` (connaissances projet).

## État actuel — ÉTAPES 1 → 6

```
CORE
├── EventBus         ✅  bus d'événements (1 tâche de dispatch)
├── StateManager     ✅  vérité unique du système (relais, inputs, tags, jeu)
├── RelayManager     ✅  32 relais (2x MCP23017), ON/OFF + impulsion + fail-safe
├── InputManager     ✅  16 entrées (MCP23017 0x25), debounce + NO/NC   [Étape 2]
├── AutomationEngine ✅  EVENT -> ACTION (input -> relais)              [Étape 3]
├── NetworkManager   ✅  Ethernet prioritaire / WiFi secours
├── ConfigStore      ✅  LittleFS + JSON + config_version
├── WebManager       ✅  serveur + WebSocket temps réel                 [Étape 4]
├── Dashboard web    ✅  data/index.html, relais + inputs + scènes live  [Étape 5]
├── SequenceEngine   ✅  scènes instantanées multi-relais             [Étape 6]
├── AnimationEngine  ⬜  Étape 7
├── OLED UI          ⬜  Étape 8
├── HoudiniConnector ⬜  Étape 9
└── OTA + Watchdog   ⬜  Étape 10 (partitions déjà prêtes ✅)
```

## Câblage (voir src/Config.h)

| Fonction | GPIO |
|---|---|
| I²C SDA / SCL | IO33 / IO32 |
| Joystick X / Y / SW | IO36 / IO39 / IO35 |
| MCP23017 relais 1-16 / 17-32 | I²C 0x27 / 0x26 |
| MCP23017 entrées 1-16 | I²C 0x25 |
| OLED SSD1306 | I²C 0x3C |

> ⚠️ I²C **uniquement** sur GPIO33/32 avec Ethernet actif (contrainte WT32-ETH01).
> ⚠️ Bouton joystick sur **IO35** (input-only, **pull-up externe requis**). IO12 banni (strapping flash).
> ⚠️ Modules relais : jumper **JD-VCC/VCC retiré**, optocoupleurs en 3.3V.
> Câblage connecteur **port B avant port A** → mapping géré dans `Mcp23017` (swapPorts=true).
> Capteurs entre la pin et GND (pull-up interne MCP23017, INPUT_PULLUP).

## Build & flash

```bash
pio run                 # compile (télécharge AsyncTCP + ESPAsyncWebServer + Adafruit MCP/SSD1306/GFX)
pio run -t upload       # flash firmware (USB la 1ère fois)
pio run -t uploadfs     # flash data/ -> LittleFS (IMPORTANT : envoie index.html + config.json)
pio device monitor      # console série 115200 (optionnel)
```

> Le `uploadfs` est obligatoire : sans lui, LittleFS est vide et le dashboard
> renvoie 404. À refaire à chaque modif de `data/`.
> Reflasher le **firmware avant** le FS si la table de partition change.

## Utilisation

1. Branche l'Ethernet, ouvre `http://192.168.1.50` dans un navigateur du LAN.
2. Le dashboard se connecte en WebSocket et affiche l'état en direct.
3. Clique une tuile relais pour la basculer (impulsion auto pour les relais momentary).
4. Le point « Battement » pulse tant que la carte répond.

## Protocole WebSocket (/ws)

Client → serveur : `{"cmd":"toggle","i":4}` · `{"cmd":"relay","i":4,"on":true}` ·
`{"cmd":"pulse","i":2}` · `{"cmd":"seq","i":0}` · `{"cmd":"game","running":true}` · `{"cmd":"snapshot"}`

Serveur → client : `snapshot` (état complet, dont la liste des scènes) puis les deltas
`relay` / `input` / `net` / `game` / `hb` / `log`.

## Scènes (Étape 6)

Une scène = un état **instantané** multi-relais (« Reset », « Ouverture », « Nettoyage »).
Définies dans `config.json` (`sequences[]`), déclenchées via le bus (`SEQUENCE_RUN`) :
dashboard (boutons « Scènes »), console série (`q<n>`), et à venir le joystick.

```json
"sequences": [
  { "id":1, "name":"Reset", "enabled":true,
    "steps":[ {"relay":0,"action":2}, {"relay":2,"action":1} ] }
]
```
`action` : 0=TOGGLE, 1=ON, 2=OFF, 3=PULSE. Tous les pas sont appliqués d'un bloc,
via le `RelayManager` (jamais de pilotage direct). Les timelines temporisées = Étape 7.

## Prochaine étape

**Étape 7 : AnimationEngine** — timelines NON bloquantes (pas échelonnés dans le temps,
ex. clignotements, ouvertures séquencées) au-dessus du même bus.
