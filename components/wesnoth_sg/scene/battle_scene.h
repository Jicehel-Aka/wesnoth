// scene/battle_scene.h — écran de bataille (moteur wsg::Game + rendu AKA).
#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "battle/campaign_state.h"
#include "battle/events.h"
#include "battle/game.h"
#include "battle/wdata.h"
#include "scene/language.h"
#include "scene/scene.h"

namespace wesnoth_sg {

struct Aki {                       // image .aki (voir tools/wesnoth_data/gen_assets.py)
    int w = 0, h = 0;
    std::vector<uint16_t> px;
    bool load(const std::string& sd_path);
};

class BattleScene : public Scene {
public:
    void enter() override;
    void update(SceneManager&) override;
    void render() override;
    // Scénario à charger (id = nom du JSON dans data/scenarios/)
    void set_scenario(const std::string& id) { scenario_id_ = id; loaded_ = false; resume_from_autosave_ = false; }
    // Comme set_scenario(), mais reprend l'état sauvegardé (save_auto.json)
    // au lieu de démarrer le scénario à zéro -- utilisé par le menu titre
    // ("Continuer") et par le mode autoplay des tests quand un autosave
    // correspond au scénario demandé.
    void resume_from_autosave(const std::string& id) { scenario_id_ = id; loaded_ = false; resume_from_autosave_ = true; }
    wsg::Game* game() { return game_.get(); }

private:
    enum class Mode { Idle, Selected, Attack, Recruit, Menu, Objectives, Ai, Over, SaveMenu };

    bool load();
    void build_canvas();
    const Aki* sprite(const wsg::Unit& u);
    void hex_center(HexCoord h, int& cx, int& cy) const;   // coordonnées canevas
    void follow(HexCoord h);
    void move_cursor(int dx, int dy);
    void on_a();
    void on_b();
    void open_attack(int attacker_uid, int target_uid, HexCoord from);
    void refresh_reach();
    void build_recruit_list();   // fusionne recrues neuves et rappels disponibles
    void next_unit(int dir = 1);   // +1 = suivante, -1 = précédente
    void start_ai_if_needed();
    void draw_hex_outline(HexCoord h, uint16_t c);
    void draw_units();
    void draw_hud();
    void draw_panel();
    void draw_menu(const std::vector<std::string>& items, int sel, const char* title);
    void draw_attack();

    std::string scenario_id_ = "01_Born_to_the_Banner";
    std::string scenario_name_;   // champ "name" du JSON, pour l'affichage des sauvegardes
    wsg::GameData data_;
    bool data_ok_ = false;
    std::unique_ptr<wsg::Game> game_;
    std::unique_ptr<wsg::EventEngine> events_;
    std::string next_scenario_;
    bool end_fired_ = false;
    bool resume_from_autosave_ = false;
    // Sauvegarde automatique (emplacement 0) : à chaque fin de tour (joueur
    // ou IA) et à chaque fin de scénario -- voir savegame.h. Ne fait rien si
    // un message est en attente (état incomplet) ou si la partie est finie.
    void autosave();
    int save_slot_sel_ = 0;             // curseur du menu "Sauvegarder" (0..4 = emplacements 1..5)
    std::string save_feedback_;         // confirmation affichée brièvement après une sauvegarde manuelle
    void draw_save_menu();
    int dlg_sel_ = 0;
    int dlg_page_ = 0;   // pagination des messages trop longs pour la boîte (voir draw_dialog)
    // Langue courante (même enum que StoryScene, cf. scene/language.h) --
    // relue depuis akaRuntime à chaque frame comme dans story_scene.cpp,
    // pour rester cohérente si le joueur la change depuis le menu système
    // pendant qu'une bataille est en cours.
    Language language_ = Language::French;
    // Doublage des messages d'ouverture (intro_messages, cf. events.h) :
    // vrai tant que le message courant (front de la file) n'a pas encore
    // déclenché sa lecture -- remis à vrai à chaque nouveau message (pop_
    // message()/choose()) et au chargement du scénario.
    bool msg_audio_pending_ = true;
    const std::string& current_text(const wsg::EventMessage& m) const;
    void draw_dialog();
    // Nombre de lignes de texte de dialogue qui tiennent dans la boîte fixe
    // (h=104, cf. draw_dialog) sans déborder de l'écran -- partagé entre
    // update() (pour savoir s'il reste une page) et draw_dialog() (pour ne
    // dessiner que la page courante).
    int dialog_max_lines() const;
    // Décalage horizontal du texte (106 si un portrait existe et sera
    // dessiné, 6 sinon) -- factorisé pour que update() (pagination) et
    // draw_dialog() (rendu) calculent exactement la même largeur de ligne.
    int dialog_text_x(const wsg::EventMessage& m) const;
    bool check_end();
    bool loaded_ = false;
    std::vector<uint16_t> canvas_;
    int cw_ = 0, ch_ = 0;
    int cam_x_ = 0, cam_y_ = 0;
    std::unordered_map<std::string, Aki> sprites_;
    HexCoord cursor_{1, 1};
    Mode mode_ = Mode::Idle;
    int sel_uid_ = 0;
    std::unordered_map<int, wsg::PathNode> reach_;
    // attaque
    int att_uid_ = 0, def_uid_ = 0, weapon_ = 0;
    HexCoord att_from_;
    std::vector<int> weapons_;
    int menu_sel_ = 0;
    std::vector<std::string> menu_items_;
    // Liste combinée affichée par le menu Recruter/Rappeler : les premières
    // entrées sont les types recrutables du camp, les suivantes des soldats
    // du roster reporté (voir campaign_state.h) encore disponibles.
    std::vector<std::string> recruit_names_;
    std::vector<int> recruit_cost_;
    std::vector<int> recruit_roster_idx_;  // -1 = recrue neuve, sinon index dans campaign_state().roster
    // Les troupes génériques du roster n'ont pas d'id WML (seuls les
    // personnages de l'histoire en ont un) : on ne peut donc pas les
    // reconnaître sur la carte pour éviter de proposer deux fois la même
    // entrée. On retient donc nous-mêmes, pour ce scénario, les indices déjà
    // rappelés.
    std::vector<int> recalled_roster_idx_;
    int ai_timer_ = 0;
    int repeat_ = 0;
    std::string log_;
};

BattleScene& battle_scene();

}  // namespace wesnoth_sg
