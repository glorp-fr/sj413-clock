/* ============================================================
 * MODULE HORLOGE / INCLINOMÈTRE — SUZUKI SAMURAI SJ413
 * Carte : LilyGO T-Display-S3 (ESP32-S3 + ST7789 170x320,
 *         bus PARALLÈLE 8 bits — surtout pas du SPI)
 * ------------------------------------------------------------
 * Modes (bouton KEY = mode suivant) :
 *   0. Inclinomètre — écran coupé en deux : tangage (profil) et
 *      roulis (vue arrière), silhouettes de Samurai qui pivotent
 *   1. Horloge      — heure, date, température extérieure
 *   2. Veille       — écran noir
 *
 * Bouton BOOT, appui long : calibrage du zéro de l'inclinomètre
 * (à faire véhicule à plat, moteur arrêté).
 * ------------------------------------------------------------
 * MODE SIMULATION
 * Mettre SIMULATION à 1 pour faire tourner l'ensemble SANS aucun
 * capteur branché (angles sinusoïdaux, heure interne, température
 * factice). Mettre 0 quand les capteurs sont câblés.
 * ------------------------------------------------------------
 * PRÉREQUIS TFT_eSPI (une seule fois) :
 *   Dans User_Setup_Select.h : commenter #include <User_Setup.h>
 *   et décommenter
 *   #include <User_Setups/Setup206_LilyGo_T_Display_S3.h>
 *
 * ARDUINO IDE : ESP32S3 Dev Module / PSRAM OPI / Flash 16MB /
 *   Partition 16M Flash (3M APP/9.9MB FATFS) / USB CDC On Boot
 * ============================================================ */

#define SIMULATION 1

#include <Arduino.h>
#include <TFT_eSPI.h>

#if !SIMULATION
  #include <Wire.h>
  #include <Adafruit_MPU6050.h>
  #include <Adafruit_Sensor.h>
  #include <RTClib.h>
  #include <OneWire.h>
  #include <DallasTemperature.h>
#endif

/* ---------------- Broches ---------------- */
#define PIN_POWER_ON   15    // alimentation des périphériques : DOIT être HIGH
#define PIN_BTN_BOOT    0    // bouton intégré gauche
#define PIN_BTN_KEY    14    // bouton intégré droit
#define PIN_ONEWIRE    16    // sonde DS18B20
#define PIN_I2C_SDA    43    // connecteur Qwiic / STEMMA QT
#define PIN_I2C_SCL    44

/* MPU-6050 forcé en 0x69 (broche AD0 reliée au 3V3) : en 0x68 par
 * défaut il entrerait en conflit avec le DS3231, qui occupe la même
 * adresse. Sans ce cavalier, les deux capteurs se marchent dessus. */
#define ADRESSE_MPU  0x69

/* ---------------- Géométrie de l'affichage ---------------- */
#define LARGEUR_ECRAN    320
#define HAUTEUR_ECRAN    170
#define LARGEUR_PANNEAU  (LARGEUR_ECRAN / 2)

const float   ECHELLE     = 0.48f;   // valeurs validées : aux angles extrêmes
const int16_t CY_VEHICULE = 72;      // la silhouette ne mord ni sur le titre
const int16_t Y_HORIZON   = 84;      // ni sur la valeur

/* La silhouette de profil est longue : au-delà de ~34° ses extrémités
 * viendraient chevaucher le texte. On sature l'inclinaison DESSINÉE ;
 * la valeur numérique affichée reste toujours l'angle réel. */
const float ANGLE_DESSIN_MAX = 34.0f;

/* ---------------- Seuils d'alerte ----------------
 * Deux niveaux par axe. Le roulis est plus bas que le tangage : sur un
 * Samurai (empattement court, voie étroite, centre de gravité haut)
 * c'est le basculement latéral le vrai danger — en montée on perd
 * l'adhérence ou on recule bien avant de basculer.
 *
 * ATTENTION : point de départ VOLONTAIREMENT CONSERVATEUR, pas une
 * limite constructeur. L'angle réel de bascule dépend des pneus, d'un
 * rehaussement, du chargement et surtout d'une galerie de toit. Un
 * angle tenu à l'arrêt ne se tient pas en dynamique : une bosse, un
 * freinage, un passager qui se déplace suffisent. Cet écran est une
 * aide à la lecture du terrain, jamais une autorisation d'aller
 * jusqu'au seuil. Ajustez ces valeurs vers le BAS, pas vers le haut. */
