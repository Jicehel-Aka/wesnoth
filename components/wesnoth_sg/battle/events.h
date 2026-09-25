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
};

struct EventMessage {
    std::string speaker;     // nom affiché (vide = narrateur)
    std::string portrait;    // chemin /sdcard/... (.bmp) ou vide
    std::string text;
    std::vector<std::string> options;
    int unit_uid = 0;
};

class EventEngine {
public:
    explicit EventEngine(Game& g) : g_(g) {}
    // scenario_json : le JSON du scénario (clés "events" et "intro_messages")
    void load(const std::string& scenario_json);
    void attach();                               // branche le crochet de Game
    void fire(const std::string& name, int uid1, int uid2, HexCoord where);

    bool has_message() const { return !queue_.empty(); }
    const EventMessage& message() const { return queue_.front(); }
    void pop_message();                          // message lu (sans option)
    void choose(int option);                     // message à options

    const std::map<std::string, int>& unsupported() const { return unsupported_; }
    int fired_count() const { return fired_; }

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
    std::vector<const WNode*> pending_options_;
    Ctx pending_ctx_;
    bool waiting_ = false, running_ = false;
    std::map<std::string, std::string> vars_;
    std::map<std::string, int> unsupported_;
    int fired_ = 0;
};

}  // namespace wsg
