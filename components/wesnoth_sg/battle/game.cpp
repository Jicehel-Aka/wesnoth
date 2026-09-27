// battle/game.cpp — voir game.h
#include "battle/game.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <queue>
#include <sstream>

#include "cJSON.h"

namespace wsg {

// Horaire par défaut de Wesnoth (DEFAULT_SCHEDULE), utilisé seulement si le
// scénario n'exporte pas ses [time]
static const int kLawful[6] = {0, 25, 25, 0, -25, -25};
static const char* kTodIds[6] = {"dawn", "morning", "afternoon", "dusk", "first_watch", "second_watch"};
static const char* tod_fr(const std::string& id) {
    if (id == "dawn") return "Aube";
    if (id == "morning") return "Matin";
    if (id == "afternoon") return "Après-midi";
    if (id == "dusk") return "Crépuscule";
    if (id == "first_watch") return "Première veille";
    if (id == "second_watch") return "Seconde veille";
    if (id == "midnight") return "Minuit";
    if (id == "underground") return "Souterrain";
    if (id == "indoors") return "Intérieur";
    return "?";
}

int round_damage(int base, int bonus, int divisor) {
    if (base == 0) return 0;
    const int rounding = divisor / 2 - (bonus < divisor || divisor == 1 ? 0 : 1);
    return std::max(1, (base * bonus + rounding) / divisor);
}

uint32_t Game::rand_u32() {
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
    return rng_;
}

// ---------------------------------------------------------------------------
// Chargement
// ---------------------------------------------------------------------------
namespace {
std::string jstr(const cJSON* o, const char* k, const char* d = "") {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
    return (v && cJSON_IsString(v)) ? v->valuestring : d;
}
int jint(const cJSON* o, const char* k, int d = 0) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
    if (!v) return d;
    if (cJSON_IsNumber(v)) return v->valueint;
    if (cJSON_IsString(v)) return atoi(v->valuestring);
    return d;
}
std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> r;
    std::stringstream ss(s);
    std::string x;
    while (std::getline(ss, x, ',')) {
        size_t a = x.find_first_not_of(' '), b = x.find_last_not_of(' ');
        if (a != std::string::npos) r.push_back(x.substr(a, b - a + 1));
    }
    return r;
}
}  // namespace

bool Game::load_scenario(const std::string& scenario_json, const std::string& map_text) {
    // carte (codes bruts, bordure comprise : les coordonnées WML s'y lisent
    // directement)
    map_.clear();
    w_ = h_ = 0;
    std::stringstream lines(map_text);
    std::string line;
    std::vector<std::vector<std::string>> rows;
    while (std::getline(lines, line)) {
        if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
        std::vector<std::string> row;
        for (auto& c : split_csv(line)) {
            size_t sp = c.find_last_of(' ');
            std::string code = sp == std::string::npos ? c : c.substr(sp + 1);
            while (!code.empty() && (code.back() == '\r')) code.pop_back();
            row.push_back(code);
        }
        if (!row.empty()) rows.push_back(row);
    }
    h_ = (int)rows.size();
    w_ = h_ ? (int)rows[0].size() : 0;
    map_.resize((size_t)w_ * h_, "Xv");
    for (int y = 0; y < h_; ++y)
        for (int x = 0; x < w_ && x < (int)rows[y].size(); ++x) map_[y * w_ + x] = rows[y][x];
    villages_.clear();
    for (int y = 0; y < h_; ++y)
        for (int x = 0; x < w_; ++x)
            if (d_.terrain(map_[y * w_ + x]).village) villages_.push_back({{x, y}, 0});

    cJSON* s = cJSON_Parse(scenario_json.c_str());
    if (!s || !w_) { cJSON_Delete(s); return false; }
    turns_ = jint(s, "turns", -1);
    turn_ = start_turn_ = jint(s, "current_turn", 1);
    xp_mod_ = jint(s, "experience_modifier", 100);
    const cJSON* gco = cJSON_GetObjectItemCaseSensitive(s, "gold_carryover");
    const cJSON* gcb = gco ? cJSON_GetObjectItemCaseSensitive(gco, "bonus") : nullptr;
    gc_bonus_ = gcb && cJSON_IsTrue(gcb);
    gc_percent_ = gco ? jint(gco, "percent", 80) : 80;
    const cJSON* sch = cJSON_GetObjectItemCaseSensitive(s, "schedule");
    tod_start_ = sch ? jint(sch, "start_index", 0) : 0;
    lawful_.clear();
    tod_ids_.clear();
    if (sch) {
        const cJSON* t = nullptr;
        cJSON_ArrayForEach(t, cJSON_GetObjectItemCaseSensitive(sch, "times")) {
            lawful_.push_back(jint(t, "lawful_bonus"));
            tod_ids_.push_back(jstr(t, "id"));
        }
    }
    if (lawful_.empty())
        for (int i = 0; i < 6; ++i) { lawful_.push_back(kLawful[i]); tod_ids_.push_back(kTodIds[i]); }

    sides_.clear();
    const cJSON* e = nullptr;
    cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(s, "sides")) {
        Side sd;
        sd.id = jint(e, "side");
        sd.teams = split_csv(jstr(e, "team_name"));
        if (sd.teams.empty()) sd.teams.push_back("side" + std::to_string(sd.id));
        sd.color = jstr(e, "color");
        sd.human = jstr(e, "controller") == "human";
        sd.hidden = jstr(e, "hidden") == "yes";
        sd.gold = jint(e, "gold", 100);
        sd.income_mod = jint(e, "income", 0);
        sd.recruit = split_csv(jstr(e, "recruit"));
        sides_.push_back(sd);
    }
    std::sort(sides_.begin(), sides_.end(), [](const Side& a, const Side& b) { return a.id < b.id; });

    units_.clear();
    units_.reserve(256);
    cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(s, "units")) {
        std::vector<std::string> traits;
        const cJSON* t = nullptr;
        cJSON_ArrayForEach(t, cJSON_GetObjectItemCaseSensitive(e, "traits")) traits.push_back(t->valuestring);
        std::string type = jstr(e, "type");
        std::string variation = jstr(e, "variation");
        if (!variation.empty() && d_.type(type + ":" + variation)) type += ":" + variation;
        if (!d_.type(type)) {
            printf("[wsg] type d'unité inconnu : %s\n", type.c_str());
            continue;
        }
        Unit u = make_unit(type, jint(e, "side", 1), {jint(e, "x"), jint(e, "y")}, traits,
                           jstr(e, "random_traits", "yes") != "no");
        u.id = jstr(e, "id");
        u.name = jstr(e, "name");
        u.canrecruit = jstr(e, "canrecruit") == "yes";
        u.role = jstr(e, "role");
        u.guardian = jint(e, "guardian") != 0;
        if (const cJSON* z = cJSON_GetObjectItemCaseSensitive(e, "zone_guardian")) {
            u.zone_x = jint(z, "x"); u.zone_y = jint(z, "y"); u.zone_r = jint(z, "radius", 1);
        }
        // objets (ex. increase_damage) : seules les augmentations de dégâts
        // sont reprises pour l'instant
        const cJSON* ob = nullptr;
        cJSON_ArrayForEach(ob, cJSON_GetObjectItemCaseSensitive(e, "objects")) {
            const cJSON* ef = nullptr;
            cJSON_ArrayForEach(ef, cJSON_GetObjectItemCaseSensitive(ob, "effects"))
                if (jstr(ef, "apply_to") == "attack") u.dmg_bonus += jint(ef, "increase_damage");
        }
        units_.push_back(u);
    }
    cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(s, "villages")) {
        HexCoord p{jint(e, "x"), jint(e, "y")};
        for (auto& v : villages_) if (v.pos == p) v.owner = jint(e, "side");
    }
    auto filters = [&](const char* key, std::vector<std::string>& out) {
        out.clear();
        const cJSON* f = nullptr;
        cJSON_ArrayForEach(f, cJSON_GetObjectItemCaseSensitive(s, key)) {
            if (!jstr(f, "id").empty()) out.push_back("id:" + jstr(f, "id"));
            else if (!jstr(f, "type").empty()) out.push_back("type:" + jstr(f, "type"));
            else if (!jstr(f, "role").empty()) out.push_back("role:" + jstr(f, "role"));
        }
    };
    filters("victory_on_death", victory_ids_);
    filters("defeat_on_death", defeat_ids_);
    objectives_.clear();
    cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(s, "objectives"))
        objectives_.push_back((jstr(e, "condition") == "win" ? "+ " : "- ") + jstr(e, "text"));
    cJSON_Delete(s);

    outcome_ = Outcome::None;
    cur_side_ = sides_.empty() ? 1 : sides_.front().id;
    rng_ = 0x5EED1234u;
    return true;
}

