// platform/gb_port.h — Façade plateforme, patron identique à celui déjà
// utilisé et testé sur d'autres portages AKA de ce studio (Asteria). Reprise
// volontairement à l'identique pour la partie commune : device implémente
// dans gb_port_aka.cpp (via gb_graphics/gb_core réels), PC dans
// gb_port_host.cpp (pour tester la logique sans matériel).
//
// Ajout par rapport au patron d'origine : text()/text_width()/text_wrapped()
// -- nos beats sont de longs paragraphes de dialogue, il faut le retour à la
// ligne automatique que gb_graphics::print_str ne fait pas lui-même.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace gb {
typedef uint16_t Color;                 // BGR565 (convention AKA)
inline Color rgb(uint8_t r,uint8_t g,uint8_t b){ return (Color)(((b>>3)<<11)|((g>>2)<<5)|(r>>3)); }
constexpr int SCREEN_W=320, SCREEN_H=240;
enum Button:uint32_t{ BTN_UP=1,BTN_DOWN=2,BTN_LEFT=4,BTN_RIGHT=8,BTN_A=16,BTN_B=32,BTN_MENU=64,BTN_RUN=128,
                       BTN_L1=256,BTN_R1=512,BTN_C=1024,BTN_D=2048 };

bool     running();
void     frame_begin();
void     frame_end();
void     clear(Color c);
void     pixel(int x,int y,Color c);
void     fill_rect(int x,int y,int w,int h,Color c);
bool     blit_bmp(const char* path);
// Dessine un BMP (24 bits) à une position arbitraire (pas juste plein
// écran) -- utilisé pour les portraits de dialogue. Renvoie false si le
// fichier est introuvable ou invalide ; l'appelant doit alors se contenter
// de ne rien dessiner plutôt que de planter.
bool     draw_image(const char* path, int x, int y);
uint32_t buttons();
uint32_t buttons_pressed();
uint32_t millis();
bool     file_exists(const char* path);
// Convertit un chemin logique "/sdcard/..." en chemin reel de la plateforme
// (identite sur AKA, ./sdcard_files/... sur PC). A utiliser pour TOUT fopen
// direct fait hors de la facade (JSON, cartes, audio SDL).
std::string sd_path(const char* path);
// Copie un bloc de pixels (Color) à l'écran, avec découpage aux bords.
// stride = largeur d'une ligne de la source. key : couleur transparente
// (0 = opaque). Utilisé pour la carte pré-rendue et les sprites .aki.
void blit565(int x, int y, int w, int h, const uint16_t* src, int stride, uint16_t key);
void     return_to_loader();
void     log(const char* msg);

// --- Texte (ajout wesnoth_sg) ---
// Dessine une ligne simple à (x,y), police fixe 8x8 (gb_graphics::print_str
// côté device — voir gb_port_aka.cpp). Pas de retour à la ligne.
void     text(int x, int y, const char* s, Color c);
// Largeur en pixels d'une chaîne pour cette police (8px/caractère, monospace
// -- font8x8_basic du composant gamebuino).
int      text_width(const char* s);
// Découpe `s` en lignes tenant dans max_width_px et les dessine à partir de
// (x,y), line_height_px entre chaque ligne. Renvoie le nombre de lignes
// dessinées (utile pour savoir où placer ce qui vient après, ex. le nom du
// personnage au-dessus). Coupe uniquement aux espaces (pas de césure).
int      text_wrapped(int x, int y, int max_width_px, int line_height_px,
                       const std::string& s, Color c);

}  // namespace gb