const float TANGAGE_ATTENTION = 27.0f;
const float TANGAGE_CRITIQUE  = 35.0f;
const float ROULIS_ATTENTION  = 22.0f;
const float ROULIS_CRITIQUE   = 30.0f;

/* Sens de rotation : si une silhouette penche à l'envers une fois
 * installée dans le véhicule, inverser le 1 en -1 ici. */
#define SENS_TANGAGE   1
#define SENS_ROULIS   -1

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite panneau = TFT_eSprite(&tft);   // un seul sprite, réutilisé pour les 2 moitiés

#if !SIMULATION
  Adafruit_MPU6050 mpu;
  RTC_DS3231 rtc;
  OneWire oneWire(PIN_ONEWIRE);
  DallasTemperature sondeExt(&oneWire);
  bool mpuOk = false, rtcOk = false;
#endif

uint16_t COUL_VEHICULE, COUL_ATTENTION, COUL_ALERTE, COUL_TITRE, COUL_VALEUR, COUL_HORIZON;

enum Mode { MODE_INCLINO, MODE_HORLOGE, MODE_VEILLE, NB_MODES };
uint8_t mode = MODE_INCLINO;
bool modeChange = true;

float offsetTangage = 0.0f, offsetRoulis = 0.0f;
float tempExterieure = NAN;

/* ---------------- Silhouettes (coordonnées du dessin, centrées) ---------------- */

const int8_t PROFIL_CAISSE[][2] = {
  {-92,6},{-92,-10},{-84,-10},{-84,-24},{-46,-26},{-36,-58},{54,-58},
  {58,-26},{58,6},{54,6},{54,-18},{14,-18},{14,6},{-36,6},{-36,-18},
  {-76,-18},{-76,6}
};
const uint8_t PROFIL_CAISSE_N = 17;
const int8_t PROFIL_SECOURS[][2] = { {58,-46},{71,-46},{71,-4},{58,-4} };
const int8_t PROFIL_VITRES[][2]  = { {-30,-54},{-30,-30},{48,-30},{48,-54} };

const int8_t ARRIERE_CAISSE[][2] = { {-50,4},{-50,-52},{50,-52},{50,4} };
const int8_t ARRIERE_VITRE[][2]  = { {-40,-46},{-40,-24},{40,-24},{40,-46} };
const int8_t ARRIERE_FEU[][2]    = { {-44,-18},{-32,-18},{-32,-3},{-44,-3} };
const int8_t ARRIERE_PNEU_G[][2] = { {-44,6},{-27,6},{-27,26},{-44,26} };
const int8_t ARRIERE_PNEU_D[][2] = { {27,6},{44,6},{44,26},{27,26} };

/* ---------------- Rotation et tracé ---------------- */

struct Repere { float cosA, sinA, cx, cy; };

Repere faireRepere(float angleDeg, float cx, float cy) {
  Repere r;
  if (angleDeg >  ANGLE_DESSIN_MAX) angleDeg =  ANGLE_DESSIN_MAX;
  if (angleDeg < -ANGLE_DESSIN_MAX) angleDeg = -ANGLE_DESSIN_MAX;
  float a = angleDeg * DEG_TO_RAD;
  r.cosA = cosf(a);
  r.sinA = sinf(a);
  r.cx = cx;
  r.cy = cy;
  return r;
}

void projeter(const Repere &r, float x, float y, int16_t &sx, int16_t &sy) {
  sx = (int16_t)lroundf(r.cx + (x * r.cosA - y * r.sinA) * ECHELLE);
  sy = (int16_t)lroundf(r.cy + (x * r.sinA + y * r.cosA) * ECHELLE);
}

