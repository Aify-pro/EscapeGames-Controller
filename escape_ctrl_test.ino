#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_MCP23X17.h>
#include <WiFi.h>
#include <ETH.h>

#define ETH_PHY_TYPE  ETH_PHY_LAN8720
#define ETH_PHY_ADDR  1
#define ETH_PHY_MDC   23
#define ETH_PHY_MDIO  18
#define ETH_PHY_POWER 16
#define ETH_CLK_MODE  ETH_CLOCK_GPIO0_IN

#define I2C_SDA 33
#define I2C_SCL 32
#define OLED_ADDR 0x3C

#define JOY_VRX 36
#define JOY_VRY 39
// Bouton désactivé pour l'instant — bascule automatique

Adafruit_SSD1306 display(128, 64, &Wire, -1);
Adafruit_MCP23X17 mcpRelay0;
Adafruit_MCP23X17 mcpRelay1;
Adafruit_MCP23X17 mcpInputs;

const uint8_t MCP_ADDR_RELAY0 = 0x27;
const uint8_t MCP_ADDR_RELAY1 = 0x26;
const uint8_t MCP_ADDR_INPUTS = 0x25;

bool mcpRelay0Ok = false;
bool mcpRelay1Ok = false;
bool mcpInputsOk = false;

bool     ethConnected    = false;
uint8_t  currentRelay    = 0;
uint32_t lastRelayChange = 0;
uint32_t lastDisplay     = 0;
uint32_t lastModeSwitch  = 0;
uint8_t  dispMode        = 0;   // 0=relais 1=inputs
#define  MODE_SWITCH_MS  5000   // bascule auto toutes les 5s

void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_GOT_IP:      ethConnected = true;  break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
    case ARDUINO_EVENT_ETH_STOP:        ethConnected = false; break;
    default: break;
  }
}

void allRelaysOff() {
  if (mcpRelay0Ok) for (uint8_t i = 0; i < 16; i++) mcpRelay0.digitalWrite(i, HIGH);
  if (mcpRelay1Ok) for (uint8_t i = 0; i < 16; i++) mcpRelay1.digitalWrite(i, HIGH);
}

void setRelay(uint8_t idx, bool on) {
  uint8_t level = on ? LOW : HIGH;
  uint8_t localIdx  = idx < 16 ? idx : idx - 16;
  uint8_t mappedPin = localIdx < 8 ? localIdx + 8 : localIdx - 8;
  if (idx < 16  && mcpRelay0Ok) mcpRelay0.digitalWrite(mappedPin, level);
  if (idx >= 16 && mcpRelay1Ok) mcpRelay1.digitalWrite(mappedPin, level);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Wire.begin(I2C_SDA, I2C_SCL, 400000);
  delay(100);

  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Init MCP23017...");
  display.display();

  mcpRelay0Ok = mcpRelay0.begin_I2C(MCP_ADDR_RELAY0);
  mcpRelay1Ok = mcpRelay1.begin_I2C(MCP_ADDR_RELAY1);
  mcpInputsOk = mcpInputs.begin_I2C(MCP_ADDR_INPUTS);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.printf("R0 0x%02X: %s\n", MCP_ADDR_RELAY0, mcpRelay0Ok ? "OK" : "FAIL");
  display.printf("R1 0x%02X: %s\n", MCP_ADDR_RELAY1, mcpRelay1Ok ? "OK" : "FAIL");
  display.printf("IN 0x%02X: %s\n", MCP_ADDR_INPUTS, mcpInputsOk ? "OK" : "FAIL");
  display.println("");
  display.println("Debut dans 2s...");
  display.display();
  delay(2000);

  for (uint8_t i = 0; i < 16; i++) {
    if (mcpRelay0Ok) { mcpRelay0.pinMode(i, OUTPUT); mcpRelay0.digitalWrite(i, HIGH); }
    if (mcpRelay1Ok) { mcpRelay1.pinMode(i, OUTPUT); mcpRelay1.digitalWrite(i, HIGH); }
    if (mcpInputsOk)   mcpInputs.pinMode(i, INPUT_PULLUP);
  }

  WiFi.onEvent(WiFiEvent);
  ETH.begin();

  lastRelayChange = millis();
  lastModeSwitch  = millis();
}

void loop() {
  uint32_t now = millis();

  int vrx = analogRead(JOY_VRX);
  int vry = analogRead(JOY_VRY);

  // Bascule automatique entre mode relais et mode inputs toutes les 5s
  if (now - lastModeSwitch >= MODE_SWITCH_MS) {
    lastModeSwitch = now;
    dispMode = (dispMode + 1) % 2;
    // En mode inputs : stoppe les relais pour éviter le bruit
    if (dispMode == 1) allRelaysOff();
    Serial.printf("Mode -> %s\n", dispMode == 0 ? "RELAIS" : "INPUTS");
  }

  // Séquence relais — seulement en mode 0
  if (dispMode == 0 && now - lastRelayChange >= 500) {
    allRelaysOff();
    delay(20);
    setRelay(currentRelay, true);
    Serial.printf("R%02d ON\n", currentRelay + 1);
    currentRelay = (currentRelay + 1) % 32;
    lastRelayChange = now;
  }

  // Affichage OLED toutes les 200ms
  if (now - lastDisplay >= 200) {
    lastDisplay = now;
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);

    // Compte à rebours avant bascule
    uint32_t remaining = (MODE_SWITCH_MS - (now - lastModeSwitch)) / 1000 + 1;

    if (dispMode == 0) {
      // --- Mode relais ---
      display.printf("MODE RELAIS [%lus]\n", remaining);
      uint8_t shown = (currentRelay + 31) % 32;
      display.setTextSize(2);
      char buf[8];
      snprintf(buf, sizeof(buf), "R%02d", shown + 1);
      display.println(buf);
      display.setTextSize(1);
      display.printf("Bank%d 0x%02X\n",
        shown < 16 ? 0 : 1,
        shown < 16 ? MCP_ADDR_RELAY0 : MCP_ADDR_RELAY1);
      display.printf("R0:%s R1:%s\n",
        mcpRelay0Ok ? "OK" : "--",
        mcpRelay1Ok ? "OK" : "--");

    } else {
      // --- Mode inputs ---
      display.printf("MODE INPUTS [%lus]\n", remaining);
      display.println("Court-circ pin->GND");
      display.println("pour activer:");
      if (mcpInputsOk) {
        // Affiche 16 inputs sur 2 lignes
        // Pull-up : repos=HIGH(0), actif=LOW(1)
        char row1[18] = " 1-8: ";
        char row2[18] = "9-16: ";
        for (uint8_t i = 0; i < 8; i++)
          row1[6 + i] = mcpInputs.digitalRead(i) ? '-' : '1';
        for (uint8_t i = 0; i < 8; i++)
          row2[6 + i] = mcpInputs.digitalRead(i + 8) ? '-' : '1';
        row1[14] = 0; row2[14] = 0;
        display.println(row1);
        display.println(row2);
        display.printf("Joy X:%-4d Y:%-4d\n", vrx, vry);
      } else {
        display.println("MCP Inputs FAIL");
      }
    }

    display.display();
  }
}
