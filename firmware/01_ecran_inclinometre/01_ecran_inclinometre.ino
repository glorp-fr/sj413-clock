/* ============================================================
 * ÉCRAN INCLINOMÈTRE — T-Display-S3 (ESP32-S3, 170x320)
 * Projet : module horloge / inclinomètre Suzuki Samurai SJ413
 * ------------------------------------------------------------
 * Écran séparé verticalement en deux :
 *   - Gauche  : TANGAGE, Samurai vu de profil (roue de secours
 *               à l'arrière)
 *   - Droite  : ROULIS, Samurai vu de dos (roue de secours sur
 *               la porte, pneus rentrés sous la carrosserie)
 *
 * >>> MODE SIMULATION <<<
 * Le MPU-6050 n'est pas encore branché : les angles sont générés
 * par la fonction lireAngles() plus bas. Tout le reste du code
 * est le code définitif. Quand le capteur arrivera, il n'y a QUE
 * cette fonction à remplacer (voir le bloc commenté à l'intérieur).
 *
 * Boutons pendant le test :
 *   KEY  (GPIO14) : met l'animation en pause / la relance
 *   BOOT (GPIO0)  : force un angle extrême pour voir l'alerte
 *
 * PRÉREQUIS : TFT_eSPI configuré avec
 *   #include <User_Setups/Setup206_LilyGo_T_Display_S3.h>
 * ============================================================ */

#include <Arduino.h>
#include <TFT_eSPI.h>

#define PIN_POWER_ON  15
#define PIN_BTN_BOOT   0
#define PIN_BTN_KEY   14

#define LARGEUR_ECRAN  320
#define HAUTEUR_ECRAN  170
#define LARGEUR_PANNEAU (LARGEUR_ECRAN / 2)   // 160

/* ---------- Seuils d'alerte ----------
 * Deux niveaux par axe : "attention" (on approche) puis "critique".
 * Le roulis est plus bas que le tangage : sur un Samurai (empattement
 * très court, voie étroite, centre de gravité haut) c'est le basculement
 * latéral qui est le vrai danger — en montée on perd l'adhérence ou on
 * recule bien avant de basculer.
 *
 * ATTENTION : ces valeurs sont un point de départ VOLONTAIREMENT
 * CONSERVATEUR, pas une limite constructeur. L'angle réel de bascule
 * dépend des pneus, d'un éventuel rehaussement, du chargement et surtout
 * de la galerie de toit — un Samurai réhaussé et chargé sur le toit part
 * nettement plus tôt. Et un angle tenu à l'arrêt ne se tient pas en
 * dynamique : une bosse, un freinage ou un passager qui bouge suffisent.
 * Cet écran est une aide à la lecture du terrain, jamais une autorisation
 * d'aller jusqu'au seuil. Ajustez ces chiffres vers le BAS selon votre
 * véhicule, pas vers le haut. */
const float TANGAGE_ATTENTION = 27.0f;
const float TANGAGE_CRITIQUE  = 35.0f;
const float ROULIS_ATTENTION  = 22.0f;
const float ROULIS_CRITIQUE   = 30.0f;

/* Sens de rotation — si à l'essai dans le véhicule une silhouette
 * penche à l'envers, mettre -1 ici au lieu de 1 (ou l'inverse).
 * C'est le seul réglage à toucher pour corriger un sens. */
#define SENS_TANGAGE   1
#define SENS_ROULIS   -1

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite panneau = TFT_eSprite(&tft);   // un seul sprite réutilisé pour les 2 moitiés

uint16_t COUL_VEHICULE, COUL_ATTENTION, COUL_ALERTE, COUL_TITRE, COUL_VALEUR, COUL_HORIZON;

// Échelle des silhouettes (coordonnées du dessin -> pixels écran)
const float ECHELLE = 0.48f;
const int16_t CY_VEHICULE = 72;    // centre vertical des silhouettes dans le panneau
const int16_t Y_HORIZON   = 84;

