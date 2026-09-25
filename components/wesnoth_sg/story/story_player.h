// story_player.h
//
// Fait avancer la campagne chargée (Campaign) beat par beat, dans l'ordre du
// JSON — c'est-à-dire dans l'ordre exact où Wesnoth les joue, garantissant le
// "débit d'histoire" voulu. Ne connaît rien de gb_graphics/gb_audio en
// direct : passe par story_gfx et story_audio.
#pragma once

#include "campaign_loader.h"

enum class Language { French, English };

class StoryPlayer {
public:
    explicit StoryPlayer(Campaign campaign, Language lang = Language::French);

    // À appeler une fois par frame (boucle unique 40 FPS de app_main.cpp).
    // Gère l'avancement (bouton "suite"), le lancement audio, l'affichage.
    void update();

    // true quand on a atteint la fin de la campagne chargée (dernier beat du
    // dernier scénario, ou next_scenario introuvable — cf. "missing" du JSON
    // de chain_campaign côté outil Python).
    bool finished() const { return finished_; }

    void set_language(Language lang) { language_ = lang; }
    Language language() const { return language_; }

private:
    void render_current_beat();
    void advance_to_next_beat();
    const std::string& current_text(const Beat& beat) const;

    Campaign campaign_;
    Language language_;
    size_t scenario_index_ = 0;
    size_t beat_index_ = 0;
    bool finished_ = false;
    bool beat_just_entered_ = true;  // pour ne lancer l'audio qu'une fois par beat
};
