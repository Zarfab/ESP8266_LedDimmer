#define USE_SERIAL false

#include "WifiCredentials.h"
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>          // SPIFFS
#include <EEPROM.h>
#include <MedianFilter.h>

#define WIFI_RETRY_INTERVAL 10000
const char* ssid = WIFI_SSID;
const char* pwd = WIFI_PASS;

#define EEPROM_SIZE 8
#define EEPROM_ADDR 0

#define ENC_A D5
#define ENC_B D6
#define BTN D7
#if USE_SERIAL
  #define LED LED_BUILTIN
#else
   #define LED D2
#endif
#define PWM_RANGE 2048
#define PWM_VAR 0.08


#define BTN_LONG_PRESS 3000 // in ms
#define BTN_SHORT_PRESS 500 // in ms

#define PARTY_WATCHDOG_TIMEOUT 1000

float pwm = 0;
int lastA;

MedianFilter buttonFilter(15, HIGH);
int lastBtn;
bool shortPressDone = false;
bool longPressDone = false;
uint32_t btnT0 = 0;
uint32_t lightOffT0 = 0;
uint32_t lightOffDur = 0;
int nbTransitions = 1;

void flashLed(int nbFlashes = 1, int dur = 200) {
  lightOffT0 = millis();
  lightOffDur = dur;
  nbTransitions = 2 * nbFlashes - 1;
}

void setLedValue(float val) {
  #if LED==LED_BUILTIN
    analogWrite(LED, PWM_RANGE-int(val));
  #else
    analogWrite(LED, int(val));
  #endif
}

AsyncWebServer server(80);

bool connectToWifi = false;
bool wifiConnected = false;
bool wifiConnecting = false;
bool mdnsStarted = false;
bool otaEnabled = false;
bool serverStarted = false;
const char * filesToCheck[] = {"/index.html", "/script.js", "/style.css"};

uint32_t lastWifiAttempt = 0;
uint32_t wifiConnectingT0 = 0;

uint8_t partyFreqHz = 5;    // 1–20 Hz
uint8_t partyPulseMs = 5;   // 1–15 ms
bool partyModeEnabled = false;
uint32_t lastKeepAlive = 0;


void handleWifi(bool force = false) {
  if (wifiConnected)
    return;

  if(wifiConnecting && millis() - wifiConnectingT0 > 500) {
    if(WiFi.status() != WL_CONNECTED) {
      #if USE_SERIAL
        Serial.print(".");
      #endif
      wifiConnectingT0 = millis();
      return;
    }
  }

  if (!wifiConnecting && (force || millis() - lastWifiAttempt > WIFI_RETRY_INTERVAL)) {
    #if USE_SERIAL
      Serial.print("Connect to Wifi : ");
      Serial.println(ssid);
    #endif
    lastWifiAttempt = millis();
    wifiConnecting = true;
    wifiConnectingT0 = millis();
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pwd);
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    wifiConnecting = false;

    #if USE_SERIAL
      Serial.print("\nConnected, IP: ");
      Serial.println(WiFi.localIP());
    #endif

    if (!serverStarted) {
      server.begin();
      serverStarted = true;
      #if USE_SERIAL
        Serial.println("Async WebServer started");
      #endif
    }
    flashLed(3, 200);
  }
}

