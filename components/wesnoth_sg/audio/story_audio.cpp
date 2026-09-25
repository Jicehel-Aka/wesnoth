// audio/story_audio.cpp
#include "story_audio.h"

#if defined(ESP_PLATFORM)
#include "gb_audio_player.h"
#include "gb_audio_track_wav.h"
#include "platform/gb_port.h"

extern gb_audio_player g_audio_player;   // défini dans main.cpp, partagé

namespace story_audio {
namespace {
gb_audio_track_wav g_narration_track;
bool g_registered = false;
}

void init() {
    if (!g_registered) {
        g_audio_player.add_track(&g_narration_track, /*f32_volume=*/1.0f);
        g_registered = true;
    }
}

bool play_line(const std::string& sd_path) {
    if (g_narration_track.is_playing()) {
        gb::log(("voie narration deja occupee, ligne ignoree: " + sd_path).c_str());
        return false;
    }
    int rc = g_narration_track.play_wav(sd_path.c_str());
    return rc == 0;
}

bool is_playing() { return g_narration_track.is_playing(); }
void stop() { g_narration_track.stop_playing(); }

}  // namespace story_audio

#else  // build hôte : pas de vrai composant audio, stub silencieux

#include <cstdio>

namespace story_audio {
namespace {
bool g_active = false;
}
void init() {}
bool play_line(const std::string& sd_path) {
    printf("[host] play_line(%s)\n", sd_path.c_str());
    g_active = true;
    return true;
}
bool is_playing() { return g_active; }
void stop() { g_active = false; }
}  // namespace story_audio

#endif
