// battle/events.h — interpréteur embarqué des événements WML d'un scénario.
//
// Les événements sont exportés par tools/wesnoth_data/extract_scenario.py
// (arbre WML développé, macros comprises) et rejoués ici quand le moteur
// (wsg::Game) signale « moveto », « attack », « die », « side 1 turn end »...
//
// Pris en charge :
//  déclencheurs : noms de Wesnoth (turn N, new turn, side N turn [M] [end],
//    side turn, turn end, turn refresh, moveto, attack, attack end,
//    last breath, die, recruit, victory...) + [fire_event] ; first_time_only ;
//    [filter], [filter_second], [filter_condition].
//  filtres d'unité : id, side, type, race, role, canrecruit, level, x, y
//    (listes et plages), [not] [and] [or], [filter_adjacent], [filter_location].
//  conditions : [variable] (equals, not_equals, numerical_*, greater/less_*),
//    [have_unit] (count), [not] [and] [or], [if]/[then]/[else]/[elseif].
//  actions : [message] (avec [option]/[command]), [unit], [kill],
//    [modify_unit], [move_unit], [teleport], [heal_unit], [gold],
//    [modify_side], [modify_turns], [objectives], [endlevel],
//    [capture_village], [set_variable], [clear_variable],
//    [store_time_of_day], [fire_event], [event], [remove_event],
//    [micro_ai] zone_guardian.
//  Le reste (animations, sons, astuces, menus...) est ignoré et compté.
#pragma once
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "battle/game.h"

struct cJSON;

namespace wsg {

struct WNode {
    std::string tag;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::vector<WNode> kids;
    const std::string* find(const std::string& k) const;
    std::string get(const std::string& k, const std::string& d = "") const;
    const WNode* child(const std::string& t) const;
    static WNode from_json(const cJSON* j);
    // Inverse de from_json -- sert à ré-exporter les événements ajoutés
    // dynamiquement en jeu ([event] imbriqué, cf. add_event()) au moment de
    // la sauvegarde. L'appelant possède l'objet renvoyé (à insérer dans un
    // parent cJSON, ou cJSON_Delete()).
    cJSON* to_json() const;
};

struct EventMessage {
    std::string speaker;     // nom affiché (vide = narrateur)
    std::string portrait;    // chemin /sdcard/... (.bmp) ou vide
    std::string text;
    std::string text_fr;     // traduction française, vide si non doublée (voir load())
    // Doublage : uniquement pour les messages d'ouverture (intro_messages),
    // qui forment une séquence fixe et indexée -- les messages déclenchés
    // dynamiquement en jeu (die/moveto/...) restent muets (texte seul), le
    // chemin n'a de sens que pour un index de scénario stable. Vide si ce
    // message n'a pas de doublage (cf. tools/wesnoth_data/generate_audio.py).
    std::string audio_path, audio_path_fr;   // chemins relatifs à /sdcard/WESNOTH_SG/audio/
    std::vector<std::string> options;
    // Traductions des choix de dialogue [option], parallèle à "options" (même
    // index) ; entrée vide si ce choix précis n'a pas de "message_fr"/"label_fr"
    // -- repli sur l'anglais au cas par cas (voir choix affichés).
    std::vector<std::string> options_fr;
    int unit_uid = 0;
};

class EventEngine {
public:
    explicit EventEngine(Game& g) : g_(g) {}
    // scenario_json : le JSON du scénario (clés "events" et "intro_messages").
    // queue_intro_messages=false lors de la reprise d'une sauvegarde en
    // cours de bataille : les messages d'ouverture ont déjà été joués (et
    // leurs éventuels effets déjà appliqués via les événements "start"/
    // "prestart" qu'on ne refire pas non plus, cf. story/savegame.cpp) --
    // seule la structure "events" doit être (re)connue du moteur.
    void load(const std::string& scenario_json, bool queue_intro_messages = true);
    void attach();                               // branche le crochet de Game
    void fire(const std::string& name, int uid1, int uid2, HexCoord where);

    bool has_message() const { return !queue_.empty(); }
    const EventMessage& message() const { return queue_.front(); }
    void pop_message();                          // message lu (sans option)
    void choose(int option);                     // message à options
    // Incrémenté à chaque message affiché (pop_message/choose) : sert à
    // BattleScene pour savoir quand invalider son cache de mise en page du
    // dialogue (retour à la ligne, portrait...) plutôt que de le recalculer
    // à chaque frame -- voir battle_scene.cpp.
    int message_version() const { return msg_version_; }

    const std::map<std::string, int>& unsupported() const { return unsupported_; }
    int fired_count() const { return fired_; }

    // --- sauvegarde --------------------------------------------------------
    // Sérialise events_ (y compris les événements ajoutés dynamiquement en
    // jeu via [event], cf. add_event()) et vars_ ([variable]/[set_variable]).
    // La file de messages en attente (queue_) n'est PAS sauvegardée : les
    // points de sauvegarde (fin de tour, fin de scénario) sont toujours
    // choisis quand elle est vide (has_message() == false).
    void save_state(cJSON* out) const;
    // Réapplique un état sauvegardé : reconstruit events_ par des appels à
    // add_event() (pour recalculer noms/once exactement comme au chargement
    // normal), dans le même ordre qu'à la sauvegarde, puis restaure le
    // fanion dead de chacun. À appeler juste après load() (voir son
    // paramètre queue_intro_messages) et avant toute reprise du jeu.
    void load_state(const cJSON* in);

private:
    struct Ctx { int u1 = 0, u2 = 0; HexCoord loc{-1, -1}; };
    struct Ev { WNode node; std::vector<std::string> names; std::string id; bool once = true, dead = false; };
    struct Frame { const std::vector<WNode>* list; size_t i; Ctx ctx; };

    void add_event(const WNode& n);
    void run();
    void push(const std::vector<WNode>& list, const Ctx& c) { stack_.push_back({&list, 0, c}); }
    void exec(const WNode& n, const Ctx& c);
    bool cond(const WNode& n, const Ctx& c);
    bool match_unit(const WNode& f, const Unit& u, const Ctx& c);
    std::vector<Unit*> find_units(const WNode& f, const Ctx& c);
    std::string subst(const std::string& s, const Ctx& c);
    void queue_message(const WNode& n, const Ctx& c);
    std::string portrait_for(const Unit* u, const std::string& image) const;

    Game& g_;
    std::deque<Ev> events_;        // deque : adresses stables pendant les ajouts
    std::vector<Frame> stack_;
    std::deque<std::vector<WNode>> owned_;   // listes créées en cours de route
    std::vector<EventMessage> queue_;
    int msg_version_ = 0;
    std::vector<const WNode*> pending_options_;
    Ctx pending_ctx_;
    bool waiting_ = false, running_ = false;
    std::map<std::string, std::string> vars_;
    std::map<std::string, int> unsupported_;
    int fired_ = 0;
};

}  // namespace wsg
