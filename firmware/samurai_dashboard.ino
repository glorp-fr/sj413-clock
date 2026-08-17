/* ============================================================
 * MODULE HORLOGE / INCLINOMÈTRE - SUZUKI SAMURAI SJ413
 * Carte : ESP32-S3 avec écran IPS 1.9" intégré (170x320, ST7789)
 * Capteurs : MPU-6050 (inclinaison), DS3231 (heure), DS18B20 (temp. ext.)
 * ------------------------------------------------------------
 * Bibliothèques requises (Gestionnaire de bibliothèques Arduino) :
 *   - TFT_eSPI (Bodmer)            -> écran
 *   - Adafruit MPU6050 + Adafruit Unified Sensor -> inclinomètre
 *   - RTClib (Adafruit)            -> horloge DS3231
 *   - OneWire + DallasTemperature (Miles Burton) -> sonde DS18B20
 *
 * IMPORTANT : TFT_eSPI doit être configuré (fichier User_Setup.h ou
 * User_Setup_Select.h) avec le pinout EXACT fourni par le fabricant
 * de votre carte ESP32-S3+écran (chaque revendeur câble différemment
 * le bus SPI de l'écran en interne). Sans ce fichier correct,
 * l'écran restera noir même si le code compile sans erreur.
 * ============================================================ */

#include <Arduino.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <RTClib.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// --- Broches (À ADAPTER selon les GPIO libres de votre carte) ---
#define PIN_BTN_MODE   0    // Bouton haut : changer de mode d'affichage
#define PIN_BTN_SET    14   // Bouton bas  : calibrage / réglage
#define PIN_ONEWIRE    17   // Sonde DS18B20 (température extérieure)
// SDA / SCL : utiliser les broches I2C par défaut de la carte (souvent
// GPIO 8/9 ou GPIO 21/22 selon le modèle - voir la doc du vendeur).
// Le bus I2C est PARTAGÉ entre le MPU-6050 et le DS3231.

TFT_eSPI tft = TFT_eSPI();
Adafruit_MPU6050 mpu;
RTC_DS3231 rtc;
OneWire oneWire(PIN_ONEWIRE);
DallasTemperature exteriorTemp(&oneWire);

// --- État de l'application ---
enum Mode { MODE_INCLINOMETRE, MODE_HORLOGE, MODE_VEILLE, MODE_COUNT };
Mode currentMode = MODE_HORLOGE;

float pitchOffset = 0.0f;
float rollOffset  = 0.0f;

// --- Anti-rebond non bloquant (n'utilise jamais delay() ni while()) ---
bool lastRawState = HIGH;
bool debouncedState = HIGH;
unsigned long lastEdgeTime = 0;
unsigned long pressStartTime = 0;
bool longPressHandled = false;

const unsigned long DEBOUNCE_MS   = 30;
const unsigned long LONG_PRESS_MS = 1500;
const unsigned long REFRESH_MS    = 200;    // ~5 rafraîchissements/s
unsigned long lastRefresh = 0;
unsigned long lastTempRead = 0;
float lastExteriorTempC = NAN;

