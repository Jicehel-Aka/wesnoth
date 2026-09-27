// battle/game.h — état d'une bataille et règles de Wesnoth.
//
// Règles reprises de Wesnoth (1.18) : moment de la journée et alignements,
// zones de contrôle, coûts de déplacement et défense par type de mouvement,
// résistances, arrondi des dégâts, spéciales d'arme (marksman, magical,
// firststrike, charge, backstab, poison, slow, drain, berserk, plague, swarm),
// capacités (heals, cures, regenerates, leadership, skirmisher, illuminates,
// steadfast), traits, XP et montée de niveau (y compris AMLA), soins
// (village, repos, soigneurs, poison), revenu et entretien, capture de
// villages, recrutement depuis le donjon.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "battle/hex.h"
#include "battle/campaign_state.h"
#include "battle/wdata.h"

struct cJSON;

namespace wsg {

struct Unit {
    int uid = 0;
    std::string type_id, id, name, role;
    const UnitTypeDef* t = nullptr;
    int side = 1;
    HexCoord pos;
    int hp = 1, max_hp = 1, mp = 0, max_mp = 0, xp = 0, max_xp = 1;
    int melee_bonus = 0, ranged_bonus = 0, dmg_bonus = 0;
    std::vector<std::string> traits;
    bool canrecruit = false, guardian = false, loyal = false, fearless = false;
    bool immune_poison = false, immune_drain = false, immune_plague = false;
    bool always_rest_heal = false;
    int village_def_cap = 0;
    int zone_x = -1, zone_y = -1, zone_r = 0;   // micro-IA zone_guardian
    bool poisoned = false, slowed = false;
    // Marque un simple soldat recruté (ou déjà rappelé) : c'est lui qui
    // apparaît dans le menu « Rappeler » du joueur d'un scénario à l'autre,
    // à la différence des personnages de l'histoire (Deoran...), toujours
    // ramenés par le WML du scénario via [recall] id=... (voir campaign_state.h).
    bool recallable = false;
    bool attacked = false;       // a attaqué ce tour-ci
    bool resting = true;         // ni déplacé ni attaqué depuis son dernier tour
    bool alive() const { return hp > 0; }
    int level() const { return t ? t->level : 0; }
};

struct Side {
    int id = 0;
    std::vector<std::string> teams;
    std::string color;
    bool human = false, hidden = false;
    int gold = 0, income_mod = 0;
    std::vector<std::string> recruit;
};

struct Village { HexCoord pos; int owner = 0; };

// Résultat détaillé d'un assaut (prévision exacte ou résultat réel)
struct CombatSide {
    int weapon = -1;            // index d'arme, -1 = pas de riposte
    int damage = 0, strikes = 0, cth = 0;
    double p_dead = 0, exp_hp = 0;
    bool slows = false, poisons = false, drains = false, firststrike = false;
};
struct CombatPreview { CombatSide att, def; bool berserk = false; };

struct PathNode { int cost = 0; int prev = -1; };

enum class Outcome { None, Victory, Defeat };

// Point d'accroche des événements WML : nom, unité principale, secondaire,
// case. Appelé de façon synchrone ; l'appelant relit ses unités par uid.
using EventHook = std::function<void(const std::string& name, int uid1, int uid2, HexCoord where)>;

class Game {
public:
    explicit Game(const GameData& d) : d_(d) {}

    bool load_scenario(const std::string& scenario_json, const std::string& map_text);
    void begin();   // à appeler une fois le crochet d'événements branché

    // --- carte ---------------------------------------------------------
    int width() const { return w_; }
    int height() const { return h_; }
    // Case jouable : la première et la dernière ligne/colonne d'une carte
    // Wesnoth sont une bordure (affichée, mais jamais occupée).
    bool in_map(HexCoord h) const { return h.x >= 1 && h.y >= 1 && h.x < w_ - 1 && h.y < h_ - 1; }
    bool in_data(HexCoord h) const { return h.x >= 0 && h.y >= 0 && h.x < w_ && h.y < h_; }
    const std::string& code(HexCoord h) const;
    const TerrainDef& terrain(HexCoord h) const { return d_.terrain(code(h)); }
    int village_owner(HexCoord h) const;
    int key(HexCoord h) const { return h.y * w_ + h.x; }
    HexCoord unkey(int k) const { return {k % w_, k / w_}; }

    // --- camps et unités -------------------------------------------------
    std::vector<Unit>& units() { return units_; }
    const std::vector<Unit>& units() const { return units_; }
    Unit* unit_at(HexCoord h);
    Unit* unit_by_id(const std::string& id);
    Unit* unit_by_uid(int uid);
    Side* side(int id);
    const std::vector<Side>& sides() const { return sides_; }
    bool allied(int a, int b) const;
    int village_count(int side) const;
    int upkeep(int side) const;
    int income(int side) const;

    // --- temps ----------------------------------------------------------
    int turn() const { return turn_; }
    int turn_limit() const { return turns_; }
    int current_side() const { return cur_side_; }
    int tod_index() const;
    const char* tod_name() const;
    int lawful_bonus_at(HexCoord h) const;   // avec illuminates

    // --- règles -----------------------------------------------------------
    Unit make_unit(const std::string& type_id, int side, HexCoord pos,
                   const std::vector<std::string>& forced_traits, bool random_traits);
    int  defense_of(const Unit& u, HexCoord h) const;       // chance d'être touché
    bool in_enemy_zoc(const Unit& u, HexCoord h) const;
    std::unordered_map<int, PathNode> reach(const Unit& u) const;
    std::vector<HexCoord> path_to(const std::unordered_map<int, PathNode>& r, HexCoord to) const;
    // Déplace le long du chemin ; renvoie false si rien n'a bougé
    bool move_unit(Unit& u, HexCoord to);

