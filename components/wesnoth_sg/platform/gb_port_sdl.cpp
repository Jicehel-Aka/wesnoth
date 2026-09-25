// platform/gb_port_sdl.cpp — Port SDL2 : fenêtre 320x240 (x2), clavier,
// blit BMP depuis ./sdcard_files. Adapté de gb_port_sdl.cpp (Asteria),
// même patron déjà éprouvé, + text()/text_width() (police font8x8_basic,
// absente de l'interface d'origine).
//
// Build (sur une machine avec libsdl2-dev installé -- pas testable dans ce
// bac à sable, qui n'a que la lib SDL2 runtime, pas les headers/dev) :
//   g++ -std=c++17 -Wno-narrowing -I components/wesnoth_sg -I components/aka_font/include \
//       components/wesnoth_sg/wesnoth_app.cpp components/wesnoth_sg/scene/*.cpp \
//       components/wesnoth_sg/audio/story_audio.cpp \
//       components/wesnoth_sg/platform/gb_port_sdl.cpp \
//       components/wesnoth_sg/platform/gb_port_common.cpp \
//       components/wesnoth_sg/story/campaign_loader.cpp \
//       pc/main_sdl.cpp $(sdl2-config --cflags --libs) -lcjson \
//       -o wesnoth_sg_pc
#include "platform/gb_port.h"
#include "aka_font/gb_text_render.h"
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static uint16_t FB[gb::SCREEN_W*gb::SCREEN_H];
static SDL_Window* win=nullptr; static SDL_Renderer* ren=nullptr; static SDL_Texture* tex=nullptr;
static bool g_quit=false; static uint32_t g_held=0, g_pressed=0;
static const int SCALE=3;   // émulation PC : écran AKA 320x240 affiché en x3

static void pump(){
  g_pressed=0; SDL_Event e;
  while(SDL_PollEvent(&e)){
    if(e.type==SDL_QUIT) g_quit=true;
    if(e.type==SDL_KEYDOWN||e.type==SDL_KEYUP){
      bool d=(e.type==SDL_KEYDOWN); uint32_t b=0;
      switch(e.key.keysym.sym){
        case SDLK_UP: case SDLK_z: b=gb::BTN_UP; break;
        case SDLK_DOWN: case SDLK_s: b=gb::BTN_DOWN; break;
        case SDLK_LEFT: case SDLK_q: b=gb::BTN_LEFT; break;
        case SDLK_RIGHT: case SDLK_d: b=gb::BTN_RIGHT; break;
        // A = avancer le dialogue -- touche la plus accessible (Espace/Entree)
        case SDLK_SPACE: case SDLK_x: case SDLK_RETURN: b=gb::BTN_A; break;
        case SDLK_c: case SDLK_BACKSPACE: b=gb::BTN_B; break;
        case SDLK_RETURN2: case SDLK_ESCAPE: case SDLK_m: b=gb::BTN_MENU; break;
        // L1/R1 = unité précédente/suivante en bataille (gâchettes de la
        // console ; « n » est le raccourci équivalent dans Wesnoth sur PC)
        case SDLK_PAGEUP: case SDLK_COMMA: case SDLK_a: b=gb::BTN_L1; break;
        case SDLK_PAGEDOWN: case SDLK_PERIOD: case SDLK_TAB: case SDLK_n: b=gb::BTN_R1; break;
      }
      if(b){ if(d){ if(!(g_held&b)) g_pressed|=b; g_held|=b; } else g_held&=~b; }
    }
  }
}

