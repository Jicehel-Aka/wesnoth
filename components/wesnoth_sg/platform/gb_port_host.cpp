// platform/gb_port_host.cpp — Port logiciel pour tester la vraie logique
// (StoryScene, campaign_loader, découpage de texte) sans matériel.
// Repris du patron gb_port_host.cpp d'Asteria.
#include "platform/gb_port.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include "aka_font/gb_text_render.h"

static uint16_t FB[gb::SCREEN_W*gb::SCREEN_H];
static int g_frame=0, g_budget=1;
static uint32_t g_ms=0;

// Script d'entrées injectable depuis le test (par défaut : rien).
static std::function<uint32_t(int)> g_scripted = [](int){ return 0u; };

void host_set_budget(int n){ g_budget = n; }
void host_set_input_script(std::function<uint32_t(int)> fn){ g_scripted = fn; }
static std::string g_dump_dir = ".";
void host_set_dump_dir(const char* dir){ g_dump_dir = dir; }

namespace gb {

bool running(){ return g_frame < g_budget; }
void frame_begin(){}

static int g_dump_every = 1;
void host_set_dump_every(int n){ g_dump_every = n < 1 ? 1 : n; }
static void dumpPPM(){
  if (g_dump_dir.empty() || g_frame % g_dump_every) return;
  char name[256]; snprintf(name,sizeof(name),"%s/frame_%02d.ppm",g_dump_dir.c_str(),g_frame);
  FILE* fp=fopen(name,"wb"); if(!fp) return;
  fprintf(fp,"P6\n%d %d\n255\n",SCREEN_W,SCREEN_H);
  for(int i=0;i<SCREEN_W*SCREEN_H;i++){ uint16_t c=FB[i];
    unsigned b=((c>>11)&0x1F)<<3, g=((c>>5)&0x3F)<<2, r=(c&0x1F)<<3;
    unsigned char px[3]={(unsigned char)r,(unsigned char)g,(unsigned char)b}; fwrite(px,1,3,fp); }
  fclose(fp);
}
void frame_end(){ dumpPPM(); g_frame++; g_ms+=16; }
void clear(Color c){ for(int i=0;i<SCREEN_W*SCREEN_H;i++) FB[i]=c; }
void pixel(int x,int y,Color c){ if((unsigned)x<(unsigned)SCREEN_W&&(unsigned)y<(unsigned)SCREEN_H) FB[y*SCREEN_W+x]=c; }
void fill_rect(int x,int y,int w,int h,Color c){ for(int j=0;j<h;j++)for(int i=0;i<w;i++) pixel(x+i,y+j,c); }

// Rendu via gb_text_render.h (UTF-8, police de base + glyphes accentues
// FR/DE/ES generes) -- plus le rectangle/police ASCII-only d'avant.
void text(int x,int y,const char* s,Color c){
  gb_text::draw_utf8(x, y, s, [&](int px,int py){ pixel(px,py,c); });
}
int text_width(const char* s){ return gb_text::utf8_width(s); }

static std::string mappath(const char* p){
  std::string s=p; std::string k="/sdcard/";
  if(s.rfind(k,0)==0) s = std::string(getenv("HOST_SDCARD_ROOT") ? getenv("HOST_SDCARD_ROOT") : "./sdcard_files") + "/" + s.substr(k.size());
  return s;
}
std::string sd_path(const char* p){ return mappath(p); }

// Vrai decodage BMP (24 bits non compresse) pour le mock hote aussi -- une
// fois les images generees et deposees dans sdcard_files/, les PPM de test
// les montreront reellement, pas juste "fichier trouve/pas trouve".
static bool load_and_blit(const char* path, int x, int y){
  FILE* f=fopen(mappath(path).c_str(),"rb"); if(!f) return false;
  uint8_t header[54];
  if(fread(header,1,54,f)!=54 || header[0]!='B' || header[1]!='M'){ fclose(f); return false; }
  uint32_t data_offset = header[10]|(header[11]<<8)|(header[12]<<16)|(header[13]<<24);
  int32_t  width       = header[18]|(header[19]<<8)|(header[20]<<16)|(header[21]<<24);
  int32_t  height_raw  = header[22]|(header[23]<<8)|(header[24]<<16)|(header[25]<<24);
  uint16_t bpp         = header[28]|(header[29]<<8);
  uint32_t compression = header[30]|(header[31]<<8)|(header[32]<<16)|(header[33]<<24);
  bool flip_y = height_raw>0; int32_t height = flip_y?height_raw:-height_raw;
  if(compression!=0 || bpp!=24 || width<=0 || height<=0){ fclose(f); return false; }
  int row_bytes = ((width*3+3)/4)*4;
  std::vector<uint8_t> row(row_bytes);
  fseek(f,data_offset,SEEK_SET);
  for(int32_t yy=0; yy<height; ++yy){
    if(fread(row.data(),1,row_bytes,f)!=(size_t)row_bytes){ fclose(f); return false; }
    int dst_y = flip_y ? (height-1-yy) : yy;
    for(int32_t xx=0; xx<width; ++xx){
      pixel(x+xx, y+dst_y, rgb(row[xx*3+2], row[xx*3+1], row[xx*3+0]));
    }
  }
  fclose(f);
  return true;
}
bool blit_bmp(const char* path){ return load_and_blit(path, 0, 0); }
bool draw_image(const char* path, int x, int y){ return load_and_blit(path, x, y); }
uint32_t buttons(){ return 0; }
uint32_t buttons_pressed(){ return g_scripted(g_frame); }
uint32_t millis(){ return g_ms; }
bool file_exists(const char* path){ FILE* f=fopen(mappath(path).c_str(),"rb"); if(f){fclose(f);return true;} return false; }
void return_to_loader(){}
void log(const char* m){ printf("[host] %s\n",m); }


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
