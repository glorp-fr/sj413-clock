0# Module Horloge / Inclinomètre — Suzuki Samurai SJ413

Remplacement de l'horloge d'origine du tableau de bord d'un Suzuki Samurai
SJ413 (2001) par un module numérique : heure, inclinomètre (tangage /
roulis), température extérieure. Basé sur un ESP32-S3 avec écran IPS 1.9"
intégré (170x320, ST7789), boîtier imprimé en 3D sur mesure pour la découpe
d'origine (63 x 42 mm), assemblage par clips sans vis.

![status](https://img.shields.io/badge/status-en%20cours-yellow)

## Sommaire

- [Fonctionnalités](#fonctionnalités)
- [Liste des composants (BOM)](#liste-des-composants-bom)
- [Structure du dépôt](#structure-du-dépôt)
- [Câblage](#câblage)
- [Firmware](#firmware)
- [Impression 3D](#impression-3d)
- [Assemblage](#assemblage)
- [Réglages / dépannage](#réglages--dépannage)
- [Historique des révisions](#historique-des-révisions)

## Fonctionnalités

- Horloge temps réel (RTC, garde l'heure même véhicule débranché)
- Inclinomètre tangage / roulis avec alerte visuelle au-delà de 30°
- Température extérieure (sonde étanche)
- Navigation à 2 boutons (cycle des modes + calibrage du zéro)
- S'intègre dans la découpe rectangulaire d'origine de l'horloge (63x42mm),
  débord de collerette pour masquer les bords de la découpe
- Port USB-C accessible depuis l'extérieur du boîtier (programmation /
  alimentation de secours)
- Fentes de refroidissement pour l'ESP32-S3

## Liste des composants (BOM)

| Composant | Spécifications | Qté | Rôle |
| :--- | :--- | :---: | :--- |
| Carte microcontrôleur | ESP32-S3 avec écran IPS 1.9" intégré (170x320 px, ST7789) | 1 | Cerveau + affichage |
| Capteur d'inclinaison | MPU-6050 (accéléromètre/gyroscope 6 axes, I2C) | 1 | Tangage / roulis |
| Horloge temps réel | Module RTC DS3231 + pile CR2032 | 1 | Garde l'heure hors tension |
| Sonde de température | DS18B20 étanche, câble 1,5–2 m (1-Wire) | 1 | Température extérieure |
| Boutons poussoirs | Micro-interrupteurs silicone étanches 8x8x5mm, 4 broches | 2 | Navigation / calibrage |
| Alimentation | Convertisseur DC-DC step-down (buck) 12V → 5V, 3A | 1 | Adapte le 12V véhicule |
| Protection électrique | Diode 1N4007 (anti-inversion) + porte-fusible 1A | 1 lot | Protège l'ESP32 |
| Câblage | Fil souple 24 AWG, gaine thermo, câble USB-C | 1 lot | Connexions |
| Impression 3D | Filament PETG ou ASA noir mat | ~60 g | Façade + boîtier |

Coût estimé (hors imprimante) : ~30–45 € en pièces AliExpress.

## Structure du dépôt

```
.
├── README.md
├── firmware/
│   └── samurai_dashboard.ino
└── cad/
    ├── 01_facade_samurai.scad
    └── 02_boitier_corps.scad
```

Les deux fichiers `.scad` partagent le même repère de coordonnées et les
mêmes variables (`largeur_trou`, `largeur_patte`, `x_patte_trou`, etc.) — si
vous changez une dimension d'assemblage, reportez le changement dans les
deux fichiers.

## Câblage



![wiring](https://github.com/glorp-fr/sj413-clock/blob/main/wiring.jpeg)


Convention de lecture : un point noir = connexion réelle, un simple croisement de fils sans point = pas de contact (convention standard en électronique).

Points à retenir pour le câblage réel :

Masse commune obligatoire : la masse du convertisseur, celle de l'ESP32, des trois capteurs et des deux boutons doivent toutes être reliées entre elles. C'est le fil le plus souvent oublié et la cause n°1 de capteurs I2C qui « ne répondent pas ».
Bus I2C : SDA et SCL sont partagés en parallèle entre le MPU-6050 et le DS3231 — les deux modules ont des adresses I2C différentes (0x68 et 0x57/0x68 pour le DS3231 selon la carte), donc pas de conflit. La plupart des modules breakout intègrent déjà leurs résistances de tirage I2C.
DS18B20 : la résistance 4,7 kΩ entre la ligne de données et le 3V3 est indispensable si votre sonde n'a pas déjà un module avec la résistance intégrée. Le fil de la sonde étant long (1,5–2 m jusqu'au pare-chocs), placez cette résistance côté ESP32, pas côté sonde.
Boutons : une patte au GPIO, l'autre à la masse. Pas de résistance externe nécessaire — le INPUT_PULLUP du code active le tirage interne de l'ESP32.
Sens de la diode : la bague de la 1N4007 doit être orientée vers le convertisseur (côté cathode = sortie), sinon rien ne s'allume.

Une précision utile : alimentez l'ensemble par le +12V après contact (pas le +12V permanent), sinon le module tournera en permanence et videra la batterie du Samurai en quelques jours. Le DS3231 garde l'heure grâce à sa pile CR2032, donc couper l'alimentation ne pose aucun problème.

Les broches I2C par défaut (SDA/SCL déjà câblées en interne pour l'écran)
dépendent du modèle exact de carte ESP32-S3+écran — vérifiez la documentation
du vendeur avant de câbler.

## Firmware

`firmware/samurai_dashboard.ino`

Bibliothèques requises (Gestionnaire de bibliothèques Arduino) :
- `TFT_eSPI` (Bodmer)
- `Adafruit MPU6050` + `Adafruit Unified Sensor`
- `RTClib` (Adafruit)
- `OneWire` + `DallasTemperature` (Miles Burton)

**Avant de flasher :** `TFT_eSPI` doit être configuré (`User_Setup.h`) avec
le pinout exact de votre carte — chaque revendeur câble différemment le bus
SPI de l'écran. Sans ce fichier correct, l'écran reste noir même si le code
compile.

**Premier flash :** décommentez temporairement la ligne `rtc.adjust(...)`
dans `setup()` pour régler l'heure sur celle de compilation, flashez une
fois, puis recommentez-la pour éviter que l'heure ne se réinitialise à
chaque redémarrage.

Le code utilise un anti-rebond non bloquant (`millis()`, pas de `delay()`
bloquant ni de `while()`) pour rester réactif même en environnement vibrant.

## Impression 3D

- Matière : **PETG ou ASA** noir mat (le PLA se déforme l'été derrière un
  pare-brise, >60°C dans l'habitacle)
- Hauteur de couche : 0,16–0,20 mm
- Remplissage : 20–30 % (pas de pièces structurelles)
- Façade : face visible contre le plateau, pas de support nécessaire
- Boîtier : à plat, ouverture vers le haut ; supports légers utiles sous la
  découpe USB-C et le passage de câbles arrière selon le trancheur

Les deux fichiers ont été vérifiés avec OpenSCAD (export STL en maillage
2-manifold valide) avant publication.

## Assemblage

1. Souder/connecter les boutons, le MPU-6050, le RTC et la sonde DS18B20
   selon la section [Câblage](#câblage).
2. Glisser l'ensemble électronique dans le boîtier par l'avant : le PCB
   vient buter contre l'étagère interne (5 mm depuis l'avant), juste
   derrière la fenêtre de l'écran.
3. Passer le câble d'alimentation 12V/GND par le passage dédié à l'arrière,
   brancher le convertisseur step-down avec sa protection (diode + fusible)
   en amont.
4. Clipser la façade sur le boîtier : les deux pattes (haut/bas) s'enfoncent
   par friction dans les encoches. Appuyez fermement et de façon répartie
   sur toute la largeur de la façade, pas seulement sur un coin.
5. Insérer l'ensemble dans la découpe du tableau de bord — le débord de
   2,5 mm de la façade masque les bords de la découpe et les fentes des
   encoches.
6. Flasher le firmware par USB-C, régler l'heure du RTC, puis passer à
   l'alimentation 12V définitive.

## Réglages / dépannage

- **Clip trop lâche ou trop dur :** ajustez `epaisseur_patte` dans
  `cad/01_facade_samurai.scad` par pas de 0,1 mm (plus grand = plus serré)
  et réimprimez uniquement la façade — le boîtier n'a pas besoin d'être
  réimprimé.
- **Écran noir :** vérifiez la configuration `User_Setup.h` de `TFT_eSPI`
  (pinout spécifique à votre carte).
- **Heure incorrecte au démarrage :** pile CR2032 du RTC déchargée ou
  absente, ou `rtc.adjust(...)` jamais exécuté.

## Historique des révisions

- **v2** — Correction d'un maillage STL non-manifold sur la façade
  (géométrie du clip), correction d'un bug où les 4 premiers mm du boîtier
  étaient pleins par erreur (bloquait la vue de l'écran), réalignement
  numériquement vérifié des clips/encoches, clip par crochet remplacé par un
  clip par interférence (plus simple et fiable en FDM), ré-ajout du RTC et
  de la sonde de température (absents du BOM alors qu'ils faisaient partie
  du cahier des charges initial), anti-rebond du firmware rendu non
  bloquant.
- **v1** — Version initiale (ESP32-S3 + écran 1.9", MPU-6050, boutons
  silicone, clip à crochet, sans RTC ni sonde de température).
