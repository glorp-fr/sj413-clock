/* ============================================================
 * TEST "HELLO WORLD" — T-Display-S3 (ESP32-S3 + ST7789 1.9 pouces, 170x320)
 * Projet : module horloge / inclinomètre Suzuki Samurai SJ413
 * ------------------------------------------------------------
 * But de ce croquis : valider le matériel AVANT de câbler quoi
 * que ce soit. Il teste, dans l'ordre :
 *   1. l'alimentation de l'écran (GPIO15)
 *   2. l'affichage (texte, couleurs, orientation)
 *   3. les 2 boutons intégrés (BOOT = GPIO0, KEY = GPIO14)
 *   4. la liaison série USB
 *
 * Aucun capteur n'est nécessaire : ne branchez RIEN pour ce test,
 * juste le câble USB-C.
 * ------------------------------------------------------------
 * PRÉREQUIS TFT_eSPI (à faire une seule fois) :
 *   Dans le dossier de la bibliothèque TFT_eSPI, ouvrez
 *   User_Setup_Select.h, commentez la ligne :
 *       #include <User_Setup.h>
 *   et décommentez :
 *       #include <User_Setups/Setup206_LilyGo_T_Display_S3.h>
 *   Ne modifiez PAS User_Setup.h à la main : cette carte utilise
 *   un bus parallèle 8 bits, pas du SPI — les brochages SPI
 *   trouvés sur les blogs ne fonctionneront pas.
 *
 * RÉGLAGES ARDUINO IDE (Outils) :
 *   Carte             : ESP32S3 Dev Module
 *   USB CDC On Boot   : Enabled  (pour voir le moniteur série)
 *   Flash Size        : 16MB (128Mb)
 *   Partition Scheme  : 16M Flash (3M APP/9.9MB FATFS)
 *   PSRAM             : OPI PSRAM
 *   USB Mode          : CDC and JTAG
 * ============================================================ */

#include <Arduino.h>
#include <TFT_eSPI.h>

#define PIN_POWER_ON  15   // Alimentation écran : DOIT être HIGH
#define PIN_BTN_BOOT   0   // Bouton BOOT intégré (gauche)
#define PIN_BTN_KEY   14   // Bouton KEY intégré (droite)

TFT_eSPI tft = TFT_eSPI();

// Couleurs testées en cycle par le bouton KEY
const uint16_t COULEURS[] = { TFT_BLACK, TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE };
const char* NOMS_COULEURS[] = { "Noir", "Rouge", "Vert", "Bleu", "Blanc" };
const uint8_t NB_COULEURS = 5;
uint8_t indexCouleur = 0;

uint32_t compteur = 0;
uint32_t dernierRefresh = 0;

// Anti-rebond non bloquant (même principe que le firmware final)
bool etatBrutKey = HIGH, etatStableKey = HIGH;
uint32_t dernierFrontKey = 0;
const uint16_t ANTIREBOND_MS = 40;

void dessinerEcran() {
  uint16_t fond = COULEURS[indexCouleur];
  // Texte lisible quel que soit le fond
  uint16_t encre = (fond == TFT_WHITE || fond == TFT_GREEN) ? TFT_BLACK : TFT_WHITE;

  tft.fillScreen(fond);
  tft.setTextColor(encre, fond);

  tft.drawString("Hello Samurai !", 12, 14, 4);

  tft.drawString("Ecran   : " + String(tft.width()) + "x" + String(tft.height()), 12, 56, 2);
  tft.drawString("Fond    : " + String(NOMS_COULEURS[indexCouleur]), 12, 76, 2);
  tft.drawString("Uptime  : " + String(millis() / 1000) + " s", 12, 96, 2);
  tft.drawString("Cycles  : " + String(compteur), 12, 116, 2);

  // État des boutons, lu en direct
  String boot = (digitalRead(PIN_BTN_BOOT) == LOW) ? "APPUYE" : "relache";
  String key  = (digitalRead(PIN_BTN_KEY)  == LOW) ? "APPUYE" : "relache";
  tft.drawString("BOOT: " + boot + "   KEY: " + key, 12, 142, 2);
}

void setup() {
  // 1. Alimentation de l'écran — sans ça, écran noir sur alim externe
  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);

  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("=== Test T-Display-S3 - module Samurai SJ413 ===");

  pinMode(PIN_BTN_BOOT, INPUT_PULLUP);
  pinMode(PIN_BTN_KEY,  INPUT_PULLUP);

  tft.init();
  tft.setRotation(1);            // Paysage : 320 x 170
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  Serial.printf("Resolution detectee : %d x %d\n", tft.width(), tft.height());
  Serial.println("Appui sur KEY (GPIO14) = changer la couleur de fond");
  Serial.println("Appui sur BOOT (GPIO0) = affiche un message serie");

  dessinerEcran();
}

void loop() {
  // --- Bouton KEY : change la couleur de fond (anti-rebond) ---
  bool brut = digitalRead(PIN_BTN_KEY);
  if (brut != etatBrutKey) {
    dernierFrontKey = millis();
    etatBrutKey = brut;
  }
  if ((millis() - dernierFrontKey) > ANTIREBOND_MS && brut != etatStableKey) {
    etatStableKey = brut;
    if (etatStableKey == LOW) {              // front descendant = appui
      indexCouleur = (indexCouleur + 1) % NB_COULEURS;
      Serial.printf("KEY presse -> fond %s\n", NOMS_COULEURS[indexCouleur]);
      dessinerEcran();
    }
  }

  // --- Bouton BOOT : simple retour série (test de lecture GPIO) ---
  static bool bootPrecedent = HIGH;
  bool bootActuel = digitalRead(PIN_BTN_BOOT);
  if (bootPrecedent == HIGH && bootActuel == LOW) {
    Serial.println("BOOT presse - la lecture GPIO fonctionne");
  }
  bootPrecedent = bootActuel;

  // --- Rafraîchissement périodique (uptime, compteur, etat boutons) ---
  if (millis() - dernierRefresh > 500) {
    dernierRefresh = millis();
    compteur++;
    dessinerEcran();
  }
}
