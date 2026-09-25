// battle/hex_render.h
//
// Conversion hex <-> pixel et primitives de dessin de la grille, pour
// l'écran 320x240. Taille d'hexagone fixée à 42px après comparaison
// visuelle (24/32/40/48 testés -- voir test_host/test_hex_size.cpp) :
// bon compromis lisibilité des unités / nombre d'hexagones visibles.
//
// Header-only comme aka_font -- ne dépend que de gb:: (pixel) et hex.h,
// utilisable aussi bien par le mock hôte que par le vrai device.
#pragma once
#include <cmath>
#include <cstdint>
#include "hex.h"

namespace battle_render {

constexpr int HEX_W = 42;
constexpr int HEX_H = (int)(HEX_W * 1.05f);  // meme ratio que le test valide

struct HexLayout {
    int origin_x = HEX_W/2 + 4;
    int origin_y = HEX_H/2 + 4;

    void center(HexCoord h, int& cx, int& cy) const {
        cx = origin_x + h.x * (HEX_W * 3 / 4);
        cy = origin_y + h.y * HEX_H + (h.x & 1 ? HEX_H/2 : 0);
    }

    // Nombre d'hexagones visibles sur un viewport donné (pour calculer le
    // défilement de caméra une fois la carte plus grande que l'écran).
    static int cols_visible(int screen_w) { return (screen_w - 8) / (HEX_W*3/4); }
    static int rows_visible(int screen_h) { return (screen_h - 8) / HEX_H; }
};

// Dessine le contour d'un hexagone (pointy-top) via une fonction de pixel
// fournie par l'appelant -- reste indépendant de la façade gb:: pour rester
// testable hors device (voir test_hex_size.cpp).
template <typename PixelFn>
void draw_hex_outline(const HexLayout& L, HexCoord h, PixelFn set_pixel) {
    int cx, cy; L.center(h, cx, cy);
    float pts_x[6], pts_y[6];
    for (int i = 0; i < 6; i++) {
        float ang = (float)M_PI / 180.0f * (60*i - 30);
        pts_x[i] = cx + (HEX_W/2.0f) * cosf(ang);
        pts_y[i] = cy + (HEX_H/2.0f) * sinf(ang);
    }
    for (int i = 0; i < 6; i++) {
        int x0=(int)pts_x[i], y0=(int)pts_y[i];
        int x1=(int)pts_x[(i+1)%6], y1=(int)pts_y[(i+1)%6];
        int dx=abs(x1-x0), dy=-abs(y1-y0);
        int sx = x0<x1?1:-1, sy = y0<y1?1:-1;
        int err = dx+dy;
        while (true) {
            set_pixel(x0, y0);
            if (x0==x1 && y0==y1) break;
            int e2 = 2*err;
            if (e2>=dy) { err+=dy; x0+=sx; }
            if (e2<=dx) { err+=dx; y0+=sy; }
        }
    }
}

template <typename PixelFn>
void fill_circle(int cx, int cy, int r, PixelFn set_pixel) {
    for (int y=-r; y<=r; y++)
        for (int x=-r; x<=r; x++)
            if (x*x+y*y <= r*r) set_pixel(cx+x, cy+y);
}

}  // namespace battle_render
