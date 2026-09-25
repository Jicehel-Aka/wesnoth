// battle/camera.h
//
// Le viewport (10x5 hexagones à 42px, voir hex_render.h) est bien plus
// petit qu'une carte Wesnoth typique (souvent 30-40+ hexagones de large).
// La caméra centre la vue sur une cible (unité sélectionnée, curseur...),
// en s'arrêtant proprement aux bords de la carte plutôt que d'afficher du
// vide hors-carte.
#pragma once
#include <algorithm>
#include "hex.h"
#include "hex_render.h"

namespace battle_render {

class Camera {
public:
    HexCoord origin{0, 0};  // hexagone monde affiché en haut-gauche de l'ecran

    // Centre la caméra sur `target`, borné aux dimensions de la carte
    // (map_w x map_h en hexagones). Si la carte est plus petite que le
    // viewport dans une dimension, l'origine reste à 0 sur cette dimension
    // (pas de scroll possible/necessaire).
    void follow(HexCoord target, int map_w, int map_h,
                int screen_w = 320, int screen_h = 240) {
        int cols = HexLayout::cols_visible(screen_w);
        int rows = HexLayout::rows_visible(screen_h);

        int max_ox = std::max(0, map_w - cols);
        int max_oy = std::max(0, map_h - rows);

        int ox = target.x - cols / 2;
        int oy = target.y - rows / 2;

        origin.x = std::clamp(ox, 0, max_ox);
        origin.y = std::clamp(oy, 0, max_oy);
    }

    // Coordonnée hexagone MONDE -> coordonnée hexagone ECRAN (celle à passer
    // à HexLayout::center pour le dessin). Renvoie aussi si le hex est dans
    // le viewport visible, pour éviter de dessiner hors-écran.
    HexCoord world_to_screen(HexCoord world) const {
        return {world.x - origin.x, world.y - origin.y};
    }

    bool is_visible(HexCoord world, int map_w, int map_h,
                     int screen_w = 320, int screen_h = 240) const {
        HexCoord s = world_to_screen(world);
        return s.x >= 0 && s.x < HexLayout::cols_visible(screen_w) &&
               s.y >= 0 && s.y < HexLayout::rows_visible(screen_h);
    }
};

}  // namespace battle_render