void polyligne(const int8_t pts[][2], uint8_t n, bool fermee,
               const Repere &r, uint16_t couleur, bool epais) {
  int16_t xd, yd, xp, yp, xc, yc;
  projeter(r, pts[0][0], pts[0][1], xd, yd);
  xp = xd; yp = yd;
  for (uint8_t i = 1; i < n; i++) {
    projeter(r, pts[i][0], pts[i][1], xc, yc);
    panneau.drawLine(xp, yp, xc, yc, couleur);
    if (epais) panneau.drawLine(xp, yp + 1, xc, yc + 1, couleur);
    xp = xc; yp = yc;
  }
  if (fermee) {
    panneau.drawLine(xp, yp, xd, yd, couleur);
    if (epais) panneau.drawLine(xp, yp + 1, xd, yd + 1, couleur);
  }
}

void cerclePivote(const Repere &r, float x, float y, float rayon,
                  uint16_t couleur, bool epais) {
  int16_t sx, sy;
  projeter(r, x, y, sx, sy);
  int16_t rp = (int16_t)lroundf(rayon * ECHELLE);
  panneau.drawCircle(sx, sy, rp, couleur);
  if (epais && rp > 1) panneau.drawCircle(sx, sy, rp - 1, couleur);
}

/* ---------------- Niveaux d'alerte ---------------- */

uint8_t niveauAlerte(float angle, float attention, float critique) {
  float a = fabsf(angle);
  if (a >= critique)  return 2;
  if (a >= attention) return 1;
  return 0;
}

uint16_t couleurNiveau(uint8_t niveau, uint16_t couleurNormale) {
  if (niveau == 2) return COUL_ALERTE;
  if (niveau == 1) return COUL_ATTENTION;
  return couleurNormale;
}

/* ---------------- Écran inclinomètre ---------------- */

void dessinerFond(const char *titre, uint16_t couleurTitre, uint8_t niveau) {
  panneau.fillSprite(TFT_BLACK);

  // Cadre rouge clignotant en critique : c'est ce qu'on capte en vision
  // périphérique sans quitter la piste des yeux.
  if (niveau == 2 && ((millis() / 400) % 2 == 0)) {
    panneau.drawRect(0, 0, LARGEUR_PANNEAU, HAUTEUR_ECRAN, COUL_ALERTE);
    panneau.drawRect(1, 1, LARGEUR_PANNEAU - 2, HAUTEUR_ECRAN - 2, COUL_ALERTE);
    panneau.drawRect(2, 2, LARGEUR_PANNEAU - 4, HAUTEUR_ECRAN - 4, COUL_ALERTE);
  }

  panneau.setTextDatum(TC_DATUM);
  panneau.setTextColor(couleurTitre, TFT_BLACK);
  panneau.drawString(titre, LARGEUR_PANNEAU / 2, 6, 2);

  for (int16_t x = 14; x < LARGEUR_PANNEAU - 14; x += 8) {
    panneau.drawFastHLine(x, Y_HORIZON, 4, COUL_HORIZON);
  }
}

void dessinerValeur(float angle, uint16_t couleur) {
  char txt[8];
  snprintf(txt, sizeof(txt), "%d", (int)lroundf(angle));

  panneau.setTextDatum(TC_DATUM);
  panneau.setTextColor(couleur, TFT_BLACK);
  panneau.drawString(txt, LARGEUR_PANNEAU / 2 - 6, 112, 6);

  // Le symbole ° n'existe pas dans la police 6 : on le dessine
  int16_t largeur = panneau.textWidth(txt, 6);
  int16_t xd = LARGEUR_PANNEAU / 2 - 6 + largeur / 2 + 9;
  panneau.drawCircle(xd, 122, 5, couleur);
  panneau.drawCircle(xd, 122, 4, couleur);
}