    int  best_defender_weapon(const Unit& att, int att_weapon, const Unit& def, HexCoord att_from) const;
    CombatPreview preview(const Unit& att, int att_weapon, const Unit& def, HexCoord att_from) const;
    // Résout l'assaut ; renvoie un résumé lisible
    std::string attack(Unit& att, int att_weapon, Unit& def);

    std::vector<HexCoord> recruit_hexes(const Unit& leader) const;
    bool recruit(int side, const std::string& type_id, HexCoord where);

    void end_turn_of_current_side();     // passe au camp suivant (et au tour suivant)
    Outcome outcome() const { return outcome_; }
    const std::string& outcome_reason() const { return outcome_reason_; }
    const std::string& last_log() const { return log_; }

    uint32_t rand_u32();

    // --- pour l'interpréteur d'événements -------------------------------------
    void set_hook(EventHook h) { hook_ = std::move(h); }
    Unit* spawn(Unit u);                          // place une unité (case libre la plus proche)
    void set_outcome(Outcome o, const std::string& why) { if (outcome_ == Outcome::None) { outcome_ = o; outcome_reason_ = why; } }
    void set_objectives(std::vector<std::string> o) { objectives_ = std::move(o); }
    void set_turn_limit(int t) { turns_ = t; }
    void set_turn(int t) { turn_ = t; }
    void capture(HexCoord h, int side);
    const std::string& tod_id() const;
    bool has_live_id(const std::string& id) const;     // unité vivante avec cet id ?
    void set_terrain(HexCoord h, const std::string& code);  // [terrain] / MODIFY_TERRAIN
    bool gold_carryover_bonus() const { return gc_bonus_; }
    int gold_carryover_percent() const { return gc_percent_; }
    int compute_carryover_gold(int side = 1) const;    // à appeler à la victoire
    std::vector<PersistentUnit> harvest_roster(int side = 1) const;  // survivants -> report
    // Reconstruit une unité depuis une entrée persistante (PV/XP/traits
    // exacts) et la pose au plus près de `where` ; ne touche pas à l'or --
    // à l'appelant de facturer le coût (gratuit pour [recall] scripté,
    // payant pour le menu du joueur). Partagé par events.cpp et BattleScene.
    Unit* place_recall(const PersistentUnit& p, HexCoord where);
    static constexpr int kRecallCost = 20;   // coût par défaut de Wesnoth (recall_cost du camp non exporté ici)
    void kill_unit(Unit& victim, Unit* killer);   // avec « last breath » / « die »

    const GameData& data() const { return d_; }
    const std::vector<std::string>& objectives() const { return objectives_; }

    // --- sauvegarde ------------------------------------------------------
    // Sérialise l'état mutable de la partie en cours (unités, camps, villages,
    // tour courant...) dans un objet cJSON déjà créé par l'appelant (voir
    // story/savegame.cpp). À appeler après load_scenario()+begin() : la carte,
    // les types d'unité et les identifiants de victoire/défaite du scénario
    // ne sont PAS sauvegardés (ils sont ré-obtenus en rechargeant le même
    // scenario_json au moment du chargement).
    void save_state(cJSON* out) const;
    // Réapplique un état précédemment sauvegardé par save_state(), par-dessus
    // une partie déjà chargée (load_scenario()+begin() avec le MÊME
    // scenario_json) : remplace units_/sides_[].gold/villages_/tour/etc.
    void load_state(const cJSON* in);

private:
    void start_side_turn(int side);
    void heal_side(int side);
    void check_outcome();
    void advance_if_needed(Unit& u);
    void apply_type(Unit& u, const UnitTypeDef* t, bool full_heal);
    int  strike_damage(const Unit& a, const AttackDef& w, const Unit& d, HexCoord a_pos,
                       HexCoord d_pos, bool charge, bool backstab) const;
    int  leadership_bonus(const Unit& u, HexCoord at) const;
    bool backstab_active(const Unit& att, HexCoord from, const Unit& def) const;
    int  strikes_of(const Unit& u, const AttackDef& w) const;
    void kill(Unit& victim, Unit* killer, bool plague);

    const GameData& d_;
    int w_ = 0, h_ = 0;
    std::vector<std::string> map_;
    std::vector<Village> villages_;
    std::vector<Unit> units_;
    std::vector<Side> sides_;
    int next_uid_ = 1;
    int turn_ = 1, start_turn_ = 1, turns_ = -1, cur_side_ = 1, tod_start_ = 0, xp_mod_ = 100;
    std::vector<int> lawful_;              // horaire du scénario ([time] lawful_bonus)
    std::vector<std::string> tod_ids_;
    std::vector<std::string> victory_ids_, defeat_ids_;
    bool gc_bonus_ = false;
    int gc_percent_ = 80;
    std::vector<std::string> objectives_;
    Outcome outcome_ = Outcome::None;
    std::string outcome_reason_, log_;
    uint32_t rng_ = 0x5EED1234u;
    EventHook hook_;
    void fire(const std::string& n, int a = 0, int b = 0, HexCoord w = {-1, -1}) { if (hook_) hook_(n, a, b, w); }
};

// Arrondi des dégâts de Wesnoth (round_damage) : au plus proche, les cas
// d'égalité vers la valeur de base, minimum 1.
int round_damage(int base, int bonus, int divisor);

}  // namespace wsg