/* La silhouette de profil est longue : au-delà de ~34° ses extrémités
 * viendraient mordre sur le titre et sur la valeur. On sature donc
 * l'inclinaison DESSINÉE à cette limite — la valeur numérique affichée,
 * elle, reste toujours l'angle réel. */
const float ANGLE_DESSIN_MAX = 34.0f;

/* ---------- Silhouettes (coordonnées du dessin, centrées sur 0,0) ---------- */

// Profil : capot plat, pare-brise vertical, passages de roue carrés
const int8_t PROFIL_CAISSE[][2] = {
  {-92,6},{-92,-10},{-84,-10},{-84,-24},{-46,-26},{-36,-58},{54,-58},
  {58,-26},{58,6},{54,6},{54,-18},{14,-18},{14,6},{-36,6},{-36,-18},
  {-76,-18},{-76,6}
};
const uint8_t PROFIL_CAISSE_N = 17;

const int8_t PROFIL_SECOURS[][2] = { {58,-46},{71,-46},{71,-4},{58,-4} };
const int8_t PROFIL_VITRES[][2]  = { {-30,-54},{-30,-30},{48,-30},{48,-54} };

// Vue arrière : caisse étroite, pneus sous la carrosserie
const int8_t ARRIERE_CAISSE[][2] = { {-50,4},{-50,-52},{50,-52},{50,4} };
const int8_t ARRIERE_VITRE[][2]  = { {-40,-46},{-40,-24},{40,-24},{40,-46} };
const int8_t ARRIERE_FEU[][2]    = { {-44,-18},{-32,-18},{-32,-3},{-44,-3} };
const int8_t ARRIERE_PNEU_G[][2] = { {-44,6},{-27,6},{-27,26},{-44,26} };
const int8_t ARRIERE_PNEU_D[][2] = { {27,6},{44,6},{44,26},{27,26} };

/* ---------- Outils de rotation / tracé ---------- */

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

// Trace une polyligne pivotée. epais=true dessine un 2e passage décalé
// pour obtenir un trait de 2 px, bien plus lisible sur 160 px de large.
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

/* ---------- Dessin des deux panneaux ---------- */

/* 0 = normal, 1 = on approche, 2 = critique */
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

