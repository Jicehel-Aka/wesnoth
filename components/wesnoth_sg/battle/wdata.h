// battle/wdata.h — données de jeu Wesnoth chargées depuis les JSON générés
// par tools/wesnoth_data/gen_data.py (units.json, terrain.json).
//
// Rien n'est codé en dur ici : statistiques, types de mouvement, résistances,
// alias de terrain viennent des vrais fichiers de Wesnoth.
#pragma once
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace wsg {

constexpr int UNREACHABLE = 99;   // coût Wesnoth d'un terrain interdit

enum class Align { Lawful, Chaotic, Neutral, Liminal };

struct AttackDef {
    std::string name, type, range;
    int dmg = 0, num = 0;
    std::vector<std::string> spec;
    bool has(const std::string& s) const;
    bool melee() const { return range == "melee"; }
};

struct Movetype {
    std::map<std::string, int> costs, defense, resist;
    bool flying = false;
};

struct UnitTypeDef {
    std::string id, name, race, movetype;
    int hp = 1, mp = 0, xp = 0, level = 0, cost = 0;
    int num_traits = -1;              // -1 : valeur de la race
    bool ignore_race_traits = false;
    Align align = Align::Neutral;
    bool amla = false;
    std::vector<std::string> adv, abilities, musthave, extra_traits;
    std::vector<AttackDef> attacks;
    std::map<std::string, int> costs, defense, resist;   // surcharges du type
    bool has_ability(const std::string& prefix) const;   // "heals" -> heals4/heals8
    int ability_value(const std::string& prefix) const;  // heals8 -> 8
};

struct TerrExpr {
    int op = 0;                        // 0 feuille, 1 meilleur, 2 pire
    std::string leaf;
    std::vector<TerrExpr> args;
};

struct TerrainDef {
    TerrExpr mvt, def;
    bool village = false, castle = false, keep = false;
    int heals = 0;
    std::string name;
    std::string display;               // terrain dominant (pour la couleur)
};

struct Race {
    int num_traits = 0;
    bool ignore_global = false;
    std::vector<std::string> traits;
};

struct TraitEffect {
    int melee = 0, ranged = 0, hp = 0, hp_pct = 0, hp_per_level = 0, mp = 0, xp_pct = 0;
    int village_def_cap = 0;
    bool loyal = false, fearless = false, always_rest_heal = false;
    bool immune_poison = false, immune_drain = false, immune_plague = false;
};

class GameData {
public:
    bool load(const std::string& units_json, const std::string& terrain_json);

    const UnitTypeDef* type(const std::string& id) const;
    const TerrainDef& terrain(const std::string& code) const;
    const Race* race(const std::string& id) const;
    const TraitEffect* trait(const std::string& id) const;
    const std::vector<std::string>& global_traits() const { return global_traits_; }

    // Valeurs effectives pour un type d'unité sur un code de terrain
    int move_cost(const UnitTypeDef& t, const std::string& code) const;
    int defense(const UnitTypeDef& t, const std::string& code) const;   // % de chance d'être touché
    int resistance(const UnitTypeDef& t, const std::string& dmg_type) const;  // % de dégâts subis

    size_t type_count() const { return types_.size(); }

private:
    int leaf_value(const UnitTypeDef& t, const std::string& leaf, bool movement) const;
    int eval(const UnitTypeDef& t, const TerrExpr& e, bool movement) const;

    std::unordered_map<std::string, UnitTypeDef> types_;
    std::unordered_map<std::string, Movetype> movetypes_;
    std::unordered_map<std::string, TerrainDef> terrain_;
    std::unordered_map<std::string, Race> races_;
    std::unordered_map<std::string, TraitEffect> traits_;
    std::vector<std::string> global_traits_;
    TerrainDef unknown_terrain_;
    mutable std::unordered_map<std::string, int> cache_;
};

}  // namespace wsg