void Game::begin() {
    // Ordre du vrai cycle de vie Wesnoth : prestart, puis start (tous deux
    // n'ont d'effet que si extract_scenario.py a dû en reporter tout ou
    // partie faute de pouvoir les résoudre hors ligne -- cas des scénarios
    // qui rappellent des personnages via [recall], le report d'unités
    // n'étant connu qu'en jeu), puis le premier tour.
    fire("prestart");
    fire("start");
    fire("turn " + std::to_string(turn_));
    fire("new turn");
    start_side_turn(cur_side_);
}

// ---------------------------------------------------------------------------
// Accès
// ---------------------------------------------------------------------------
const std::string& Game::code(HexCoord h) const {
    static const std::string off = "Xv";
    return in_data(h) ? map_[h.y * w_ + h.x] : off;
}
int Game::village_owner(HexCoord h) const {
    for (const auto& v : villages_) if (v.pos == h) return v.owner;
    return -1;
}
Unit* Game::unit_at(HexCoord h) {
    for (auto& u : units_) if (u.alive() && u.pos == h) return &u;
    return nullptr;
}
Unit* Game::unit_by_id(const std::string& id) {
    for (auto& u : units_) if (u.id == id) return &u;
    return nullptr;
}
Unit* Game::unit_by_uid(int uid) {
    for (auto& u : units_) if (u.uid == uid) return &u;
    return nullptr;
}
Side* Game::side(int id) {
    for (auto& s : sides_) if (s.id == id) return &s;
    return nullptr;
}
bool Game::allied(int a, int b) const {
    if (a == b) return true;
    const Side *sa = nullptr, *sb = nullptr;
    for (const auto& s : sides_) { if (s.id == a) sa = &s; if (s.id == b) sb = &s; }
    if (!sa || !sb) return false;
    for (const auto& t : sa->teams)
        if (std::find(sb->teams.begin(), sb->teams.end(), t) != sb->teams.end()) return true;
    return false;
}
int Game::village_count(int side) const {
    int n = 0;
    for (const auto& v : villages_) if (v.owner == side) ++n;
    return n;
}
int Game::upkeep(int side) const {
    int u = 0;
    for (const auto& x : units_)
        if (x.alive() && x.side == side && !x.loyal && !x.canrecruit) u += x.level();
    return u;
}
int Game::income(int side) const {
    const Side* s = nullptr;
    for (const auto& x : sides_) if (x.id == side) s = &x;
    int v = village_count(side);
    int base = 2 + v * 1 + (s ? s->income_mod : 0);          // base_income 2, village_gold 1
    return base - std::max(0, upkeep(side) - v * 1);         // village_support 1
}

int Game::tod_index() const {
    int n = std::max<int>(1, (int)lawful_.size());
    return ((tod_start_ + turn_ - 1) % n + n) % n;
}
const char* Game::tod_name() const { return tod_ids_.empty() ? "?" : tod_fr(tod_ids_[tod_index()]); }

int Game::lawful_bonus_at(HexCoord h) const {
    int lb = lawful_.empty() ? 0 : lawful_[tod_index()];
    int max_lb = lawful_.empty() ? 25 : *std::max_element(lawful_.begin(), lawful_.end());
    // illuminates : +25 sur la case et les voisines, plafonné au maximum de
    // l'horaire
    for (const auto& u : units_) {
        if (!u.alive() || !u.t || !u.t->has_ability("illuminates")) continue;
        if (u.pos == h || hex_distance(u.pos, h) == 1) { lb = std::min(std::max(lb, max_lb), lb + 25); break; }
    }
    return lb;
}