void dessinerProfil(float tangage) {
  uint8_t niveau = niveauAlerte(tangage, TANGAGE_ATTENTION, TANGAGE_CRITIQUE);
  uint16_t couleur = couleurNiveau(niveau, COUL_VEHICULE);

  dessinerFond("Tangage", couleurNiveau(niveau, COUL_TITRE), niveau);
  Repere r = faireRepere(SENS_TANGAGE * tangage, LARGEUR_PANNEAU / 2.0f, CY_VEHICULE);

  polyligne(PROFIL_CAISSE, PROFIL_CAISSE_N, true, r, couleur, true);
  polyligne(PROFIL_SECOURS, 4, true, r, couleur, false);   // roue de secours par la tranche
  polyligne(PROFIL_VITRES, 4, false, r, couleur, false);

  int16_t x1, y1, x2, y2;
  projeter(r, 8, -54, x1, y1);
  projeter(r, 8, -30, x2, y2);
  panneau.drawLine(x1, y1, x2, y2, couleur);               // montant central

  cerclePivote(r, -56, 4, 19, couleur, true);
  cerclePivote(r,  34, 4, 19, couleur, true);
  cerclePivote(r, -56, 4, 7, couleur, false);
  cerclePivote(r,  34, 4, 7, couleur, false);

  dessinerValeur(tangage, couleurNiveau(niveau, COUL_VALEUR));
  panneau.pushSprite(0, 0);
}

void dessinerArriere(float roulis) {
  uint8_t niveau = niveauAlerte(roulis, ROULIS_ATTENTION, ROULIS_CRITIQUE);
  uint16_t couleur = couleurNiveau(niveau, COUL_VEHICULE);

  dessinerFond("Roulis", couleurNiveau(niveau, COUL_TITRE), niveau);
  Repere r = faireRepere(SENS_ROULIS * roulis, LARGEUR_PANNEAU / 2.0f, CY_VEHICULE);

  polyligne(ARRIERE_CAISSE, 4, true, r, couleur, true);
  polyligne(ARRIERE_VITRE, 4, true, r, couleur, false);
  polyligne(ARRIERE_FEU, 4, true, r, couleur, false);

  int16_t x1, y1, x2, y2;
  projeter(r, -50, 6, x1, y1);
  projeter(r,  50, 6, x2, y2);
  panneau.drawLine(x1, y1, x2, y2, couleur);               // pare-chocs
  panneau.drawLine(x1, y1 + 1, x2, y2 + 1, couleur);

  // Roue de secours : remplie en noir AVANT le contour, pour masquer le
  // tracé de la vitre qu'elle recouvre.
  int16_t xs, ys;
  projeter(r, 14, -20, xs, ys);
  int16_t rs = (int16_t)lroundf(18 * ECHELLE);
  panneau.fillCircle(xs, ys, rs, TFT_BLACK);
  panneau.drawCircle(xs, ys, rs, couleur);
  panneau.drawCircle(xs, ys, rs - 1, couleur);
  panneau.drawCircle(xs, ys, (int16_t)lroundf(7 * ECHELLE), couleur);

  polyligne(ARRIERE_PNEU_G, 4, true, r, couleur, true);    // pneus vus de dos
  polyligne(ARRIERE_PNEU_D, 4, true, r, couleur, true);

  dessinerValeur(roulis, couleurNiveau(niveau, COUL_VALEUR));
  panneau.pushSprite(LARGEUR_PANNEAU, 0);
}

/* ---------------- Écran horloge ----------------
 * Dessiné directement sur l'écran (pas de sprite) : le contenu ne change
 * qu'une fois par seconde, le scintillement est invisible et on économise
 * les 108 Ko qu'occuperait un sprite plein écran. */

void dessinerHorloge(uint8_t h, uint8_t m, uint8_t s, uint16_t annee,
                     uint8_t mois, uint8_t jour, float tempC, bool complet) {
  if (complet) tft.fillScreen(TFT_BLACK);

  char heure[6], secondes[4], date[12], temp[16];
  snprintf(heure, sizeof(heure), "%02d:%02d", h, m);
  snprintf(secondes, sizeof(secondes), "%02d", s);
  snprintf(date, sizeof(date), "%02d/%02d/%04d", jour, mois, annee);
  if (isnan(tempC)) snprintf(temp, sizeof(temp), "Ext  --");
  else              snprintf(temp, sizeof(temp), "Ext  %.1fC", tempC);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COUL_VALEUR, TFT_BLACK);
  tft.drawString(heure, 18, 28, 7);

  tft.setTextColor(COUL_TITRE, TFT_BLACK);
  tft.drawString(secondes, 232, 62, 4);

  tft.setTextColor(COUL_VEHICULE, TFT_BLACK);
  tft.drawString(date, 18, 118, 4);
  tft.drawString(temp, 190, 118, 4);
}

