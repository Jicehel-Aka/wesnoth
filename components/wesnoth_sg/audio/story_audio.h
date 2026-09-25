// audio/story_audio.h
//
// Réserve UNE voie du g_audio_player partagé (AUDIO_PLAYER_TRACK_COUNT=4,
// cf. gb_audio_player.h) pour la narration, séparée de la musique/des
// effets. API confirmée contre gb_audio_track_wav.h / .cpp réels :
//   - play_wav(path) est non bloquant : il ouvre le fichier et positionne
//     l'état, la lecture avance ensuite via play_callback(), appelé par
//     g_audio_player.pool() (tâche dédiée cœur 0, cf. main.cpp).
//   - is_playing() redevient false tout seul quand le fichier est épuisé.
//   - Le format attendu est strictement 44,1kHz/mono/16 bits (vérifié par
//     check_file_format) -- exactement ce que produit desormais
//     generate_audio.py.
#pragma once

#include <string>

namespace story_audio {

// À appeler une fois au démarrage, après que g_audio_player existe (créé et
// possédé par main.cpp, comme g_core/gfx).
void init();

bool play_line(const std::string& sd_path);
bool is_playing();
void stop();

}  // namespace story_audio
