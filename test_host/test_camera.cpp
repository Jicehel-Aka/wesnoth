// test_camera.cpp — Valide visuellement battle/camera.h sur une carte plus
// grande que le viewport (20x12, contre 10x5 visibles), avec une unité qui
// traverse la carte : la caméra doit suivre, puis se bloquer proprement
// aux bords plutôt que de montrer du vide.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include "battle/hex.h"
#include "battle/hex_render.h"
#include "battle/camera.h"
#include "battle/battle_map.h"
#include "aka_font/gb_text_render.h"

static const int SCREEN_W = 320, SCREEN_H = 240;
static uint16_t FB[320*240];

static uint16_t rgb(int r,int g,int b){ return ((b>>3)<<11)|((g>>2)<<5)|(r>>3); }
static void pixel(int x,int y,uint16_t c){ if((unsigned)x<320&&(unsigned)y<240) FB[y*320+x]=c; }

static void draw_unit_marker(const battle_render::HexLayout& L, HexCoord screen_h, uint16_t team_color, char letter) {
    int cx, cy; L.center(screen_h, cx, cy);
    int r = battle_render::HEX_W * 0.32f;
    battle_render::fill_circle(cx, cy, r, [&](int x,int y){ pixel(x,y,team_color); });
    for (int a=0;a<360;a+=4){
        float rad = a * 3.14159f/180.0f;
        pixel(cx+(int)(r*cosf(rad)), cy+(int)(r*sinf(rad)), rgb(0,0,0));
    }
    char s[2] = {letter, 0};
    gb_text::draw_utf8(cx-4, cy-4, s, [&](int x,int y){ pixel(x,y,rgb(255,255,255)); });
}

static void save_ppm(const char* path){
    FILE* f=fopen(path,"wb");
    fprintf(f,"P6\n%d %d\n255\n",SCREEN_W,SCREEN_H);
    for(int i=0;i<SCREEN_W*SCREEN_H;i++){
        uint16_t c=FB[i];
        unsigned b=((c>>11)&0x1F)<<3, g=((c>>5)&0x3F)<<2, r=(c&0x1F)<<3;
        unsigned char px[3]={(unsigned char)r,(unsigned char)g,(unsigned char)b};
        fwrite(px,1,3,f);
    }
    fclose(f);
}

// Construit une carte de test 20x12 (bien plus grande que le viewport
// 10x5) -- pas encore la vraie carte du scenario, juste assez grande pour
// prouver que le defilement fonctionne.
static std::string make_test_map(int w, int h) {
    std::string out;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            out += ((x+y*3) % 5 == 0) ? "Hh" : "Gg";
            if (x < w-1) out += ",";
        }
        out += "\n";
    }
    return out;
}

int main(){
    BattleMap map;
    map.load_from_string(make_test_map(20, 12));
    printf("Carte de test : %dx%d hexagones\n", map.width, map.height);

    battle_render::HexLayout L;
    battle_render::Camera cam;

    // L'unité traverse la carte de gauche a droite : (1,6) -> (18,6)
    HexCoord positions[] = {{1,6}, {5,6}, {10,6}, {14,6}, {18,6}};

    for (int frame = 0; frame < 5; frame++) {
        HexCoord unit_world_pos = positions[frame];
        cam.follow(unit_world_pos, map.width, map.height);

        memset(FB, 0, sizeof(FB));
        int cols = battle_render::HexLayout::cols_visible(SCREEN_W);
        int rows = battle_render::HexLayout::rows_visible(SCREEN_H);
        uint16_t grass = rgb(90,150,70), hill = rgb(150,130,80);

        for (int sy = 0; sy < rows; sy++) for (int sx = 0; sx < cols; sx++) {
            HexCoord world{sx + cam.origin.x, sy + cam.origin.y};
            if (!map.in_bounds(world)) continue;
            TerrainClass tc = map.terrain_at(world);
            uint16_t c = (tc == TerrainClass::Hills) ? hill : grass;
            battle_render::draw_hex_outline(L, {sx,sy}, [&](int px,int py){ pixel(px,py,c); });
        }

        HexCoord screen_pos = cam.world_to_screen(unit_world_pos);
        draw_unit_marker(L, screen_pos, rgb(50,90,220), 'D');

        char path[128];
        snprintf(path, sizeof(path), "/tmp/camera_frame_%d.ppm", frame);
        save_ppm(path);
        printf("frame %d: unite monde=(%d,%d) camera_origin=(%d,%d) ecran=(%d,%d)\n",
               frame, unit_world_pos.x, unit_world_pos.y, cam.origin.x, cam.origin.y,
               screen_pos.x, screen_pos.y);
    }
    return 0;
}
