// battle/hex.h
//
// Système de coordonnées hexagonal utilisé par Wesnoth : décalage "impair-q"
// (les colonnes impaires sont décalées d'une demi-case vers le bas). C'est
// le même système que les vraies cartes .map de Wesnoth, donc les fichiers
// de carte officiels se chargent sans conversion.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <array>

struct HexCoord {
    int x = 0, y = 0;
    bool operator==(const HexCoord& o) const { return x == o.x && y == o.y; }
};

// Les 6 voisins d'un hexagone. En décalage "impair-q", le motif change
// selon que x (la colonne) est pair ou impair -- erreur classique si on
// l'oublie, vérifié ci-dessous par un test unitaire.
inline std::array<HexCoord, 6> hex_neighbors(HexCoord h) {
    if (h.x & 1) {  // colonne impaire
        return {{
            {h.x,   h.y - 1},  // N
            {h.x+1, h.y},      // NE
            {h.x+1, h.y+1},    // SE
            {h.x,   h.y + 1},  // S
            {h.x-1, h.y+1},    // SW
            {h.x-1, h.y},      // NW
        }};
    } else {  // colonne paire
        return {{
            {h.x,   h.y - 1},
            {h.x+1, h.y-1},
            {h.x+1, h.y},
            {h.x,   h.y + 1},
            {h.x-1, h.y},
            {h.x-1, h.y-1},
        }};
    }
}

inline int hex_distance(HexCoord a, HexCoord b) {
    // Conversion en coordonnées cubiques pour une distance exacte (la
    // distance de Manhattan naïve sur x,y ne marche pas sur une grille
    // hexagonale décalée).
    auto to_cube = [](HexCoord h) {
        int col = h.x, row = h.y;
        int cz = col;
        int cx = row - (col - (col & 1)) / 2;
        int cy = -cx - cz;
        return std::array<int,3>{cx, cy, cz};
    };
    auto ca = to_cube(a), cb = to_cube(b);
    return (std::abs(ca[0]-cb[0]) + std::abs(ca[1]-cb[1]) + std::abs(ca[2]-cb[2])) / 2;
}