void setupWebServer() {

  // Page principale
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/index.html", "text/html");
  });

  // Fichiers statiques
  server.serveStatic("/script.js", SPIFFS, "/script.js");
  server.serveStatic("/style.css", SPIFFS, "/style.css");
  server.serveStatic("/debug.html", SPIFFS, "/debug.html");
  server.serveStatic("/partymode.html", SPIFFS, "/partymode.html");

  // API GET PWM
  server.on("/api/pwm", HTTP_GET, [](AsyncWebServerRequest *request) {
    char buf[32];
    snprintf(buf, sizeof(buf), "{\"pwm\":%d}", (int)pwm);
    request->send(200, "application/json", buf);
  });

  // API POST PWM
  server.on("/api/pwm", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("value", true)) {
      pwm = constrain(
        request->getParam("value", true)->value().toFloat(),
        1,
        PWM_RANGE
      );
      setLedValue(pwm);
    }
    request->send(200, "text/plain", "OK");
  });
  
  // API POST SAVE
  server.on("/api/save", HTTP_POST, [](AsyncWebServerRequest *request) {
    // save pwm in EEPROM
    EEPROM.put(EEPROM_ADDR, int(pwm));
    EEPROM.commit();
    // flash twice to indicate its saved
    flashLed(2, 200);
    #if USE_SERIAL
      Serial.println("PWM value saved in EEPROM");
    #endif

    request->send(200, "text/plain", "PWM sauvegardé !");
  });

  // DEBUG
  server.on("/debug", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{";

    json += "\"heap_free\":" + String(ESP.getFreeHeap()) + ",";
    json += "\"uptime_ms\":" + String(millis()) + ",";
    json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"mac\":\"" + WiFi.macAddress() + "\",";
    json += "\"rssi_dbm\":" + String(WiFi.RSSI()) + ",";
    json += "\"cpu_freq_mhz\":" + String(ESP.getCpuFreqMHz()) + ",";
    json += "\"sdk\":\"" + String(ESP.getSdkVersion()) + "\",";
    json += "\"reset_reason\":\"" + ESP.getResetReason() + "\",";
    json += "\"pwm\":" + String(int(pwm));
    json += "}";

    request->send(200, "application/json", json);
  });

  server.on("/party", HTTP_GET, [](AsyncWebServerRequest *request) {
     if (request->hasParam("enable")) {
      partyModeEnabled = request->getParam("enable")->value().toInt() == 1;
      if(!partyModeEnabled) {
        setLedValue(pwm);
      }
      else {
        lastKeepAlive = millis();
      }
    }
    if (request->hasParam("keepalive")) {
      lastKeepAlive = millis();
    }
    if (request->hasParam("freq")) {
      partyFreqHz = constrain(
        request->getParam("freq")->value().toInt(),
        1, 20
      );
    }
    if (request->hasParam("pulse")) {
      partyPulseMs = constrain(
        request->getParam("pulse")->value().toInt(),
        1, 15
      );
    }

    #if USE_SERIAL
      Serial.printf(
        "Party: %s | freq=%d Hz | pulse=%d ms\n",
        partyModeEnabled ? "ON" : "OFF",
        partyFreqHz,
        partyPulseMs
      );
    #endif

    request->send(200, "text/plain", "OK");
  });
}

void handlePartyMode() {
  static uint32_t lastToggle = 0;
  static bool ledOn = false;

  if (!partyModeEnabled) return;
  
  if (millis() - lastKeepAlive > PARTY_WATCHDOG_TIMEOUT) {
    partyModeEnabled = false;
    setLedValue(pwm); // retour état normal
    #if USE_SERIAL
      Serial.println("Party mode watchdog timeout -> OFF");
    #endif
  }

  uint32_t periodMs = 1000 / partyFreqHz;

  if (millis() - lastToggle >= (ledOn ? partyPulseMs : periodMs - partyPulseMs)) {
    lastToggle = millis();
    ledOn = !ledOn;

    setLedValue(ledOn ? PWM_RANGE : 0);
  }
}




void setup() {
  #if USE_SERIAL
    Serial.begin(115200);
    delay(50);
    Serial.println("\n");
  #endif

  pinMode(ENC_A, INPUT);
  pinMode(ENC_B, INPUT);
  pinMode(BTN, INPUT);
  pinMode(LED, OUTPUT);

  lastA = digitalRead(ENC_A);
  lastBtn = digitalRead(BTN);
  
  EEPROM.begin(EEPROM_SIZE);
  int pwmValInit;
  EEPROM.get(EEPROM_ADDR, pwmValInit);
  pwm = constrain(float(pwmValInit), 1.0f, PWM_RANGE);
  
  analogWriteRange(PWM_RANGE);
  setLedValue(pwm);
  #if USE_SERIAL
    Serial.print("PWM read from eeprom : ");
    Serial.println(pwm);
  #endif

  bool spiffsOK = SPIFFS.begin();

  if (!spiffsOK) {
    #if USE_SERIAL
      Serial.println("SPIFFS mount FAILED");
    #endif
  } else {
    #if USE_SERIAL
      Serial.println("SPIFFS mounted");
      for(int i=0; i<3; i++) {
        Serial.print("Check file ");
        Serial.print(filesToCheck[i]);
        Serial.print(" ...... ");
        if (SPIFFS.exists(filesToCheck[i])) {
          Serial.println("found !");
        }
        else {
          Serial.println("missing !");
        }
    }
    #endif
    setupWebServer();
  }

  #if USE_SERIAL
  Serial.print("Free heap: ");
  Serial.println(ESP.getFreeHeap());
  #endif
}


