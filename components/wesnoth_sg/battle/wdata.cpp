// battle/wdata.cpp
#include "battle/wdata.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "cJSON.h"

namespace wsg {

bool AttackDef::has(const std::string& s) const {
    return std::find(spec.begin(), spec.end(), s) != spec.end();
}

bool UnitTypeDef::has_ability(const std::string& prefix) const {
    for (const auto& a : abilities)
        if (a.compare(0, prefix.size(), prefix) == 0) return true;
    return false;
}

int UnitTypeDef::ability_value(const std::string& prefix) const {
    int best = 0;
    for (const auto& a : abilities) {
        if (a.compare(0, prefix.size(), prefix) != 0) continue;
        const char* p = a.c_str() + prefix.size();
        while (*p && (*p < '0' || *p > '9')) ++p;
        best = std::max(best, *p ? atoi(p) : 1);
    }
    return best;
}

namespace {

std::string str(const cJSON* o, const char* k, const char* d = "") {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
    return (v && cJSON_IsString(v) && v->valuestring) ? v->valuestring : d;
}
int num(const cJSON* o, const char* k, int d = 0) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
    return (v && cJSON_IsNumber(v)) ? v->valueint : d;
}
std::vector<std::string> strs(const cJSON* o, const char* k) {
    std::vector<std::string> r;
    const cJSON* a = cJSON_GetObjectItemCaseSensitive(o, k);
    const cJSON* e = nullptr;
    cJSON_ArrayForEach(e, a) if (cJSON_IsString(e)) r.push_back(e->valuestring);
    return r;
}
std::map<std::string, int> intmap(const cJSON* o, const char* k) {
    std::map<std::string, int> r;
    const cJSON* m = cJSON_GetObjectItemCaseSensitive(o, k);
    const cJSON* e = nullptr;
    cJSON_ArrayForEach(e, m) if (cJSON_IsNumber(e)) r[e->string] = e->valueint;
    return r;
}

TerrExpr parse_expr(const cJSON* v) {
    TerrExpr e;
    if (!v) { e.leaf = "impassable"; return e; }
    if (cJSON_IsString(v)) { e.leaf = v->valuestring; return e; }
    const cJSON* best = cJSON_GetObjectItemCaseSensitive(v, "best");
    const cJSON* worst = cJSON_GetObjectItemCaseSensitive(v, "worst");
    const cJSON* arr = best ? best : worst;
    e.op = best ? 1 : 2;
    const cJSON* x = nullptr;
    cJSON_ArrayForEach(x, arr) e.args.push_back(parse_expr(x));
    return e;
}

std::string last_leaf(const TerrExpr& e) {
    if (e.op == 0) return e.leaf;
    return e.args.empty() ? std::string("flat") : last_leaf(e.args.back());
}

}  // namespace