namespace gb {

bool running(){ return !g_quit; }

void frame_begin(){
  if(!win){
    SDL_Init(SDL_INIT_VIDEO);
    win=SDL_CreateWindow("The South Guard (portage AKA -- PC)",
        SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,SCREEN_W*SCALE,SCREEN_H*SCALE,0);
    ren=SDL_CreateRenderer(win,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    tex=SDL_CreateTexture(ren,SDL_PIXELFORMAT_RGB565,SDL_TEXTUREACCESS_STREAMING,SCREEN_W,SCREEN_H);
  }
  pump();
}

void frame_end(){
  // FB est en BGR565 (convention AKA) ; conversion en RGB565 pour SDL.
  static uint16_t conv[SCREEN_W*SCREEN_H];
  for(int i=0;i<SCREEN_W*SCREEN_H;i++){ uint16_t c=FB[i];
    unsigned b=(c>>11)&0x1F,g=(c>>5)&0x3F,r=c&0x1F; conv[i]=(r<<11)|(g<<5)|b; }
  SDL_UpdateTexture(tex,nullptr,conv,SCREEN_W*2);
  SDL_RenderClear(ren); SDL_RenderCopy(ren,tex,nullptr,nullptr); SDL_RenderPresent(ren);
  SDL_Delay(16);
}

void clear(Color c){ for(int i=0;i<SCREEN_W*SCREEN_H;i++) FB[i]=c; }
void pixel(int x,int y,Color c){ if((unsigned)x<(unsigned)SCREEN_W&&(unsigned)y<(unsigned)SCREEN_H) FB[y*SCREEN_W+x]=c; }
void fill_rect(int x,int y,int w,int h,Color c){ for(int j=0;j<h;j++)for(int i=0;i<w;i++) pixel(x+i,y+j,c); }

// Meme rendu UTF-8 partage que le mock hote et le device (voir
// gb_text_render.h) -- glyphes accentues FR/DE/ES generes, pas de "??"
void text(int x,int y,const char* s,Color c){
  gb_text::draw_utf8(x, y, s, [&](int px,int py){ pixel(px,py,c); });
}
int text_width(const char* s){ return gb_text::utf8_width(s); }

static std::string mp(const char* p){
  std::string s=p, k="/sdcard/";
  if(s.rfind(k,0)==0) s = "./sdcard_files/" + s.substr(k.size());
  return s;
}
std::string sd_path(const char* p){ return mp(p); }

static bool load_and_blit(const char* path, int x, int y) {
  SDL_Surface* s=SDL_LoadBMP(mp(path).c_str()); if(!s) return false;
  SDL_Surface* c=SDL_ConvertSurfaceFormat(s,SDL_PIXELFORMAT_RGB24,0); SDL_FreeSurface(s); if(!c) return false;
  unsigned char* px=(unsigned char*)c->pixels;
  for(int j=0;j<c->h;j++)for(int i=0;i<c->w;i++){
    int dx=x+i, dy=y+j;
    if((unsigned)dx>=(unsigned)SCREEN_W||(unsigned)dy>=(unsigned)SCREEN_H) continue;
    unsigned char* p=px+j*c->pitch+i*3;
    FB[dy*SCREEN_W+dx]=rgb(p[0],p[1],p[2]);
  }
  SDL_FreeSurface(c); return true;
}
bool blit_bmp(const char* path){ return load_and_blit(path, 0, 0); }
bool draw_image(const char* path, int x, int y){ return load_and_blit(path, x, y); }

uint32_t buttons(){ return g_held; }
uint32_t buttons_pressed(){ return g_pressed; }
uint32_t millis(){ return SDL_GetTicks(); }
bool file_exists(const char* path){ FILE* f=fopen(mp(path).c_str(),"rb"); if(f){fclose(f);return true;} return false; }
void return_to_loader(){ g_quit=true; }
void log(const char* m){ printf("[sdl] %s\n",m); }


void blit565(int x, int y, int w, int h, const uint16_t* src, int stride, uint16_t key) {
  for (int j = 0; j < h; ++j) {
    int yy = y + j; if ((unsigned)yy >= (unsigned)SCREEN_H) continue;
    const uint16_t* s = src + j * stride;
    for (int i = 0; i < w; ++i) {
      int xx = x + i; if ((unsigned)xx >= (unsigned)SCREEN_W) continue;
      if (key && s[i] == key) continue;
      FB[yy * SCREEN_W + xx] = s[i];
    }
  }
}
}  // namespace gb
