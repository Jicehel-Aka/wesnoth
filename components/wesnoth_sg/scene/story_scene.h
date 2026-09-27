// scene/story_scene.h
#pragma once
#include "scene/scene.h"
#include "story/campaign_loader.h"

namespace wesnoth_sg {

enum class Language { French, English };

class StoryScene : public Scene {
public:
    void enter() override;
    void update(SceneManager&) override;
    void render() override;

private:
    void render_current_beat();
    void advance_to_next_beat(SceneManager&);
public:
    // reprend le récit au scénario donné (après une victoire)
    void resume_at(const std::string& id) { pending_resume_ = id; }
private:
    bool has_battle(const std::string& id) const;
    bool beat_visible(const Scenario& sc, const Beat& b) const;
    void skip_hidden(SceneManager&);
    void end_of_scenario(SceneManager&);
    void go_to(const std::string& id, SceneManager&);
    std::string pending_resume_;
    bool need_skip_ = true;
    const std::string& current_text(const Beat& beat) const;

    Campaign campaign_;
    Language language_ = Language::French;
    size_t scenario_index_ = 0;
    size_t beat_index_ = 0;
    bool loaded_ = false;
    bool beat_just_entered_ = true;

    // Pagination des textes trop longs pour la boîte (récit ou dialogue) --
    // remise à zéro à chaque nouveau beat. Voir dialog_max_lines()/
    // current_lines() dans story_scene.cpp.
    int dlg_page_ = 0;
    int dialog_max_lines(const Beat& beat) const;
    // Décalage horizontal du texte pour un beat Dialogue (kPortraitSize+14 si
    // un portrait sera dessiné, 10 sinon) -- factorisé pour que update()
    // (pagination) et render() calculent la même largeur de ligne. Sans objet
    // pour StoryScreen (marge fixe des deux côtés).
    int dialog_text_x(const Beat& beat) const;
};

StoryScene& story_scene();  // instance unique, cf. title_scene() dans Asteria

}  // namespace wesnoth_sg