/* ---------------- Lecture des capteurs ---------------- */

#if SIMULATION
float tempsSimule = 0.0f;
#endif

void lireAngles(float &tangage, float &roulis) {
#if SIMULATION
  tangage = 40.0f * sinf(tempsSimule * 0.6f);
  roulis  = 34.0f * sinf(tempsSimule * 0.41f + 1.2f);
#else
  if (!mpuOk) { tangage = 0; roulis = 0; return; }
  sensors_event_t a, g, t;
  mpu.getEvent(&a, &g, &t);
  tangage = atan2f(a.acceleration.x,
              sqrtf(a.acceleration.y * a.acceleration.y +
                    a.acceleration.z * a.acceleration.z)) * RAD_TO_DEG - offsetTangage;
  roulis  = atan2f(a.acceleration.y, a.acceleration.z) * RAD_TO_DEG - offsetRoulis;
#endif
}

void calibrerZero() {
#if !SIMULATION
  if (!mpuOk) return;
  // Moyenne sur quelques lectures : une seule mesure est bruitée.
  float st = 0, sr = 0;
  const uint8_t N = 16;
  for (uint8_t i = 0; i < N; i++) {
    sensors_event_t a, g, t;
    mpu.getEvent(&a, &g, &t);
    st += atan2f(a.acceleration.x,
            sqrtf(a.acceleration.y * a.acceleration.y +
                  a.acceleration.z * a.acceleration.z)) * RAD_TO_DEG;
    sr += atan2f(a.acceleration.y, a.acceleration.z) * RAD_TO_DEG;
    delay(10);
  }
  offsetTangage = st / N;
  offsetRoulis  = sr / N;
#endif
  tft.fillScreen(COUL_VEHICULE);
  delay(250);
  modeChange = true;
}

/* ---------------- Boutons (anti-rebond non bloquant) ---------------- */

struct Bouton {
  uint8_t pin;
  bool brut, stable;
  uint32_t front, debutAppui;
  bool longTraite;
};

Bouton btnKey  = { PIN_BTN_KEY,  HIGH, HIGH, 0, 0, false };
Bouton btnBoot = { PIN_BTN_BOOT, HIGH, HIGH, 0, 0, false };

// Retourne 0 = rien, 1 = appui court relâché, 2 = appui long atteint
uint8_t lireBouton(Bouton &b) {
  const uint16_t ANTIREBOND = 40, APPUI_LONG = 1500;
  uint8_t evt = 0;
  bool lu = digitalRead(b.pin);
  if (lu != b.brut) { b.front = millis(); b.brut = lu; }
  if ((millis() - b.front) > ANTIREBOND && lu != b.stable) {
    b.stable = lu;
    if (b.stable == LOW) { b.debutAppui = millis(); b.longTraite = false; }
    else if (!b.longTraite) evt = 1;
  }
  if (b.stable == LOW && !b.longTraite && (millis() - b.debutAppui) >= APPUI_LONG) {
    b.longTraite = true;
    evt = 2;
  }
  return evt;
}

/* ---------------- Setup / loop ---------------- */

uint32_t dernierRendu = 0, dernierTick = 0, dernierTemp = 0, derniereSeconde = 0;