// ---------------------------------------------------------------------------
// Création d'unité, traits, niveaux
// ---------------------------------------------------------------------------
void Game::apply_type(Unit& u, const UnitTypeDef* t, bool full_heal) {
    u.t = t;
    u.type_id = t->id;
    int lvl = std::max(1, t->level);
    int hp = t->hp, mp = t->mp, xp_pct = 0;
    u.melee_bonus = u.ranged_bonus = 0;
    u.loyal = u.fearless = u.immune_poison = u.immune_drain = u.immune_plague = false;
    u.always_rest_heal = false;
    u.village_def_cap = 0;
    int hp_pct = 0;
    for (const auto& tr : u.traits) {
        const TraitEffect* e = d_.trait(tr);
        if (!e) continue;
        hp += e->hp + e->hp_per_level * lvl;
        hp_pct += e->hp_pct;
        mp += e->mp;
        xp_pct += e->xp_pct;
        u.melee_bonus += e->melee;
        u.ranged_bonus += e->ranged;
        u.loyal |= e->loyal;
        u.fearless |= e->fearless;
        u.always_rest_heal |= e->always_rest_heal;
        u.immune_poison |= e->immune_poison;
        u.immune_drain |= e->immune_drain;
        u.immune_plague |= e->immune_plague;
        if (e->village_def_cap) u.village_def_cap = e->village_def_cap;
    }
    if (t->race == "undead") u.immune_poison = u.immune_drain = u.immune_plague = true;
    hp += (int)std::lround(t->hp * hp_pct / 100.0);
    u.max_hp = std::max(1, hp);
    u.max_mp = std::max(0, mp);
    int base_xp = std::max(1, (int)std::lround(t->xp * xp_mod_ / 100.0));
    u.max_xp = std::max(1, (int)std::lround(base_xp * (100 + xp_pct) / 100.0));
    if (full_heal) u.hp = u.max_hp;
    u.hp = std::min(u.hp, u.max_hp);
}

Unit Game::make_unit(const std::string& type_id, int side, HexCoord pos,
                     const std::vector<std::string>& forced, bool random_traits) {
    Unit u;
    u.uid = next_uid_++;
    u.side = side;
    u.pos = pos;
    const UnitTypeDef* t = d_.type(type_id);
    u.traits = forced;
    if (t) {
        for (const auto& m : t->musthave)
            if (std::find(u.traits.begin(), u.traits.end(), m) == u.traits.end()) u.traits.push_back(m);
        const Race* r = d_.race(t->race);
        int n = t->num_traits >= 0 ? t->num_traits : (r ? r->num_traits : 0);
        int have = 0;
        for (const auto& x : u.traits)
            if (std::find(t->musthave.begin(), t->musthave.end(), x) == t->musthave.end()) ++have;
        if (random_traits && have < n) {
            std::vector<std::string> pool;
            if (r && !t->ignore_race_traits) pool = r->traits;
            if (!r || !r->ignore_global)
                for (const auto& g : d_.global_traits()) pool.push_back(g);
            for (const auto& x : t->extra_traits) pool.push_back(x);
            pool.erase(std::remove_if(pool.begin(), pool.end(), [&](const std::string& p) {
                           return std::find(u.traits.begin(), u.traits.end(), p) != u.traits.end();
                       }), pool.end());
            while (have < n && !pool.empty()) {
                size_t i = rand_u32() % pool.size();
                u.traits.push_back(pool[i]);
                pool.erase(pool.begin() + i);
                ++have;
            }
        }
        apply_type(u, t, true);
        u.mp = u.max_mp;
    }
    return u;
}

void Game::advance_if_needed(Unit& u) {
    while (u.alive() && u.xp >= u.max_xp) {
        u.xp -= u.max_xp;
        const UnitTypeDef* nt = nullptr;
        if (!u.t->adv.empty()) {
            // choix : premier type disponible (l'IA de Wesnoth tire au sort ;
            // l'interface de choix du joueur viendra avec l'écran d'unité)
            const auto& opts = u.t->adv;
            nt = d_.type(opts[u.side == 1 ? 0 : rand_u32() % opts.size()]);
        }
        if (nt) {
            apply_type(u, nt, true);
            log_ += " " + (u.name.empty() ? nt->name : u.name) + " devient " + nt->name + " !";
        } else {
            // AMLA par défaut : +3 PV max, soins complets, +20 % d'XP requise
            u.max_hp += 3;
            u.hp = u.max_hp;
            u.max_xp = (int)std::lround(u.max_xp * 1.2);
            log_ += " AMLA : +3 PV max.";
        }
        u.poisoned = u.slowed = false;
    }
}

const std::string& Game::tod_id() const {
    static const std::string none;
    return tod_ids_.empty() ? none : tod_ids_[tod_index()];
}

void Game::capture(HexCoord h, int side) {
    for (auto& v : villages_) if (v.pos == h) v.owner = side;
}

bool Game::has_live_id(const std::string& id) const {
    if (id.empty()) return false;
    for (const auto& u : units_) if (u.alive() && u.id == id) return true;
    return false;
}

void Game::set_terrain(HexCoord h, const std::string& code) {
    if (in_data(h)) map_[h.y * w_ + h.x] = code;
}

int Game::compute_carryover_gold(int side_id) const {
    const Side* s = nullptr;
    for (const auto& x : sides_) if (x.id == side_id) s = &x;
    if (!s) return 0;
    // Règle de Wesnoth : sans bonus, l'or est divisé par le pourcentage
    // quoi qu'il arrive ; avec bonus, seulement si la victoire est acquise
    // avant la limite de tours (ici non distingué faute d'info exportée :
    // on applique toujours le pourcentage, cas le plus courant).
    return std::max(0, (int)((int64_t)s->gold * gc_percent_ / 100));
}

Unit* Game::place_recall(const PersistentUnit& p, HexCoord where) {
    Unit u = make_unit(p.type_id, p.side, where.x >= 0 ? where : HexCoord{1, 1}, p.traits, false);
    u.id = p.id; u.name = p.name; u.role = p.role;
    u.canrecruit = p.canrecruit; u.recallable = p.recallable;
    u.hp = std::max(1, std::min(u.max_hp, p.hp));
    u.xp = std::max(0, p.xp);
    u.mp = 0;
    u.attacked = true;
    return spawn(u);
}

std::vector<PersistentUnit> Game::harvest_roster(int side_id) const {
    std::vector<PersistentUnit> out;
    for (const auto& u : units_) {
        if (!u.alive() || u.side != side_id) continue;
        PersistentUnit p;
        p.type_id = u.type_id; p.id = u.id; p.name = u.name; p.role = u.role;
        p.traits = u.traits; p.side = side_id; p.hp = u.hp; p.xp = u.xp;
        p.canrecruit = u.canrecruit; p.recallable = u.recallable;
        out.push_back(std::move(p));
    }
    return out;
}

