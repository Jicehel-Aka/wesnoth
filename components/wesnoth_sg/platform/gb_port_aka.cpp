#include <vector>
#include <cstring>
// platform/gb_port_aka.cpp — SEUL fichier lié à gb_graphics/gb_core.
// Patron repris directement de components/asteria/platform/gb_port_aka.cpp
// (déjà vérifié en usage réel sur d'autres jeux AKA de ce studio).
#if defined(ESP_PLATFORM)
#include "platform/gb_port.h"

#include "gb_core.h"
#include "gb_graphics.h"
#include "gb_common.h"
#include "core/input.h"
#include "aka_runtime/aka_runtime.h"
#include "aka_font/gb_text_render.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "esp_timer.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Instances globales uniques -- définies dans main.cpp, même convention que
// les autres jeux AKA de ce studio.
extern gb_core     g_core;
extern gb_graphics gfx;

namespace gb {

// core/input.h expose une Keys avec des booléens nommés (A, B, C, D, RUN,
// MENU, R1, L1, up/down/left/right) : on s'appuie directement dessus, sans
// deviner de masque brut sur g_keys.raw comme avant (source du bouton RUN
// qui ne répondait pas correctement).
//
// RUN participe par ailleurs au combo RUN+MENU qui rend la main au loader
// (aka_runtime.cpp, surveillé en continu sur g_keys.raw) : on évite de s'en
// servir seul pour une action de jeu récurrente, pour ne pas risquer de
// combiner accidentellement avec notre propre MENU (menu de bataille).
// « Unité suivante/précédente » utilise donc L1/R1, libres de toute
// affectation ailleurs dans ce jeu.
static uint32_t remap_keys(const Keys& k) {
    uint32_t out = 0;
    if (k.up) out |= BTN_UP;
    if (k.down) out |= BTN_DOWN;
    if (k.left) out |= BTN_LEFT;
    if (k.right) out |= BTN_RIGHT;
    if (k.A) out |= BTN_A;
    if (k.B) out |= BTN_B;
    if (k.MENU) out |= BTN_MENU;
    if (k.C) out |= BTN_C;
    if (k.D) out |= BTN_D;
    if (k.L1) out |= BTN_L1;
    if (k.R1) out |= BTN_R1;
    return out;
}

bool running() { return true; }

void frame_begin() {
    input_poll(g_keys);
    while (!akaRuntime.update(g_keys)) {
        vTaskDelay(pdMS_TO_TICKS(16));
        input_poll(g_keys);
    }
}

void frame_end() {
    static uint32_t s_last_ms = 0;
    const uint32_t MIN_INTERVAL_MS = 30;   // écran limité ~35fps, cf commentaire Asteria
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t elapsed = now - s_last_ms;
    if (s_last_ms != 0 && elapsed < MIN_INTERVAL_MS) {
        vTaskDelay(pdMS_TO_TICKS(MIN_INTERVAL_MS - elapsed));
    }
    gfx.update();
    s_last_ms = (uint32_t)(esp_timer_get_time() / 1000);
}

void clear(Color c) { gfx.clear(c); }

void pixel(int x, int y, Color c) {
    // IMPORTANT : gb_graphics::drawPixel ne fait AUCUN clipping fiable --
    // le bas niveau (gb_ll_lcd.c: lcd_putpixel) prend des uint16_t et ne
    // vérifie que la taille totale du framebuffer, pas les limites par
    // ligne. Un x négatif ou >= SCREEN_W déborde silencieusement sur la
    // ligne suivante (x=320 dessine en réalité en (0, y+1) -- "réapparaît
    // à gauche" comme repéré en testant la caméra). On clippe nous-mêmes
    // avant tout appel, comme le font déjà gb_port_host.cpp/gb_port_sdl.cpp.
    if ((unsigned)x >= (unsigned)SCREEN_W || (unsigned)y >= (unsigned)SCREEN_H) return;
    gfx.setColor(c);
    gfx.drawPixel((int16_t)x, (int16_t)y);
}

void fill_rect(int x, int y, int w, int h, Color c) {
    gfx.setColor(c);
    gfx.fillRect((int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h);
}

void text(int x, int y, const char* s, Color c) {
    // Dessin pixel par pixel (pas gfx.print_str : celui-ci ne connaît que la
    // police figée du composant, sans nos glyphes accentués FR/DE/ES --
    // voir gb_text_render.h). Plus lent que print_str, mais du texte narratif
    // affiché une fois par avancement de dialogue, pas par frame animée.
    gfx.setColor(c);
    gb_text::draw_utf8(x, y, s, [&](int px, int py) {
        gfx.drawPixel((int16_t)px, (int16_t)py);
    });
}

int text_width(const char* s) {
    return gb_text::utf8_width(s);
}

// Decode un BMP (24 ou 16 bits non compresse) en buffer BGR565 malloc'e.
// Partage par blit_bmp() (plein ecran) et draw_image() (position libre).
// L'appelant est responsable du free().
static uint16_t* load_bmp_buffer(const char* path, int32_t* out_w, int32_t* out_h) {
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;

    uint8_t header[54];
    if (fread(header, 1, 54, f) != 54 || header[0] != 'B' || header[1] != 'M') {
        fclose(f);
        return nullptr;
    }
    uint32_t data_offset = header[10] | (header[11] << 8) | (header[12] << 16) | (header[13] << 24);
    int32_t  width       = header[18] | (header[19] << 8) | (header[20] << 16) | (header[21] << 24);
    int32_t  height_raw  = header[22] | (header[23] << 8) | (header[24] << 16) | (header[25] << 24);
    uint16_t bpp          = header[28] | (header[29] << 8);
    uint32_t compression  = header[30] | (header[31] << 8) | (header[32] << 16) | (header[33] << 24);

    bool flip_y = height_raw > 0;
    int32_t height = flip_y ? height_raw : -height_raw;

    if (compression != 0 || (bpp != 24 && bpp != 16) || width <= 0 || height <= 0) {
        fclose(f);
        return nullptr;
    }

    uint16_t* buf = (uint16_t*)malloc((size_t)width * height * sizeof(uint16_t));
    if (!buf) { fclose(f); return nullptr; }

    int row_bytes_src = ((width * (bpp / 8) + 3) / 4) * 4;
    uint8_t* row = (uint8_t*)malloc(row_bytes_src);
    if (!row) { free(buf); fclose(f); return nullptr; }

    fseek(f, data_offset, SEEK_SET);
    for (int32_t y = 0; y < height; ++y) {
        if (fread(row, 1, row_bytes_src, f) != (size_t)row_bytes_src) {
            free(row); free(buf); fclose(f);
            return nullptr;
        }
        int dst_y = flip_y ? (height - 1 - y) : y;
        uint16_t* dst_row = buf + (size_t)dst_y * width;
        for (int32_t x = 0; x < width; ++x) {
            uint16_t c;
            if (bpp == 24) {
                uint8_t b = row[x * 3 + 0], g = row[x * 3 + 1], r = row[x * 3 + 2];
                c = rgb(r, g, b);
            } else {
                c = (uint16_t)(row[x * 2] | (row[x * 2 + 1] << 8));
            }
            dst_row[x] = c;
        }
    }
    free(row);
    fclose(f);
    *out_w = width;
    *out_h = height;
    return buf;
}

bool blit_bmp(const char* path) {
    int32_t w, h;
    uint16_t* buf = load_bmp_buffer(path, &w, &h);
    if (!buf) return false;
    gfx.drawImage(0, 0, buf, (uint16_t)w, (uint16_t)h);
    free(buf);
    return true;
}

bool draw_image(const char* path, int x, int y) {
    int32_t w, h;
    uint16_t* buf = load_bmp_buffer(path, &w, &h);
    if (!buf) return false;
    gfx.drawImage(x, y, buf, (uint16_t)w, (uint16_t)h);
    free(buf);
    return true;
}

// g_keys.raw/pressed sont des masques bruts (EXPANDER_KEY_*) ; les booléens
// nommés ne reflètent que l'état courant. On reconstruit donc un "pressed"
// (front montant) en comparant à l'état de la frame précédente.
static uint32_t s_prev = 0;
uint32_t buttons() { return remap_keys(g_keys); }
uint32_t buttons_pressed() {
    uint32_t cur = remap_keys(g_keys);
    uint32_t pressed = cur & ~s_prev;
    s_prev = cur;
    return pressed;
}
uint32_t millis()          { return (uint32_t)(esp_timer_get_time() / 1000); }

bool file_exists(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

void return_to_loader() { akaRuntime.returnToLoader(); }
std::string sd_path(const char* p) { return std::string(p); }
void log(const char* msg) { printf("[WESNOTH_SG] %s\n", msg); }


// Bloc opaque : copié ligne par ligne dans un tampon puis envoyé en une fois
// via gfx.drawImage (découpage fait par le composant) ; avec couleur clé :
// pixel par pixel (sprites, petites surfaces).
void blit565(int x, int y, int w, int h, const uint16_t* src, int stride, uint16_t key) {
    if (w <= 0 || h <= 0) return;
    if (key) {
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) {
                uint16_t c = src[j * stride + i];
                if (c != key) pixel(x + i, y + j, c);
            }
        return;
    }
    static std::vector<uint16_t> tmp;
    tmp.resize((size_t)w * h);
    for (int j = 0; j < h; ++j) memcpy(&tmp[(size_t)j * w], src + (size_t)j * stride, (size_t)w * 2);
    gfx.drawImage((int16_t)x, (int16_t)y, tmp.data(), (uint16_t)w, (uint16_t)h);
}
}  // namespace gb
#endif