bool GameData::load(const std::string& units_json, const std::string& terrain_json) {
    cJSON* u = cJSON_Parse(units_json.c_str());
    if (!u) return false;
    const cJSON* e = nullptr;

    cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(u, "movetypes")) {
        Movetype m;
        m.costs = intmap(e, "costs");
        m.defense = intmap(e, "defense");
        m.resist = intmap(e, "resist");
        m.flying = num(e, "flying") != 0;
        movetypes_[e->string] = std::move(m);
    }
    cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(u, "races")) {
        Race r;
        r.num_traits = num(e, "num_traits");
        r.ignore_global = num(e, "ignore_global") != 0;
        r.traits = strs(e, "traits");
        races_[e->string] = std::move(r);
    }
    global_traits_ = strs(u, "global_traits");
    cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(u, "trait_effects")) {
        TraitEffect t;
        t.melee = num(e, "melee_damage");
        t.ranged = num(e, "ranged_damage");
        t.hp = num(e, "hp");
        t.hp_pct = num(e, "hp_percent");
        t.hp_per_level = num(e, "hp_per_level");
        t.mp = num(e, "mp");
        t.xp_pct = num(e, "xp_percent");
        t.village_def_cap = num(e, "village_defense_cap");
        t.loyal = num(e, "loyal") != 0;
        t.fearless = num(e, "fearless") != 0;
        t.always_rest_heal = num(e, "always_rest_heal") != 0;
        t.immune_poison = num(e, "immune_poison") != 0;
        t.immune_drain = num(e, "immune_drain") != 0;
        t.immune_plague = num(e, "immune_plague") != 0;
        traits_[e->string] = t;
    }
    cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(u, "types")) {
        UnitTypeDef t;
        t.id = e->string;
        t.name = str(e, "name", e->string);
        t.name_fr = str(e, "name_fr", "");  // absent -> reste en anglais (voir uname())
        t.race = str(e, "race");
        t.movetype = str(e, "movetype", "smallfoot");
        t.hp = num(e, "hp", 1);
        t.mp = num(e, "mp");
        t.xp = num(e, "xp", 100);
        t.level = num(e, "level");
        t.cost = num(e, "cost");
        t.num_traits = num(e, "num_traits", -1);
        t.ignore_race_traits = num(e, "ignore_race_traits") != 0;
        std::string al = str(e, "alignment", "neutral");
        t.align = al == "lawful" ? Align::Lawful : al == "chaotic" ? Align::Chaotic
                : al == "liminal" ? Align::Liminal : Align::Neutral;
        t.amla = num(e, "amla") != 0;
        t.adv = strs(e, "adv");
        t.abilities = strs(e, "abilities");
        t.musthave = strs(e, "musthave");
        t.extra_traits = strs(e, "extra_traits");
        t.costs = intmap(e, "costs");
        t.defense = intmap(e, "defense");
        t.resist = intmap(e, "resist");
        const cJSON* a = nullptr;
        cJSON_ArrayForEach(a, cJSON_GetObjectItemCaseSensitive(e, "attacks")) {
            AttackDef ad;
            ad.name = str(a, "name");
            ad.type = str(a, "type", "blade");
            ad.range = str(a, "range", "melee");
            ad.dmg = num(a, "dmg");
            ad.num = num(a, "num");
            ad.spec = strs(a, "spec");
            t.attacks.push_back(std::move(ad));
        }
        types_[t.id] = std::move(t);
    }
    cJSON_Delete(u);

    cJSON* tj = cJSON_Parse(terrain_json.c_str());
    if (!tj) return false;
    cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(tj, "codes")) {
        TerrainDef d;
        d.mvt = parse_expr(cJSON_GetObjectItemCaseSensitive(e, "mvt"));
        d.def = parse_expr(cJSON_GetObjectItemCaseSensitive(e, "def"));
        d.village = num(e, "village") != 0;
        d.castle = num(e, "castle") != 0;
        d.keep = num(e, "keep") != 0;
        d.heals = num(e, "heals");
        d.name = str(e, "name", e->string);
        d.display = d.village ? "village" : d.keep ? "keep" : d.castle ? "castle" : last_leaf(d.def);
        terrain_[e->string] = std::move(d);
    }
    cJSON_Delete(tj);
    unknown_terrain_.mvt.leaf = unknown_terrain_.def.leaf = "impassable";
    unknown_terrain_.display = "impassable";
    unknown_terrain_.name = "?";
    return !types_.empty() && !terrain_.empty();
}

const UnitTypeDef* GameData::type(const std::string& id) const {
    auto it = types_.find(id);
    return it == types_.end() ? nullptr : &it->second;
}
const TerrainDef& GameData::terrain(const std::string& code) const {
    auto it = terrain_.find(code);
    return it == terrain_.end() ? unknown_terrain_ : it->second;
}
const Race* GameData::race(const std::string& id) const {
    auto it = races_.find(id);
    return it == races_.end() ? nullptr : &it->second;
}
const TraitEffect* GameData::trait(const std::string& id) const {
    auto it = traits_.find(id);
    return it == traits_.end() ? nullptr : &it->second;
}

int GameData::leaf_value(const UnitTypeDef& t, const std::string& leaf, bool movement) const {
    const auto& over = movement ? t.costs : t.defense;
    auto o = over.find(leaf);
    if (o != over.end()) return std::abs(o->second);
    auto mt = movetypes_.find(t.movetype);
    if (mt != movetypes_.end()) {
        const auto& tab = movement ? mt->second.costs : mt->second.defense;
        auto v = tab.find(leaf);
        if (v != tab.end()) return std::abs(v->second);
    }
    return movement ? UNREACHABLE : 100;
}

int GameData::eval(const UnitTypeDef& t, const TerrExpr& e, bool movement) const {
    if (e.op == 0) return leaf_value(t, e.leaf, movement);
    int best = e.op == 1 ? 1000 : -1;
    for (const auto& a : e.args) {
        int v = eval(t, a, movement);
        // coût et « chance d'être touché » : plus bas = meilleur dans les deux cas
        best = e.op == 1 ? std::min(best, v) : std::max(best, v);
    }
    return best;
}

int GameData::move_cost(const UnitTypeDef& t, const std::string& code) const {
    std::string key = t.id + "|m|" + code;
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;
    int v = std::min(eval(t, terrain(code).mvt, true), UNREACHABLE);
    cache_[key] = v;
    return v;
}

int GameData::defense(const UnitTypeDef& t, const std::string& code) const {
    std::string key = t.id + "|d|" + code;
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;
    int v = eval(t, terrain(code).def, false);
    cache_[key] = v;
    return v;
}

int GameData::resistance(const UnitTypeDef& t, const std::string& dmg_type) const {
    auto o = t.resist.find(dmg_type);
    if (o != t.resist.end()) return o->second;
    auto mt = movetypes_.find(t.movetype);
    if (mt != movetypes_.end()) {
        auto v = mt->second.resist.find(dmg_type);
        if (v != mt->second.resist.end()) return v->second;
    }
    return 100;
}

}  // namespace wsg