Unit* Game::spawn(Unit u) {
    // case libre la plus proche (comme [unit] de Wesnoth)
    HexCoord best = u.pos;
    int bd = 1000;
    for (int y = 0; y < h_; ++y)
        for (int x = 0; x < w_; ++x) {
            HexCoord h{x, y};
            if (!in_map(h) || unit_at(h) || d_.move_cost(*u.t, code(h)) >= UNREACHABLE) continue;
            int d = hex_distance(h, u.pos);
            if (d < bd) { bd = d; best = h; }
        }
    u.pos = best;
    if (units_.size() == units_.capacity()) units_.reserve(units_.size() * 2);
    units_.push_back(u);
    return &units_.back();
}

void Game::kill_unit(Unit& victim, Unit* killer) {
    int v = victim.uid, k = killer ? killer->uid : 0;
    HexCoord where = victim.pos;
    fire("last breath", v, k, where);
    Unit* pv = unit_by_uid(v);
    if (pv && pv->alive()) pv->hp = 0;
    fire("die", v, k, where);
}

// ---------------------------------------------------------------------------
// Terrain, zones de contrôle, déplacement
// ---------------------------------------------------------------------------
int Game::defense_of(const Unit& u, HexCoord h) const {
    int d = d_.defense(*u.t, code(h));
    if (u.village_def_cap && terrain(h).village) d = std::max(d, u.village_def_cap);
    return d;
}

bool Game::in_enemy_zoc(const Unit& u, HexCoord h) const {
    for (const auto& o : units_) {
        if (!o.alive() || allied(o.side, u.side) || o.level() < 1) continue;
        if (hex_distance(o.pos, h) == 1) return true;
    }
    return false;
}

std::unordered_map<int, PathNode> Game::reach(const Unit& u) const {
    std::unordered_map<int, PathNode> best;
    const bool skirmisher = u.t && u.t->has_ability("skirmisher");
    using QE = std::pair<int, int>;  // (coût, clé)
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
    best[key(u.pos)] = {0, -1};
    q.push({0, key(u.pos)});
    while (!q.empty()) {
        auto [c, k] = q.top(); q.pop();
        if (best[k].cost != c) continue;
        HexCoord h = unkey(k);
        if (!(h == u.pos) && !skirmisher && in_enemy_zoc(u, h)) continue;  // la ZdC arrête
        for (HexCoord n : hex_neighbors(h)) {
            if (!in_map(n)) continue;
            int step = d_.move_cost(*u.t, code(n));
            if (step >= UNREACHABLE) continue;
            int nc = c + step;
            if (nc > u.mp) continue;
            const Unit* occ = nullptr;
            for (const auto& o : units_) if (o.alive() && o.pos == n) { occ = &o; break; }
            if (occ && !allied(occ->side, u.side)) continue;
            int nk = key(n);
            auto it = best.find(nk);
            if (it == best.end() || nc < it->second.cost) {
                best[nk] = {nc, k};
                q.push({nc, nk});
            }
        }
    }
    return best;
}

std::vector<HexCoord> Game::path_to(const std::unordered_map<int, PathNode>& r, HexCoord to) const {
    std::vector<HexCoord> p;
    auto it = r.find(key(to));
    if (it == r.end()) return p;
    int k = key(to);
    while (k != -1) {
        p.push_back(unkey(k));
        k = r.at(k).prev;
    }
    std::reverse(p.begin(), p.end());
    return p;
}

bool Game::move_unit(Unit& u, HexCoord to) {
    if (u.pos == to) return false;
    auto r = reach(u);
    auto path = path_to(r, to);
    if (path.size() < 2) return false;
    for (const auto& o : units_) if (o.alive() && o.pos == to && o.uid != u.uid) return false;
    const bool skirmisher = u.t->has_ability("skirmisher");
    for (size_t i = 1; i < path.size(); ++i) {
        HexCoord h = path[i];
        u.mp -= d_.move_cost(*u.t, code(h));
        bool occupied = false;
        for (const auto& o : units_) if (o.alive() && o.uid != u.uid && o.pos == h) occupied = true;
        if (occupied) continue;              // on traverse un allié sans s'y arrêter
        u.pos = h;
        // capture de village : arrête le déplacement
        for (auto& v : villages_) {
            if (v.pos == h && v.owner != u.side && !(v.owner > 0 && allied(v.owner, u.side))) {
                v.owner = u.side;
                u.mp = 0;
            }
        }
        if (!skirmisher && in_enemy_zoc(u, h)) u.mp = 0;
        if (u.mp <= 0) break;
    }
    u.mp = std::max(0, u.mp);
    u.resting = false;
    fire("moveto", u.uid, 0, u.pos);
    return true;
}

// ---------------------------------------------------------------------------
// Combat
// ---------------------------------------------------------------------------
int Game::leadership_bonus(const Unit& u, HexCoord at) const {
    int best = 0;
    for (const auto& o : units_) {
        if (!o.alive() || o.side != u.side || o.uid == u.uid || !o.t) continue;
        if (!o.t->has_ability("leadership") || hex_distance(o.pos, at) != 1) continue;
        if (o.level() > u.level()) best = std::max(best, 25 * (o.level() - u.level()));
    }
    return best;
}

bool Game::backstab_active(const Unit& att, HexCoord from, const Unit& def) const {
    // case opposée à l'attaquant par rapport au défenseur
    auto nb = hex_neighbors(def.pos);
    for (int i = 0; i < 6; ++i) {
        if (!(nb[i] == from)) continue;
        HexCoord opp = nb[(i + 3) % 6];
        for (const auto& o : units_)
            if (o.alive() && o.pos == opp && !allied(o.side, def.side) && o.uid != att.uid) return true;
    }
    return false;
}

int Game::strikes_of(const Unit& u, const AttackDef& w) const {
    if (w.has("swarm")) return w.num * u.hp / std::max(1, u.max_hp);
    return w.num;
}