void setup() {
  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);       // sans ça : écran noir hors USB

  Serial.begin(115200);
  pinMode(PIN_BTN_BOOT, INPUT_PULLUP);
  pinMode(PIN_BTN_KEY, INPUT_PULLUP);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  COUL_VEHICULE  = tft.color565(93, 202, 165);
  COUL_ATTENTION = tft.color565(239, 159, 39);
  COUL_ALERTE    = tft.color565(226, 75, 74);
  COUL_TITRE     = tft.color565(180, 178, 169);
  COUL_VALEUR    = tft.color565(241, 239, 232);
  COUL_HORIZON   = tft.color565(68, 68, 65);

  panneau.setColorDepth(16);
  if (panneau.createSprite(LARGEUR_PANNEAU, HAUTEUR_ECRAN) == nullptr) {
    tft.setTextColor(COUL_ALERTE, TFT_BLACK);
    tft.drawString("Sprite: memoire insuffisante", 10, 10, 2);
    while (true) delay(1000);
  }

#if !SIMULATION
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  mpuOk = mpu.begin(ADRESSE_MPU);
  if (mpuOk) {
    mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
    mpu.setGyroRange(MPU6050_RANGE_250_DEG);
    mpu.setFilterBandwidth(MPU6050_BANDWIDTH_21_HZ);
  } else {
    Serial.println("MPU-6050 absent (AD0 bien relie au 3V3 pour 0x69 ?)");
  }

  rtcOk = rtc.begin();
  if (!rtcOk) Serial.println("DS3231 absent");
  // Premier flash : décommenter UNE FOIS pour régler l'heure, puis
  // recommenter et reflasher, sinon l'heure repart à la compilation.
  // else if (rtc.lostPower()) rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  sondeExt.begin();
  sondeExt.setWaitForConversion(false);   // lecture non bloquante
  sondeExt.requestTemperatures();
#endif

  Serial.println(SIMULATION ? "Demarrage en MODE SIMULATION" : "Demarrage en mode capteurs");
}

void loop() {
  uint32_t maintenant = millis();

  // KEY : mode suivant
  if (lireBouton(btnKey) == 1) {
    mode = (mode + 1) % NB_MODES;
    modeChange = true;
  }
  // BOOT appui long : calibrage du zéro (uniquement en mode inclinomètre)
  if (lireBouton(btnBoot) == 2 && mode == MODE_INCLINO) {
    calibrerZero();
  }

#if SIMULATION
  tempsSimule += (maintenant - dernierTick) / 1000.0f;
#endif
  dernierTick = maintenant;

  // Température extérieure : le DS18B20 met ~750 ms à convertir, on le
  // lit sans bloquer et on relance une conversion à chaque passage.
  if (maintenant - dernierTemp > 5000) {
    dernierTemp = maintenant;
#if SIMULATION
    tempExterieure = 17.0f + 3.0f * sinf(maintenant / 20000.0f);
#else
    float t = sondeExt.getTempCByIndex(0);
    tempExterieure = (t < -100) ? NAN : t;   // -127 = sonde absente
    sondeExt.requestTemperatures();
#endif
  }

  if (modeChange) {
    tft.fillScreen(TFT_BLACK);
    modeChange = false;
    derniereSeconde = 0;
  }

  if (mode == MODE_INCLINO) {
    if (maintenant - dernierRendu >= 40) {      // ~25 images/s
      dernierRendu = maintenant;
      float tangage, roulis;
      lireAngles(tangage, roulis);
      dessinerProfil(tangage);
      dessinerArriere(roulis);
      tft.drawFastVLine(LARGEUR_PANNEAU - 1, 12, HAUTEUR_ECRAN - 24, COUL_HORIZON);
    }
  } else if (mode == MODE_HORLOGE) {
    if (maintenant - derniereSeconde >= 1000) {
      derniereSeconde = maintenant;
#if SIMULATION
      uint32_t s = maintenant / 1000;
      dessinerHorloge((s / 3600) % 24, (s / 60) % 60, s % 60,
                      2026, 1, 1, tempExterieure, false);
#else
      if (rtcOk) {
        DateTime n = rtc.now();
        dessinerHorloge(n.hour(), n.minute(), n.second(),
                        n.year(), n.month(), n.day(), tempExterieure, false);
      } else {
        tft.setTextColor(COUL_ALERTE, TFT_BLACK);
        tft.drawString("RTC absent", 18, 60, 4);
      }
#endif
    }
  }
  // MODE_VEILLE : rien à dessiner, l'écran reste noir
}
