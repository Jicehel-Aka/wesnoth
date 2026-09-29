// scene/story_scene.h
#pragma once
#include <vector>

#include "scene/campaign_root.h"
#include "scene/scene.h"
#include "scene/language.h"
#include "story/campaign_loader.h"
#include "story/savegame.h"

namespace wesnoth_sg {

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

    // --- choix de campagne (nouveau, avant le menu titre) : liste kCampaigns
    // (scene/campaign_root.h). Une fois choisie, g_campaign_root est fixé et
    // campaign_bilingual.json/manifest.json de CETTE campagne sont chargés
    // (voir enter() -- le chargement n'a plus lieu tant qu'aucun choix n'a
    // été fait, contrairement à l'ancienne version mono-campagne).
    bool campaign_chosen_ = false;
    int campaign_sel_ = 0;
    void update_campaign_select(SceneManager&);
    void render_campaign_select();
    void load_chosen_campaign();

    // --- menu titre (Nouvelle partie / Continuer / Charger une sauvegarde) --
    // affiché une fois la campagne choisie, avant tout beat -- voir
    // story/savegame.h pour ce que "Continuer" (autosave) et les 5
    // emplacements restaurent (indépendants par campagne, cf. sd_root()).
    enum class TitleMode { Menu, LoadList };
    bool at_title_ = true;
    TitleMode title_mode_ = TitleMode::Menu;
    int title_sel_ = 0;
    std::vector<SaveSlotInfo> load_list_;
    void update_title(SceneManager&);
    void render_title();
    void start_new_game();
    void start_from_save(int slot, SceneManager&);   // slot = kAutoSlot ou 1..kNumManualSlots
};

StoryScene& story_scene();  // instance unique, cf. title_scene() dans Asteria

}  // namespace wesnoth_sg