int Game::strike_damage(const Unit& a, const AttackDef& w, const Unit& d, HexCoord a_pos,
                        HexCoord d_pos, bool charge, bool backstab) const {
    (void)d_pos;
    int base = w.dmg + (w.melee() ? a.melee_bonus : a.ranged_bonus) + a.dmg_bonus;
    if (base <= 0) return 0;
    int mult_base = 1;
    if (charge) mult_base *= 2;
    if (backstab) mult_base *= 2;
    base *= mult_base;
    int lb = lawful_bonus_at(a_pos);
    int tod = 0;
    switch (a.t->align) {
        case Align::Lawful: tod = lb; break;
        case Align::Chaotic: tod = -lb; break;
        case Align::Liminal: tod = -std::abs(lb); break;
        default: break;
    }
    if (a.fearless && tod < 0) tod = 0;
    int pct = 100 + tod + leadership_bonus(a, a_pos);
    int res = d_.resistance(*d.t, w.type);
    // steadfast : résistances doublées en défense, jusqu'à 50 %
    if (d.t->has_ability("steadfast") && &d != &a) {
        int amount = 100 - res;
        if (amount > 0) amount = std::max(amount, std::min(amount * 2, 50));
        res = 100 - amount;
    }
    int dmg = round_damage(base, pct * res, 10000);
    if (a.slowed) dmg = round_damage(dmg, 1, 2);
    return dmg;
}

int Game::best_defender_weapon(const Unit& att, int aw, const Unit& def, HexCoord from) const {
    const AttackDef& a = att.t->attacks[aw];
    int best = -1;
    double best_rating = -1;
    for (int i = 0; i < (int)def.t->attacks.size(); ++i) {
        const AttackDef& w = def.t->attacks[i];
        if (w.range != a.range) continue;
        int cth = defense_of(att, from);
        if (w.has("magical")) cth = 70;
        double r = strike_damage(def, w, att, def.pos, from, a.has("charge"), false) *
                   strikes_of(def, w) * cth / 100.0;
        if (r > best_rating) { best_rating = r; best = i; }
    }
    return best;
}

CombatPreview Game::preview(const Unit& att, int aw, const Unit& def, HexCoord from) const {
    CombatPreview p;
    const AttackDef& a = att.t->attacks[aw];
    int dw = best_defender_weapon(att, aw, def, from);
    const AttackDef* dwp = dw >= 0 ? &def.t->attacks[dw] : nullptr;
    const bool charge = a.has("charge");
    const bool bs = a.has("backstab") && backstab_active(att, from, def);
    Unit att_at = att; att_at.pos = from;
    p.att.weapon = aw;
    p.att.damage = strike_damage(att_at, a, def, from, def.pos, charge, bs);
    p.att.strikes = strikes_of(att, a);
    int cth = defense_of(def, def.pos);
    if (a.has("magical")) cth = 70;
    if (a.has("marksman")) cth = std::max(cth, 60);
    p.att.cth = cth;
    p.att.slows = a.has("slow"); p.att.poisons = a.has("poison"); p.att.drains = a.has("drain");
    p.att.firststrike = a.has("firststrike");
    if (dwp) {
        p.def.weapon = dw;
        p.def.damage = strike_damage(def, *dwp, att_at, def.pos, from, charge, false);
        p.def.strikes = strikes_of(def, *dwp);
        int c2 = defense_of(att, from);
        if (dwp->has("magical")) c2 = 70;
        p.def.cth = c2;
        p.def.slows = dwp->has("slow"); p.def.poisons = dwp->has("poison");
        p.def.drains = dwp->has("drain"); p.def.firststrike = dwp->has("firststrike");
    }
    p.berserk = a.has("berserk") || (dwp && dwp->has("berserk"));

    // Distribution exacte des PV (ordre des coups compris ; les effets de
    // slow/drain en cours d'assaut ne sont pas modélisés dans la prévision)
    std::map<std::pair<int, int>, double> st;
    st[{att.hp, def.hp}] = 1.0;
    const bool def_first = dwp && p.def.firststrike && !p.att.firststrike;
    const int rounds = p.berserk ? 30 : 1;
    const int n = std::max(p.att.strikes, p.def.strikes);
    auto strike = [&](bool attacker) {
        std::map<std::pair<int, int>, double> nx;
        for (auto& [k, pr] : st) {
            int ah = k.first, dh = k.second;
            if (ah <= 0 || dh <= 0) { nx[k] += pr; continue; }
            double hit = (attacker ? p.att.cth : p.def.cth) / 100.0;
            int dmg = attacker ? p.att.damage : p.def.damage;
            if (attacker) {
                nx[{ah, std::max(0, dh - dmg)}] += pr * hit;
            } else {
                nx[{std::max(0, ah - dmg), dh}] += pr * hit;
            }
            nx[k] += pr * (1 - hit);
        }
        st.swap(nx);
    };
    for (int r = 0; r < rounds; ++r) {
        for (int i = 0; i < n; ++i) {
            bool first_att = !def_first;
            for (int j = 0; j < 2; ++j) {
                bool is_att = (j == 0) == first_att;
                int ns = is_att ? p.att.strikes : (dwp ? p.def.strikes : 0);
                if (i < ns) strike(is_att);
            }
        }
        if (p.berserk) {
            double alive_both = 0;
            for (auto& [k, pr] : st) if (k.first > 0 && k.second > 0) alive_both += pr;
            if (alive_both < 0.001) break;
        }
    }
    for (auto& [k, pr] : st) {
        if (k.first <= 0) p.att.p_dead += pr;
        if (k.second <= 0) p.def.p_dead += pr;
        p.att.exp_hp += pr * k.first;
        p.def.exp_hp += pr * k.second;
    }
    return p;
}

void Game::kill(Unit& victim_ref, Unit* killer_ref, bool plague) {
    // « last breath » (unité encore sur la carte), mort, puis « die »
    const int vuid = victim_ref.uid, kuid = killer_ref ? killer_ref->uid : 0;
    const HexCoord where = victim_ref.pos;
    fire("last breath", vuid, kuid, where);
    Unit* victim = unit_by_uid(vuid);
    if (!victim) return;
    victim->hp = 0;
    const bool immune = victim->immune_plague;
    Unit* killer = kuid ? unit_by_uid(kuid) : nullptr;
    if (plague && killer && !immune && !terrain(where).village) {
        const char* wc = "Walking Corpse";
        if (d_.type(wc)) {
            Unit z = make_unit(wc, killer->side, where, {}, false);
            z.mp = 0; z.attacked = true;
            spawn(z);
            log_ += " Une victime se relève !";
        }
    }
    fire("die", vuid, kuid, where);
}

