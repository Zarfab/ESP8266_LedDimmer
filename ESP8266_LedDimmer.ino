#include <EEPROM.h>
#include <MedianFilter.h>

#define EEPROM_SIZE 8
#define EEPROM_ADDR 0

#define ENC_A D5
#define ENC_B D6
#define BTN D7
#define LED LED_BUILTIN //D2
#define PWM_RANGE 2048
#define PWM_VAR 0.08

#define USE_SERIAL true

#define BTN_LONG_PRESS 5000 // in ms
#define BTN_SHORT_PRESS 1000 // in ms

float pwm = 0;
int lastA;

MedianFilter buttonFilter(45, HIGH);
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


void setup() {
  #if USE_SERIAL
    Serial.begin(115200);
    delay(100);
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
      flashLed(1, 200);
      #if USE_SERIAL
        Serial.println("Short pressed done, switch off for 200ms");
      #endif
    }
    if(btnPressedDuration > BTN_LONG_PRESS && !longPressDone) {
      // switch off light for 500ms
      longPressDone = true;
      flashLed(1, 500);
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
      // if wifi connected, start OTA

      // flash three times to indicate its ready for OTA
      flashLed(3, 200);
      #if USE_SERIAL
        Serial.println("Ready for OTA");
      #endif
    }
  }
  lastBtn = newBtn;


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

  delay(2);
}
