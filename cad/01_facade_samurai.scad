// ============================================================
// MODULE HORLOGE / INCLINOMÈTRE SUZUKI SAMURAI SJ413
// Pièce 1/2 : FAÇADE AVANT — 01_facade_samurai.scad
// Carte : ESP32-S3 avec écran IPS 1.9" intégré (170x320, ST7789)
// Assemblage : pattes rectangulaires en interférence (pas de vis)
// ============================================================
// IMPORTANT — repère PARTAGÉ avec 02_boitier_corps.scad :
// (0,0) = coin bas-gauche de la DÉCOUPE DU TABLEAU DE BORD
// (63x42mm), PAS le coin de la façade elle-même. La façade
// déborde donc de -rebord_facade à largeur_trou+rebord_facade.
// Toute position de patte/bouton/écran ci-dessous est exprimée
// en coordonnées "trou", puis translatée de +rebord_facade pour
// entrer dans le repère local du panneau. Cela permet de copier
// telles quelles les mêmes coordonnées "trou" dans le fichier du
// boîtier sans recalcul.
// ============================================================

$fn = 40;

// --- Dimensions de la découpe d'origine du tableau de bord ---
largeur_trou  = 63.0;
hauteur_trou  = 42.0;
rebord_facade = 2.5;               // débord de la collerette autour du trou

L_facade = largeur_trou + (rebord_facade * 2); // 68 mm
H_facade = hauteur_trou + (rebord_facade * 2); // 47 mm
epaisseur_facade = 3.0;

// --- Zone d'affichage utile de l'écran 1.9" (coord. "trou") ---
largeur_ecran     = 42.5;
hauteur_ecran     = 23.0;
x_ecran_trou      = 3.5;                              // marge à gauche
y_ecran_trou      = (hauteur_trou - hauteur_ecran)/2;  // centré verticalement

// --- Boutons poussoirs silicone 8x8x5mm (jeu de perçage 0.2mm) ---
largeur_bouton = 8.2;
hauteur_bouton = 8.2;
x_bouton_trou      = 50.0;
y_bouton_haut_trou = 9.5;
y_bouton_bas_trou  = 24.3;

// --- Pattes d'assemblage en interférence (clips sans vis) ---
// Cohérent avec 02_boitier_corps.scad :
largeur_patte   = 15.0;
epaisseur_patte = 1.9;   // légèrement > l'ouverture de l'encoche -> interférence
longueur_patte  = 4.0;   // reste dans la zone d'étagère avant du boîtier (voir profondeur_ledge)
x_patte_trou    = (largeur_trou - largeur_patte) / 2;   // centré en largeur : 24.0

module rectangle_arrondi(l, h, r) {
    hull() {
        translate([r, r]) circle(r=r);
        translate([l-r, r]) circle(r=r);
        translate([l-r, h-r]) circle(r=r);
        translate([r, h-r]) circle(r=r);
    }
}

// Patte simple avec léger chanfrein d'entrée (reste manifold via hull)
module patte_interference(largeur, epaisseur, longueur) {
    hull() {
        cube([largeur, epaisseur, 0.1]);                    // base, collée à la façade
        translate([0.6, 0, longueur])
            cube([largeur - 1.2, epaisseur, 0.1]);            // bout chanfreiné, guide l'insertion
    }
}

module facade_1_9() {
    difference() {
        union() {
            // Cadre principal de façade
            linear_extrude(height = epaisseur_facade)
                rectangle_arrondi(L_facade, H_facade, 3.0);

            // Patte haute (s'enfonce dans l'encoche haute du boîtier)
            translate([rebord_facade + x_patte_trou, rebord_facade - epaisseur_patte + 0.4, epaisseur_facade])
                patte_interference(largeur_patte, epaisseur_patte, longueur_patte);

            // Patte basse (s'enfonce dans l'encoche basse du boîtier)
            translate([rebord_facade + x_patte_trou, rebord_facade + hauteur_trou - 0.4, epaisseur_facade])
                patte_interference(largeur_patte, epaisseur_patte, longueur_patte);
        }

        // Fenêtre centrale pour l'écran
        translate([rebord_facade + x_ecran_trou, rebord_facade + y_ecran_trou, -1])
            linear_extrude(height = epaisseur_facade + 6)
                rectangle_arrondi(largeur_ecran, hauteur_ecran, 1.5);

        // Bouton 1 (haut) : cycle des modes d'affichage
        translate([rebord_facade + x_bouton_trou, rebord_facade + y_bouton_haut_trou, -1])
            linear_extrude(height = epaisseur_facade + 6)
                rectangle_arrondi(largeur_bouton, hauteur_bouton, 1.0);

        // Bouton 2 (bas) : calibrage / réglage
        translate([rebord_facade + x_bouton_trou, rebord_facade + y_bouton_bas_trou, -1])
            linear_extrude(height = epaisseur_facade + 6)
                rectangle_arrondi(largeur_bouton, hauteur_bouton, 1.0);
    }
}

facade_1_9();