std::string Game::attack(Unit& att, int aw, Unit& def) {
    log_.clear();
    const int att_uid = att.uid, def_uid = def.uid;
    fire("attack", att_uid, def_uid, def.pos);
    {   // un événement « attack » peut avoir tué ou déplacé les combattants
        Unit* a2 = unit_by_uid(att_uid);
        Unit* d2 = unit_by_uid(def_uid);
        if (!a2 || !d2 || !a2->alive() || !d2->alive() || hex_distance(a2->pos, d2->pos) != 1) return log_;
    }
    const AttackDef& a = att.t->attacks[aw];
    int dw = best_defender_weapon(att, aw, def, att.pos);
    const AttackDef* dwp = dw >= 0 ? &def.t->attacks[dw] : nullptr;
    const bool charge = a.has("charge");
    const bool bs = a.has("backstab") && backstab_active(att, att.pos, def);
    const bool def_first = dwp && dwp->has("firststrike") && !a.has("firststrike");
    const bool berserk = a.has("berserk") || (dwp && dwp->has("berserk"));
    const int att_level = att.level(), def_level = def.level();
    int a_str = strikes_of(att, a), d_str = dwp ? strikes_of(def, *dwp) : 0;
    int a_hits = 0, d_hits = 0;
    auto do_strike = [&](Unit& s, const AttackDef& w, Unit& t, bool offense) -> bool {
        int cth = defense_of(t, t.pos);
        if (w.has("magical")) cth = 70;
        if (offense && w.has("marksman")) cth = std::max(cth, 60);
        if ((int)(rand_u32() % 100) >= cth) return false;
        int dmg = strike_damage(s, w, t, s.pos, t.pos, charge, offense && bs);
        int dealt = std::min(dmg, t.hp);
        t.hp -= dmg;
        (offense ? a_hits : d_hits)++;
        if (w.has("drain") && !t.immune_drain) s.hp = std::min(s.max_hp, s.hp + dealt / 2);
        if (w.has("slow") && t.hp > 0) t.slowed = true;
        if (w.has("poison") && t.hp > 0 && !t.immune_poison) t.poisoned = true;
        return t.hp <= 0;
    };
    bool att_dead = false, def_dead = false;
    int rounds = berserk ? 30 : 1;
    for (int r = 0; r < rounds && !att_dead && !def_dead; ++r) {
        int n = std::max(a_str, d_str);
        for (int i = 0; i < n && !att_dead && !def_dead; ++i) {
            for (int j = 0; j < 2 && !att_dead && !def_dead; ++j) {
                bool is_att = (j == 0) != def_first;
                if (is_att && i < a_str) def_dead = do_strike(att, a, def, true);
                else if (!is_att && dwp && i < d_str) att_dead = do_strike(def, *dwp, att, false);
            }
        }
    }
    std::string an = att.name.empty() ? att.t->name : att.name;
    std::string dn = def.name.empty() ? def.t->name : def.name;
    char buf[160];
    snprintf(buf, sizeof buf, "%s: %d/%d coups, %s: %d/%d.", an.c_str(), a_hits, a_str, dn.c_str(), d_hits, d_str);
    std::string summary = buf;

    // XP : combat = niveau adverse ; élimination = 8 x niveau (4 au niveau 0)
    auto kill_xp = [](int lvl) { return lvl == 0 ? 4 : 8 * lvl; };
    att.attacked = true;
    att.resting = false;
    att.mp = 0;
    if (def_dead) {
        att.xp += kill_xp(def_level);
        summary += " " + dn + " est tué.";
        kill(def, &att, a.has("plague"));
    } else if (!att_dead) {
        att.xp += def_level;
        def.xp += att_level;
    }
    if (att_dead) {
        def.xp += kill_xp(att_level);
        summary += " " + an + " est tué.";
        kill(att, &def, dwp && dwp->has("plague"));
    }
    // kill() peut ajouter une unité (plague) : relire les pointeurs
    Unit* pa = unit_by_uid(att_uid);
    Unit* pd = unit_by_uid(def_uid);
    std::string before = log_;
    log_.clear();
    if (pa && pa->alive()) advance_if_needed(*pa);
    if (pd && pd->alive()) advance_if_needed(*pd);
    log_ = summary + before + log_;
    std::string result = log_;
    fire("attack end", att_uid, def_uid, {-1, -1});
    check_outcome();
    log_ = result;
    return log_;
}

// ---------------------------------------------------------------------------
// Recrutement
// ---------------------------------------------------------------------------
std::vector<HexCoord> Game::recruit_hexes(const Unit& leader) const {
    std::vector<HexCoord> out;
    if (!leader.canrecruit || !terrain(leader.pos).keep) return out;
    std::vector<int> seen{key(leader.pos)};
    std::vector<HexCoord> todo{leader.pos};
    while (!todo.empty()) {
        HexCoord h = todo.back(); todo.pop_back();
        for (HexCoord n : hex_neighbors(h)) {
            if (!in_map(n) || !terrain(n).castle) continue;
            if (std::find(seen.begin(), seen.end(), key(n)) != seen.end()) continue;
            seen.push_back(key(n));
            todo.push_back(n);
            bool free = true;
            for (const auto& o : units_) if (o.alive() && o.pos == n) free = false;
            if (free) out.push_back(n);
        }
    }
    return out;
}

bool Game::recruit(int side_id, const std::string& type_id, HexCoord where) {
    Side* s = side(side_id);
    const UnitTypeDef* t = d_.type(type_id);
    if (!s || !t || s->gold < t->cost) return false;
    for (const auto& o : units_) if (o.alive() && o.pos == where) return false;
    Unit u = make_unit(type_id, side_id, where, {}, true);
    u.mp = 0;
    u.attacked = true;
    u.recallable = true;
    s->gold -= t->cost;
    int uid = u.uid;
    units_.push_back(u);
    fire("recruit", uid, 0, where);
    return true;
}

// ---------------------------------------------------------------------------
// Tours
// ---------------------------------------------------------------------------
void Game::heal_side(int side_id) {
    for (auto& u : units_) {
        if (!u.alive() || u.side != side_id) continue;
        const TerrainDef& td = terrain(u.pos);
        int heal = 0;
        bool cure = false, halt_poison = false;
        if (td.heals > 0) { heal = std::max(heal, td.heals); cure = true; }
        if (u.t->has_ability("regenerates")) { heal = std::max(heal, 8); cure = true; }
        for (const auto& o : units_) {
            if (!o.alive() || o.uid == u.uid || !allied(o.side, u.side) || hex_distance(o.pos, u.pos) != 1) continue;
            if (o.t->has_ability("cures")) cure = true;
            int hv = o.t->ability_value("heals");
            if (hv > 0) { heal = std::max(heal, hv); halt_poison = true; }
        }
        if (u.poisoned) {
            if (cure) u.poisoned = false;
            else if (!halt_poison) u.hp = std::max(1, u.hp - 8);
            heal = 0;
        }
        if (u.resting || u.always_rest_heal) heal += 2;
        u.hp = std::min(u.max_hp, u.hp + heal);
    }
}