void setup() {
  Serial.begin(115200);

  pinMode(PIN_BTN_MODE, INPUT_PULLUP);
  pinMode(PIN_BTN_SET, INPUT_PULLUP);

  Wire.begin();

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  if (!mpu.begin()) {
    tft.drawString("Erreur MPU6050", 10, 10, 2);
  } else {
    mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
    mpu.setGyroRange(MPU6050_RANGE_250_DEG);
    mpu.setFilterBandwidth(MPU6050_BANDWIDTH_21_HZ);
  }

  if (!rtc.begin()) {
    tft.drawString("Erreur RTC DS3231", 10, 30, 2);
  } else if (rtc.lostPower()) {
    // Ne se déclenche qu'une fois, à la première mise sous tension
    // (pile CR2032 neuve). Décommenter la ligne suivante puis
    // reflasher UNE FOIS pour régler l'heure sur celle de compilation :
    // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  exteriorTemp.begin();
  delay(200); // laisse le temps aux capteurs de démarrer (une seule fois, au boot)
}

// Lit l'état du bouton avec anti-rebond ; retourne un évènement :
// 0 = rien, 1 = appui court relâché, 2 = appui long détecté (une seule fois)
int pollButton(int pin, bool &rawLast, bool &debounced, unsigned long &edgeTime,
               unsigned long &pressStart, bool &longHandled) {
  bool raw = digitalRead(pin);
  int event = 0;

  if (raw != rawLast) {
    edgeTime = millis();
    rawLast = raw;
  }

  if ((millis() - edgeTime) > DEBOUNCE_MS && raw != debounced) {
    debounced = raw;
    if (debounced == LOW) {
      pressStart = millis();
      longHandled = false;
    } else {
      unsigned long heldFor = millis() - pressStart;
      if (heldFor < LONG_PRESS_MS && !longHandled) {
        event = 1; // appui court
      }
    }
  }

  if (debounced == LOW && !longHandled && (millis() - pressStart) >= LONG_PRESS_MS) {
    longHandled = true;
    event = 2; // appui long
  }

  return event;
}

void drawInclinometre(float pitch, float roll) {
  tft.fillScreen(TFT_BLACK);
  tft.drawString("INCLINOMETRE", 10, 8, 4);
  tft.drawString("Tangage: " + String(pitch, 1) + "  ", 10, 55, 4);
  tft.drawString("Roulis : " + String(roll, 1) + "  ", 10, 90, 4);
  if (abs(pitch) > 30 || abs(roll) > 30) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("ANGLE CRITIQUE", 10, 130, 2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
  }
}

void drawHorloge(DateTime now, float tempC) {
  tft.fillScreen(TFT_BLACK);
  char buf[9];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
  tft.drawString(buf, 20, 40, 7);
  if (!isnan(tempC)) {
    tft.drawString("Ext: " + String(tempC, 1) + "C  ", 20, 110, 4);
  } else {
    tft.drawString("Ext: --  ", 20, 110, 4);
  }
}

void loop() {
  int evtMode = pollButton(PIN_BTN_MODE, lastRawState, debouncedState, lastEdgeTime,
                            pressStartTime, longPressHandled);
  // Bouton "MODE" : appui court = mode suivant, appui long = idem (pas de fonction dédiée ici)
  if (evtMode == 1 || evtMode == 2) {
    currentMode = (Mode)((currentMode + 1) % MODE_COUNT);
    tft.fillScreen(TFT_BLACK);
  }

  static bool lastRawSet = HIGH, debouncedSet = HIGH;
  static unsigned long edgeSet = 0, pressStartSet = 0;
  static bool longHandledSet = false;
  int evtSet = pollButton(PIN_BTN_SET, lastRawSet, debouncedSet, edgeSet,
                           pressStartSet, longHandledSet);
  // Bouton "SET" : appui long = calibrage zéro de l'inclinomètre
  if (evtSet == 2 && currentMode == MODE_INCLINOMETRE) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    pitchOffset = atan2(a.acceleration.x,
                    sqrt(a.acceleration.y * a.acceleration.y + a.acceleration.z * a.acceleration.z))
                    * 180.0 / PI;
    rollOffset  = atan2(a.acceleration.y, a.acceleration.z) * 180.0 / PI;
    tft.fillScreen(TFT_GREEN);
    delay(200); // clignotement de confirmation, ponctuel, sans impact sur la lecture des capteurs
    tft.fillScreen(TFT_BLACK);
  }

  // Rafraîchit la température extérieure toutes les 5s (DS18B20 est lent : ~750ms/lecture)
  if (millis() - lastTempRead > 5000) {
    exteriorTemp.requestTemperatures();
    lastExteriorTempC = exteriorTemp.getTempCByIndex(0);
    lastTempRead = millis();
  }

  // Rafraîchit l'affichage à cadence fixe (évite de spammer le bus SPI)
  if (millis() - lastRefresh > REFRESH_MS) {
    lastRefresh = millis();

    if (currentMode == MODE_INCLINOMETRE) {
      sensors_event_t a, g, temp;
      mpu.getEvent(&a, &g, &temp);
      float pitch = (atan2(a.acceleration.x,
                      sqrt(a.acceleration.y * a.acceleration.y + a.acceleration.z * a.acceleration.z))
                      * 180.0 / PI) - pitchOffset;
      float roll = (atan2(a.acceleration.y, a.acceleration.z) * 180.0 / PI) - rollOffset;
      drawInclinometre(pitch, roll);
    } else if (currentMode == MODE_HORLOGE) {
      DateTime now = rtc.now();
      drawHorloge(now, lastExteriorTempC);
    } else if (currentMode == MODE_VEILLE) {
      tft.fillScreen(TFT_BLACK);
    }
  }
}
