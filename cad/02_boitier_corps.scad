// ============================================================
// MODULE HORLOGE / INCLINOMÈTRE SUZUKI SAMURAI SJ413
// Pièce 2/2 : CORPS DU BOÎTIER ARRIÈRE — 02_boitier_corps.scad
// Carte : ESP32-S3 avec écran IPS 1.9" intégré (170x320, ST7789)
// ============================================================
// IMPORTANT — repère PARTAGÉ avec 01_facade_samurai.scad :
// (0,0) = coin bas-gauche de la DÉCOUPE DU TABLEAU DE BORD.
// Ce fichier est construit directement dans ce repère (pas de
// débord à soustraire). Les positions des encoches ci-dessous
// utilisent les MÊMES variables "x_patte_trou / largeur_patte /
// longueur_patte" que la façade — gardez-les synchronisées si
// vous modifiez l'une ou l'autre pièce.
// ============================================================

$fn = 40;

// --- Dimensions (doivent rester identiques à celles de la façade) ---
largeur_trou       = 63.0;
hauteur_trou       = 42.0;
profondeur_boitier = 45.0;
epaisseur_paroi    = 2.4;

// --- Étagère interne (ledge) qui retient le PCB contre la vitre ---
// Sur les premiers "profondeur_ledge" mm, l'ouverture est réduite de
// epaisseur_ledge en plus de l'épaisseur de paroi normale : le PCB,
// glissé depuis l'avant, bute sur ce rebord et s'arrête net derrière
// la fenêtre de l'écran.
profondeur_ledge = 5.0;
epaisseur_ledge  = 2.0;

// --- Port USB-C (accès arrière, pour prog./alimentation directe) ---
largeur_usb = 12.0;
hauteur_usb = 7.0;

// --- Encoches d'assemblage (DOIVENT matcher la façade) ---
largeur_patte   = 15.0;
epaisseur_patte = 1.9;
longueur_patte  = 4.0;                              // reste dans la zone d'étagère (< profondeur_ledge)
x_patte_trou    = (largeur_trou - largeur_patte)/2;  // 24.0 — centré, identique à la façade
jeu_largeur     = 0.4;                               // jeu latéral autour de la patte

module rectangle_arrondi(l, h, r) {
    hull() {
        translate([r, r]) circle(r=r);
        translate([l-r, r]) circle(r=r);
        translate([l-r, h-r]) circle(r=r);
        translate([r, h-r]) circle(r=r);
    }
}

module boitier_corps() {
    difference() {
        // Corps principal qui s'enfonce dans le tableau de bord
        linear_extrude(height = profondeur_boitier)
            rectangle_arrondi(largeur_trou, hauteur_trou, 3.0);

        // --- Cavité principale (profonde), paroi standard ---
        translate([epaisseur_paroi, epaisseur_paroi, profondeur_ledge])
            linear_extrude(height = profondeur_boitier - profondeur_ledge + 1)
                rectangle_arrondi(
                    largeur_trou - 2*epaisseur_paroi,
                    hauteur_trou - 2*epaisseur_paroi, 1.5);

        // --- Cavité avant (étagère), ouverture réduite pour retenir le PCB ---
        translate([epaisseur_paroi + epaisseur_ledge, epaisseur_paroi + epaisseur_ledge, -1])
            linear_extrude(height = profondeur_ledge + 1)
                rectangle_arrondi(
                    largeur_trou - 2*(epaisseur_paroi + epaisseur_ledge),
                    hauteur_trou - 2*(epaisseur_paroi + epaisseur_ledge), 1.0);

        // Accès port USB-C au dos (centré, aligné sur le connecteur de la carte)
        translate([largeur_trou/2 - largeur_usb/2, 8.0, profondeur_boitier - 5.0])
            cube([largeur_usb, hauteur_usb, 10.0]);

        // Passage des câbles d'alimentation 12V / GND (régulateur -> faisceau)
        translate([largeur_trou/2 - 8.0, hauteur_trou - 14.0, profondeur_boitier - 5.0])
            cube([16.0, 8.0, 10.0]);

        // --- Encoche haute (reçoit la patte haute de la façade) ---
        // Traverse toute l'épaisseur de la paroi (haut) pour un ajustement
        // par friction — la façade déborde du trou et masque cette fente.
        translate([x_patte_trou - jeu_largeur/2, -2, -0.5])
            cube([largeur_patte + jeu_largeur, 3.0, longueur_patte + 1]);

        // --- Encoche basse (reçoit la patte basse de la façade) ---
        translate([x_patte_trou - jeu_largeur/2, hauteur_trou - 1.0, -0.5])
            cube([largeur_patte + jeu_largeur, 3.0, longueur_patte + 1]);

        // --- Fentes de refroidissement pour l'ESP32-S3 ---
        // En dehors de la zone centrale des encoches (x 22 à 41) et de
        // la zone d'étagère avant : positionnées sur les côtés, plus
        // profondes dans le boîtier (z > 12).
        for (i = [0:2]) {
            translate([4 + i*4, hauteur_trou - epaisseur_paroi - 1, 12])
                cube([2.2, epaisseur_paroi + 2, 25]);
            translate([largeur_trou - 16 + i*4, hauteur_trou - epaisseur_paroi - 1, 12])
                cube([2.2, epaisseur_paroi + 2, 25]);
            translate([4 + i*4, -1, 12])
                cube([2.2, epaisseur_paroi + 2, 25]);
            translate([largeur_trou - 16 + i*4, -1, 12])
                cube([2.2, epaisseur_paroi + 2, 25]);
        }
    }
}

boitier_corps();