void Game::start_side_turn(int side_id) {
    const std::string sn = "side " + std::to_string(side_id);
    fire(sn + " turn");
    fire("side turn");
    fire(sn + " turn " + std::to_string(turn_));
    Side* s = side(side_id);
    const bool first_turn = (turn_ == start_turn_);
    if (s && !first_turn) {
        s->gold += income(side_id);
        heal_side(side_id);
    }
    for (auto& u : units_) {
        if (!u.alive() || u.side != side_id) continue;
        u.mp = u.max_mp;
        u.attacked = false;
        u.resting = true;
    }
    fire(sn + " turn refresh");
    fire("turn refresh");
}

void Game::end_turn_of_current_side() {
    const std::string sn = "side " + std::to_string(cur_side_);
    fire(sn + " turn end");
    fire("side turn end");
    fire(sn + " turn " + std::to_string(turn_) + " end");
    if (outcome_ != Outcome::None) return;
    for (auto& u : units_) if (u.alive() && u.side == cur_side_) u.slowed = false;
    // camp suivant (on saute les camps sans aucune unité ni recrutement)
    auto has_presence = [&](const Side& s) {
        for (const auto& u : units_) if (u.alive() && u.side == s.id) return true;
        return false;
    };
    size_t idx = 0;
    for (size_t i = 0; i < sides_.size(); ++i) if (sides_[i].id == cur_side_) idx = i;
    for (size_t step = 0; step < sides_.size(); ++step) {
        idx = (idx + 1) % sides_.size();
        if (idx == 0) {
            fire("turn end");
            fire("turn " + std::to_string(turn_) + " end");
            ++turn_;
            if (turns_ > 0 && turn_ > turns_) {
                outcome_ = Outcome::Defeat;
                outcome_reason_ = "Plus de tours";
                return;
            }
        }
        if (idx == 0) {
            fire("turn " + std::to_string(turn_));
            fire("new turn");
        }
        if (has_presence(sides_[idx])) break;
    }
    cur_side_ = sides_[idx].id;
    start_side_turn(cur_side_);
}

void Game::check_outcome() {
    if (outcome_ != Outcome::None) return;
    auto dead = [&](const std::string& f) {
        bool by_type = f.compare(0, 5, "type:") == 0;
        bool by_role = f.compare(0, 5, "role:") == 0;
        std::string v = f.substr((by_type || by_role) ? 5 : 3);
        bool any = false, any_dead = false;
        for (const auto& u : units_) {
            bool match = by_type ? u.type_id == v : (by_role ? u.role == v : u.id == v);
            if (match) {
                any = true;
                if (!u.alive()) any_dead = true;
            }
        }
        return any && any_dead;
    };
    for (const auto& f : defeat_ids_)
        if (dead(f)) { outcome_ = Outcome::Defeat; outcome_reason_ = "Mort : " + f.substr(f.find(':') + 1); return; }
    for (const auto& f : victory_ids_)
        if (dead(f)) { outcome_ = Outcome::Victory; outcome_reason_ = f.substr(f.find(':') + 1) + " est vaincu"; return; }
}

// ---------------------------------------------------------------------------
// Sauvegarde -- voir game.h. N'écrit/ne relit que l'état mutable : la carte,
// les données d'unité (d_) et les identifiants victoire/défaite du scénario
// restent ceux du scenario_json rechargé par l'appelant avant load_state()
// (sauf victory_ids_/defeat_ids_, repris ici car modifiables par les
// événements en jeu -- [modify_side] etc. -- donc pas toujours identiques
// au JSON d'origine).
// ---------------------------------------------------------------------------
void Game::save_state(cJSON* out) const {
    cJSON_AddNumberToObject(out, "turn", turn_);
    cJSON_AddNumberToObject(out, "cur_side", cur_side_);
    cJSON_AddNumberToObject(out, "next_uid", next_uid_);
    cJSON_AddNumberToObject(out, "rng", (double)rng_);
    cJSON_AddNumberToObject(out, "outcome", (int)outcome_);
    cJSON_AddStringToObject(out, "outcome_reason", outcome_reason_.c_str());

    cJSON* sides = cJSON_AddArrayToObject(out, "sides");
    for (const auto& sd : sides_) {
        cJSON* so = cJSON_CreateObject();
        cJSON_AddNumberToObject(so, "id", sd.id);
        cJSON_AddNumberToObject(so, "gold", sd.gold);
        cJSON_AddNumberToObject(so, "income_mod", sd.income_mod);
        cJSON_AddItemToArray(sides, so);
    }
    cJSON* vills = cJSON_AddArrayToObject(out, "villages");
    for (const auto& v : villages_) {
        cJSON* vo = cJSON_CreateObject();
        cJSON_AddNumberToObject(vo, "x", v.pos.x);
        cJSON_AddNumberToObject(vo, "y", v.pos.y);
        cJSON_AddNumberToObject(vo, "owner", v.owner);
        cJSON_AddItemToArray(vills, vo);
    }
    auto strs_to = [&](const char* key, const std::vector<std::string>& in) {
        cJSON* arr = cJSON_AddArrayToObject(out, key);
        for (const auto& s : in) cJSON_AddItemToArray(arr, cJSON_CreateString(s.c_str()));
    };
    strs_to("objectives", objectives_);
    strs_to("victory_ids", victory_ids_);
    strs_to("defeat_ids", defeat_ids_);

    cJSON* units = cJSON_AddArrayToObject(out, "units");
    for (const auto& u : units_) {
        cJSON* uo = cJSON_CreateObject();
        cJSON_AddNumberToObject(uo, "uid", u.uid);
        cJSON_AddStringToObject(uo, "type_id", u.type_id.c_str());
        cJSON_AddStringToObject(uo, "id", u.id.c_str());
        cJSON_AddStringToObject(uo, "name", u.name.c_str());
        cJSON_AddStringToObject(uo, "role", u.role.c_str());
        cJSON_AddNumberToObject(uo, "side", u.side);
        cJSON_AddNumberToObject(uo, "x", u.pos.x);
        cJSON_AddNumberToObject(uo, "y", u.pos.y);
        cJSON_AddNumberToObject(uo, "hp", u.hp);
        cJSON_AddNumberToObject(uo, "max_hp", u.max_hp);
        cJSON_AddNumberToObject(uo, "mp", u.mp);
        cJSON_AddNumberToObject(uo, "max_mp", u.max_mp);
        cJSON_AddNumberToObject(uo, "xp", u.xp);
        cJSON_AddNumberToObject(uo, "max_xp", u.max_xp);
        cJSON_AddNumberToObject(uo, "melee_bonus", u.melee_bonus);
        cJSON_AddNumberToObject(uo, "ranged_bonus", u.ranged_bonus);
        cJSON_AddNumberToObject(uo, "dmg_bonus", u.dmg_bonus);
        cJSON* tr = cJSON_AddArrayToObject(uo, "traits");
        for (const auto& t : u.traits) cJSON_AddItemToArray(tr, cJSON_CreateString(t.c_str()));
        cJSON_AddBoolToObject(uo, "canrecruit", u.canrecruit);
        cJSON_AddBoolToObject(uo, "guardian", u.guardian);
        cJSON_AddBoolToObject(uo, "loyal", u.loyal);
        cJSON_AddBoolToObject(uo, "fearless", u.fearless);
        cJSON_AddBoolToObject(uo, "immune_poison", u.immune_poison);
        cJSON_AddBoolToObject(uo, "immune_drain", u.immune_drain);
        cJSON_AddBoolToObject(uo, "immune_plague", u.immune_plague);
        cJSON_AddBoolToObject(uo, "always_rest_heal", u.always_rest_heal);
        cJSON_AddNumberToObject(uo, "village_def_cap", u.village_def_cap);
        cJSON_AddNumberToObject(uo, "zone_x", u.zone_x);
        cJSON_AddNumberToObject(uo, "zone_y", u.zone_y);
        cJSON_AddNumberToObject(uo, "zone_r", u.zone_r);
        cJSON_AddBoolToObject(uo, "poisoned", u.poisoned);
        cJSON_AddBoolToObject(uo, "slowed", u.slowed);
        cJSON_AddBoolToObject(uo, "recallable", u.recallable);
        cJSON_AddBoolToObject(uo, "attacked", u.attacked);
        cJSON_AddBoolToObject(uo, "resting", u.resting);
        cJSON_AddItemToArray(units, uo);
    }
}

