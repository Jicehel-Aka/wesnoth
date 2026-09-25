// test_hex_size.cpp — Valide visuellement battle/hex_render.h (42px,
// valeur officielle retenue) plutôt qu'un code de test dupliqué.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "battle/hex.h"
#include "battle/hex_render.h"
#include "aka_font/gb_text_render.h"

static const int SCREEN_W = 320, SCREEN_H = 240;
static uint16_t FB[320*240];

static uint16_t rgb(int r,int g,int b){ return ((b>>3)<<11)|((g>>2)<<5)|(r>>3); }
static void pixel(int x,int y,uint16_t c){ if((unsigned)x<320&&(unsigned)y<240) FB[y*320+x]=c; }

static void draw_unit_marker(const battle_render::HexLayout& L, HexCoord h, uint16_t team_color, char letter) {
    int cx, cy; L.center(h, cx, cy);
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

int main(){
    memset(FB, 0, sizeof(FB));
    battle_render::HexLayout L;

    int cols = battle_render::HexLayout::cols_visible(SCREEN_W);
    int rows = battle_render::HexLayout::rows_visible(SCREEN_H);

    uint16_t grass = rgb(90,150,70), hill = rgb(150,130,80);
    for (int x=0; x<cols; x++) for (int y=0; y<rows; y++) {
        uint16_t c = (x+y)%3==0 ? hill : grass;
        battle_render::draw_hex_outline(L, {x,y}, [&](int px,int py){ pixel(px,py,c); });
    }
    draw_unit_marker(L, {2,2}, rgb(50,90,220), 'D');
    draw_unit_marker(L, {3,2}, rgb(50,90,220), 'S');
    draw_unit_marker(L, {6,3}, rgb(220,60,50), 'B');
    draw_unit_marker(L, {7,1}, rgb(220,60,50), 'T');

    save_ppm("/tmp/hex_42_final.ppm");
    printf("HEX_W=%d (officiel) -> %dx%d hexagones visibles\n", battle_render::HEX_W, cols, rows);
    return 0;
}
