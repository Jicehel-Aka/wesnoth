// audio/story_audio_sdl.cpp — Lecture audio réelle pour la version PC/SDL.
// À COMPILER À LA PLACE de audio/story_audio.cpp pour la cible SDL (pas en
// plus : les deux définissent le même namespace story_audio).
//
// Approche volontairement simple (un seul device de sortie, une file audio
// remplacée à chaque nouvelle réplique) -- suffisant pour de la narration
// séquentielle (jamais deux voix en même temps), pas un vrai mixeur.
#include "story_audio.h"
#include "platform/gb_port.h"
#include <SDL2/SDL.h>
#include <cstdio>

namespace story_audio {
namespace {
SDL_AudioDeviceID g_dev = 0;
bool g_active = false;

void audio_callback(void*, Uint8*, int) {}  // inutilisé (on pousse via QueueAudio)
}

void init() {
    if (SDL_WasInit(SDL_INIT_AUDIO) == 0) {
        SDL_InitSubSystem(SDL_INIT_AUDIO);
    }
}

bool play_line(const std::string& sd_path) {
    // Meme convention de chemin que gb_port_sdl.cpp (mp()) : /sdcard/ ->
    // ./sdcard_files/. Dupliquee ici faute d'API exposee par la façade pour
    // cette conversion (elle est privée à gb_port_sdl.cpp) -- a factoriser
    // si un troisieme endroit en a besoin un jour.
    std::string path = gb::sd_path(sd_path.c_str());

    SDL_AudioSpec spec;
    Uint8* buf = nullptr;
    Uint32 len = 0;
    if (!SDL_LoadWAV(path.c_str(), &spec, &buf, &len)) {
        printf("[sdl-audio] echec chargement %s : %s\n", path.c_str(), SDL_GetError());
        return false;
    }

    if (g_dev) {
        SDL_CloseAudioDevice(g_dev);
        g_dev = 0;
    }
    g_dev = SDL_OpenAudioDevice(nullptr, 0, &spec, nullptr, 0);
    if (!g_dev) {
        SDL_FreeWAV(buf);
        return false;
    }
    SDL_QueueAudio(g_dev, buf, len);
    SDL_PauseAudioDevice(g_dev, 0);
    SDL_FreeWAV(buf);   // SDL_QueueAudio copie les données, libération immédiate OK
    g_active = true;
    return true;
}

bool is_playing() {
    if (!g_dev) return false;
    g_active = SDL_GetQueuedAudioSize(g_dev) > 0;
    return g_active;
}

void stop() {
    if (g_dev) {
        SDL_ClearQueuedAudio(g_dev);
    }
    g_active = false;
}

}  // namespace story_audio