void Game::load_state(const cJSON* in) {
    turn_ = jint(in, "turn", turn_);
    cur_side_ = jint(in, "cur_side", cur_side_);
    next_uid_ = jint(in, "next_uid", next_uid_);
    const cJSON* rngv = cJSON_GetObjectItemCaseSensitive(in, "rng");
    if (rngv && cJSON_IsNumber(rngv)) rng_ = (uint32_t)rngv->valuedouble;
    outcome_ = (Outcome)jint(in, "outcome", (int)outcome_);
    outcome_reason_ = jstr(in, "outcome_reason", outcome_reason_.c_str());

    const cJSON* e = nullptr;
    cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(in, "sides")) {
        if (Side* s = side(jint(e, "id"))) {
            s->gold = jint(e, "gold", s->gold);
            s->income_mod = jint(e, "income_mod", s->income_mod);
        }
    }
    cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(in, "villages")) {
        HexCoord p{jint(e, "x"), jint(e, "y")};
        for (auto& v : villages_) if (v.pos == p) v.owner = jint(e, "owner");
    }
    auto strs_from = [&](const char* key, std::vector<std::string>& out) {
        out.clear();
        const cJSON* it = nullptr;
        cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(in, key))
            if (cJSON_IsString(it)) out.push_back(it->valuestring);
    };
    strs_from("objectives", objectives_);
    strs_from("victory_ids", victory_ids_);
    strs_from("defeat_ids", defeat_ids_);

    units_.clear();
    cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(in, "units")) {
        Unit u;
        u.uid = jint(e, "uid");
        u.type_id = jstr(e, "type_id");
        u.t = d_.type(u.type_id);
        if (!u.t) {
            printf("[wsg] load_state : type d'unite inconnu, unite ignoree : %s\n", u.type_id.c_str());
            continue;
        }
        u.id = jstr(e, "id"); u.name = jstr(e, "name"); u.role = jstr(e, "role");
        u.side = jint(e, "side", 1);
        u.pos = {jint(e, "x"), jint(e, "y")};
        u.hp = jint(e, "hp", 1); u.max_hp = jint(e, "max_hp", 1);
        u.mp = jint(e, "mp"); u.max_mp = jint(e, "max_mp");
        u.xp = jint(e, "xp"); u.max_xp = jint(e, "max_xp", 1);
        u.melee_bonus = jint(e, "melee_bonus");
        u.ranged_bonus = jint(e, "ranged_bonus");
        u.dmg_bonus = jint(e, "dmg_bonus");
        const cJSON* tr = nullptr;
        cJSON_ArrayForEach(tr, cJSON_GetObjectItemCaseSensitive(e, "traits"))
            if (cJSON_IsString(tr)) u.traits.push_back(tr->valuestring);
        auto jbool = [&](const char* k) { const cJSON* v = cJSON_GetObjectItemCaseSensitive(e, k); return v && cJSON_IsTrue(v); };
        u.canrecruit = jbool("canrecruit");
        u.guardian = jbool("guardian");
        u.loyal = jbool("loyal");
        u.fearless = jbool("fearless");
        u.immune_poison = jbool("immune_poison");
        u.immune_drain = jbool("immune_drain");
        u.immune_plague = jbool("immune_plague");
        u.always_rest_heal = jbool("always_rest_heal");
        u.village_def_cap = jint(e, "village_def_cap");
        u.zone_x = jint(e, "zone_x", -1);
        u.zone_y = jint(e, "zone_y", -1);
        u.zone_r = jint(e, "zone_r");
        u.poisoned = jbool("poisoned");
        u.slowed = jbool("slowed");
        u.recallable = jbool("recallable");
        u.attacked = jbool("attacked");
        u.resting = jbool("resting");
        units_.push_back(std::move(u));
    }
}

}  // namespace wsg
