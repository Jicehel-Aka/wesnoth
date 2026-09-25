// story_player.cpp
#include "story_player.h"

#include "core/story_audio.h"
#include "core/story_gfx.h"
#include "esp_log.h"

static const char* TAG = "story_player";

StoryPlayer::StoryPlayer(Campaign campaign, Language lang)
    : campaign_(std::move(campaign)), language_(lang) {
    finished_ = campaign_.scenarios.empty();
}

const std::string& StoryPlayer::current_text(const Beat& beat) const {
    // Priorité à la langue choisie ; repli sur l'anglais si la traduction
    // française est absente (translated=false côté JSON) plutôt que
    // d'afficher une chaîne vide — mieux vaut du texte dans l'autre langue
    // qu'un écran blanc.
    if (language_ == Language::French && beat.has_fr) {
        return beat.text_fr;
    }
    return beat.text_en;
}

void StoryPlayer::render_current_beat() {
    if (finished_) return;
    const Scenario& scenario = campaign_.scenarios[scenario_index_];
    const Beat& beat = scenario.beats[beat_index_];

    switch (beat.type) {
        case BeatType::StoryScreen:
            story_gfx::draw_story_screen(beat.image, current_text(beat), beat.show_title);
            break;
        case BeatType::Dialogue:
            story_gfx::draw_dialogue(beat.speaker, current_text(beat), beat.image);
            break;
        case BeatType::Objectives:
            // TODO: mise en page dédiée liste d'objectifs (pas juste du texte
            // brut) — pour l'instant on ne bloque pas dessus, l'important est
            // l'ordre de passage, pas le rendu final ici.
            break;
        case BeatType::CharacterEnters:
            // Beat silencieux pour l'instant (pas de texte à afficher tel
            // quel) — pourrait déclencher une animation d'entrée plus tard.
            break;
    }

    if (beat_just_entered_) {
        // Seule la langue anglaise a de l'audio généré pour l'instant (voir
        // /tts/manifest.json) — si beat.audio_path est vide (ex: pas encore
        // synthétisé, ou langue française sans moteur TTS disponible), on
        // n'appelle simplement pas play_line et le texte reste seul à
        // l'écran. Pas de crash, pas de silence-erreur : juste pas de voix.
        if (!beat.audio_path.empty()) {
            story_audio::play_line(beat.audio_path);
        }
        beat_just_entered_ = false;
    }
}

void StoryPlayer::advance_to_next_beat() {
    Scenario& scenario = campaign_.scenarios[scenario_index_];
    story_audio::stop();  // on ne laisse jamais une voix chevaucher le beat suivant

    if (beat_index_ + 1 < scenario.beats.size()) {
        beat_index_++;
    } else {
        // Fin de scénario : cherche le suivant par id (next_scenario) parmi
        // ceux déjà chargés en RAM. Si absent (branche a/b non résolue, ou
        // scénario pas encore récupéré côté outil Python), on s'arrête
        // proprement plutôt que de planter.
        bool found = false;
        for (size_t i = 0; i < campaign_.scenarios.size(); ++i) {
            if (campaign_.scenarios[i].id == scenario.next_scenario) {
                scenario_index_ = i;
                beat_index_ = 0;
                found = true;
                break;
            }
        }
        if (!found) {
            ESP_LOGI(TAG, "Fin de campagne chargee (next_scenario='%s' introuvable)",
                     scenario.next_scenario.c_str());
            finished_ = true;
        }
    }
    beat_just_entered_ = true;
}

void StoryPlayer::update() {
    if (finished_) return;

    render_current_beat();

    if (story_gfx::advance_pressed()) {
        advance_to_next_beat();
    }
}