void dessinerFond(const char *titre, uint16_t couleurTitre, uint8_t niveau) {
  panneau.fillSprite(TFT_BLACK);

  // En critique, un cadre rouge clignotant : c'est ce qu'on capte en
  // vision périphérique sans quitter la piste des yeux.
  if (niveau == 2 && ((millis() / 400) % 2 == 0)) {
    panneau.drawRect(0, 0, LARGEUR_PANNEAU, HAUTEUR_ECRAN, COUL_ALERTE);
    panneau.drawRect(1, 1, LARGEUR_PANNEAU - 2, HAUTEUR_ECRAN - 2, COUL_ALERTE);
    panneau.drawRect(2, 2, LARGEUR_PANNEAU - 4, HAUTEUR_ECRAN - 4, COUL_ALERTE);
  }
  panneau.setTextDatum(TC_DATUM);
  panneau.setTextColor(couleurTitre, TFT_BLACK);
  panneau.drawString(titre, LARGEUR_PANNEAU / 2, 6, 2);

  // Horizon fixe en pointillés : c'est le véhicule qui bouge par rapport à lui
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
  polyligne(PROFIL_SECOURS, 4, true, r, couleur, false);   // roue de secours vue par la tranche
  polyligne(PROFIL_VITRES, 4, false, r, couleur, false);

  int16_t x1, y1, x2, y2;
  projeter(r, 8, -54, x1, y1);
  projeter(r, 8, -30, x2, y2);
  panneau.drawLine(x1, y1, x2, y2, couleur);               // montant central

  cerclePivote(r, -56, 4, 19, couleur, true);              // roue avant
  cerclePivote(r,  34, 4, 19, couleur, true);              // roue arrière
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

  // Roue de secours : remplie en noir AVANT le contour, pour masquer
  // le tracé de la vitre qu'elle recouvre.
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

/* ---------- Source des angles ---------- */

bool enPause = false;
float tempsSimule = 0.0f;

void lireAngles(float &tangage, float &roulis) {
  /* ========== À REMPLACER par le MPU-6050 le jour venu ==========
   *   sensors_event_t a, g, temp;
   *   mpu.getEvent(&a, &g, &temp);
   *   tangage = atan2(a.acceleration.x,
   *               sqrt(a.acceleration.y * a.acceleration.y +
   *                    a.acceleration.z * a.acceleration.z)) * 180.0 / PI - offsetTangage;
   *   roulis  = atan2(a.acceleration.y, a.acceleration.z) * 180.0 / PI - offsetRoulis;
   *   return;
   * ============================================================== */

  // Simulation : deux sinusoïdes de périodes différentes, pour voir
  // les deux moitiés bouger indépendamment et franchir le seuil d'alerte.
  tangage = 40.0f * sinf(tempsSimule * 0.6f);
  roulis  = 34.0f * sinf(tempsSimule * 0.41f + 1.2f);

  if (digitalRead(PIN_BTN_BOOT) == LOW) {   // test de l'affichage d'alerte
    tangage = 38.0f;
    roulis  = -41.0f;
  }
}

/* ---------- Boucle principale ---------- */

bool etatBrutKey = HIGH, etatStableKey = HIGH;
uint32_t dernierFrontKey = 0, dernierRendu = 0, dernierTick = 0;

void setup() {
  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);       // sans ça : écran noir hors USB

  Serial.begin(115200);
  pinMode(PIN_BTN_BOOT, INPUT_PULLUP);
  pinMode(PIN_BTN_KEY, INPUT_PULLUP);

  tft.init();
  tft.setRotation(1);                     // paysage : 320 x 170
  tft.fillScreen(TFT_BLACK);

  COUL_VEHICULE  = tft.color565(93, 202, 165);
  COUL_ATTENTION = tft.color565(239, 159, 39);
  COUL_ALERTE    = tft.color565(226, 75, 74);
  COUL_TITRE    = tft.color565(180, 178, 169);
  COUL_VALEUR   = tft.color565(241, 239, 232);
  COUL_HORIZON  = tft.color565(68, 68, 65);

  panneau.setColorDepth(16);
  if (panneau.createSprite(LARGEUR_PANNEAU, HAUTEUR_ECRAN) == nullptr) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("Sprite: memoire insuffisante", 10, 10, 2);
    while (true) delay(1000);
  }

  // Séparation verticale entre les deux moitiés
  tft.drawFastVLine(LARGEUR_PANNEAU - 1, 12, HAUTEUR_ECRAN - 24, COUL_HORIZON);

  Serial.println("Ecran inclinometre - MODE SIMULATION (pas de MPU-6050)");
  Serial.println("KEY = pause/reprise, BOOT maintenu = test alerte");
}

void loop() {
  // Bouton KEY : pause de l'animation, anti-rebond non bloquant
  bool brut = digitalRead(PIN_BTN_KEY);
  if (brut != etatBrutKey) {
    dernierFrontKey = millis();
    etatBrutKey = brut;
  }
  if ((millis() - dernierFrontKey) > 40 && brut != etatStableKey) {
    etatStableKey = brut;
    if (etatStableKey == LOW) {
      enPause = !enPause;
      Serial.println(enPause ? "Pause" : "Reprise");
    }
  }

  // Avance du temps simulé (indépendante de la cadence d'affichage)
  uint32_t maintenant = millis();
  if (!enPause) tempsSimule += (maintenant - dernierTick) / 1000.0f;
  dernierTick = maintenant;

  // Rendu à ~25 images/s
  if (maintenant - dernierRendu >= 40) {
    dernierRendu = maintenant;
    float tangage, roulis;
    lireAngles(tangage, roulis);
    dessinerProfil(tangage);
    dessinerArriere(roulis);
    // La séparation est redessinée car les sprites couvrent toute la largeur
    tft.drawFastVLine(LARGEUR_PANNEAU - 1, 12, HAUTEUR_ECRAN - 24, COUL_HORIZON);
  }
}