void loop() {
  // Read encoder
  int A = digitalRead(ENC_A);
  if (A != lastA) {
    if (digitalRead(ENC_B) != A) {
      if(pwm < 1.0/PWM_VAR)
        pwm--;
      else 
        pwm *= (1-PWM_VAR);
    }
    else {
      if(pwm < 1.0/PWM_VAR)
        pwm++;
      else
        pwm *= (1+PWM_VAR);
    }

    pwm = constrain(pwm, 1, PWM_RANGE);
    setLedValue(pwm);
    #if USE_SERIAL
      Serial.println(pwm);
    #endif
  }
  lastA = A;

  // Read button
  buttonFilter.in(digitalRead(BTN));
  int newBtn = buttonFilter.out();
  // button pressed, start timer
  if(newBtn == LOW && lastBtn == HIGH) {
    #if USE_SERIAL
      Serial.println("button down");
    #endif
    btnT0 = millis();
  }
  // while button is pressed, update to indicate short press is done
  if(newBtn == LOW) {
    uint32_t btnPressedDuration = millis() - btnT0;
    if(btnPressedDuration > BTN_SHORT_PRESS && !shortPressDone) {
      // switch off light for 200ms
      shortPressDone = true;
      flashLed(1, 150);
      #if USE_SERIAL
        Serial.println("Short pressed done, switch off for 200ms");
      #endif
    }
    if(btnPressedDuration > BTN_LONG_PRESS && !longPressDone) {
      // switch off light for 500ms
      longPressDone = true;
      flashLed(1, 300);
      #if USE_SERIAL
        Serial.println("Long pressed done, switch off for 500ms");
      #endif
    }
  }

  // button released, calculate if it is a short or long press
  if(newBtn == HIGH && lastBtn == LOW) {
    #if USE_SERIAL
      Serial.println("button up");
    #endif
    shortPressDone = false;
    longPressDone = false;
    lightOffDur = 0;
    uint32_t btnPressedDuration = millis() - btnT0;
    // handle short press
    if(btnPressedDuration > BTN_SHORT_PRESS && btnPressedDuration < BTN_LONG_PRESS) {
      // save pwm in EEPROM
      EEPROM.put(EEPROM_ADDR, int(pwm));
      EEPROM.commit();
      // flash twice to indicate its saved
      flashLed(2, 200);
      #if USE_SERIAL
        Serial.println("PWM value saved in EEPROM");
      #endif
    }
    // handle long press
    if(btnPressedDuration > BTN_LONG_PRESS) {
      // try to connect to wifi and start server
      connectToWifi = true;
      // flash 2 times to indicate it tries to connect to wifi
      flashLed(2, 200);
      #if USE_SERIAL
        Serial.println("Try connect to Wifi");
      #endif
    }
  }
  lastBtn = newBtn;

  // handle led flashes
  if(lightOffT0 > 0) {
    float ledVal = nbTransitions%2 == 1? 0.f : pwm;
    setLedValue(ledVal);
    if(millis() - lightOffT0 > lightOffDur) {
      nbTransitions--;
      #if USE_SERIAL
        //Serial.print("end of lightOffDur, remaining transitions : ");
        //Serial.println(nbTransitions);
      #endif
      lightOffT0 = millis();
      if(nbTransitions <= 0) {
        lightOffT0 = 0;
        setLedValue(pwm);
      }
    }
  }

  handlePartyMode();

  if(connectToWifi)
    handleWifi();

  yield();
}
