// battle/ai.cpp — voir ai.h
#include "battle/ai.h"

#include <algorithm>
#include <climits>

namespace wsg {

namespace {

struct Memo {             // unités déjà traitées pendant ce tour de camp
    int turn = -1, side = -1;
    std::vector<int> done;
    bool recruited = false;
};
Memo g_memo;

bool in_zone(const Unit& u, HexCoord h) {
    return u.zone_x < 0 || hex_distance(h, {u.zone_x, u.zone_y}) <= u.zone_r;
}

struct AttackPlan { double score = -1e9; HexCoord from; int target = 0; int weapon = 0; };

AttackPlan best_attack(Game& g, Unit& u, double aggression) {
    AttackPlan best;
    if (u.attacked || u.t->attacks.empty()) return best;
    auto r = g.reach(u);
    for (auto& e : g.units()) {
        if (!e.alive() || g.allied(e.side, u.side)) continue;
        if (u.zone_x >= 0 && !in_zone(u, e.pos)) continue;
        for (HexCoord h : hex_neighbors(e.pos)) {
            if (!g.in_map(h)) continue;
            auto it = r.find(g.key(h));
            if (it == r.end()) continue;
            Unit* occ = g.unit_at(h);
            if (occ && occ->uid != u.uid) continue;
            if ((u.guardian || u.canrecruit) && !(h == u.pos)) {
                // un garde (ou le chef, qui reste au donjon) ne frappe que ce
                // qui est à sa portée immédiate
                if (u.canrecruit || hex_distance(h, u.pos) > 1) continue;
            }
            if (u.zone_x >= 0 && !in_zone(u, h)) continue;
            for (int w = 0; w < (int)u.t->attacks.size(); ++w) {
                CombatPreview p = g.preview(u, w, e, h);
                double dealt = e.hp - p.def.exp_hp;
                double taken = u.hp - p.att.exp_hp;
                double value = dealt + p.def.p_dead * (e.t->cost + (e.canrecruit ? 60 : 0))
                             - (1.0 - aggression) * (taken + p.att.p_dead * (u.t->cost + (u.canrecruit ? 80 : 0)))
                             - (g.defense_of(u, h) - 50) / 10.0;
                if (value > best.score) { best = {value, h, e.uid, w}; }
            }
        }
    }
    return best;
}

HexCoord nearest_target(Game& g, const Unit& u, bool wounded) {
    HexCoord best{-1, -1};
    int bd = INT_MAX;
    // blessé : chercher un village ami ou libre ; sinon villages à prendre
    // puis ennemis
    for (int y = 0; y < g.height(); ++y)
        for (int x = 0; x < g.width(); ++x) {
            HexCoord h{x, y};
            if (!g.terrain(h).village) continue;
            int own = g.village_owner(h);
            bool ours = own > 0 && g.allied(own, u.side);
            if (!wounded && ours) continue;
            int d = hex_distance(u.pos, h) * (wounded ? 1 : 2);
            if (d < bd) { bd = d; best = h; }
        }
    if (!wounded) {
        for (const auto& e : g.units()) {
            if (!e.alive() || g.allied(e.side, u.side)) continue;
            int d = hex_distance(u.pos, e.pos);
            if (d < bd) { bd = d; best = e.pos; }
        }
    }
    return best;
}

}  // namespace

bool ai_step(Game& g, double aggression, AiStep& out) {
    const int side = g.current_side();
    if (g_memo.turn != g.turn() || g_memo.side != side) {
        g_memo = Memo();
        g_memo.turn = g.turn();
        g_memo.side = side;
    }
    if (g.outcome() != Outcome::None) return false;

    // 1. recrutement
    if (!g_memo.recruited) {
        g_memo.recruited = true;
        Side* s = g.side(side);
        for (auto& u : g.units()) {
            if (!u.alive() || u.side != side || !u.canrecruit || !s || s->recruit.empty()) continue;
            auto hexes = g.recruit_hexes(u);
            int n = 0;
            for (HexCoord h : hexes) {
                std::vector<std::string> ok;
                for (const auto& t : s->recruit) {
                    const UnitTypeDef* td = g.data().type(t);
                    if (td && td->cost <= s->gold) ok.push_back(t);
                }
                if (ok.empty()) break;
                g.recruit(side, ok[g.rand_u32() % ok.size()], h);
                ++n;
            }
            if (n) {
                out = {u.uid, u.pos, u.pos, 0, std::to_string(n) + " recrue(s)"};
                return true;
            }
        }
    }

    // 2. une action par unité
    for (auto& u : g.units()) {
        if (!u.alive() || u.side != side) continue;
        if (std::find(g_memo.done.begin(), g_memo.done.end(), u.uid) != g_memo.done.end()) continue;
        g_memo.done.push_back(u.uid);
        const int uid = u.uid;
        HexCoord from = u.pos;

        AttackPlan a = best_attack(g, u, aggression);
        if (a.score > 0) {
            if (!(a.from == u.pos)) g.move_unit(u, a.from);
            Unit* me = g.unit_by_uid(uid);
            Unit* tgt = g.unit_by_uid(a.target);
            if (me && tgt && me->alive() && tgt->alive() && hex_distance(me->pos, tgt->pos) == 1) {
                std::string txt = g.attack(*me, a.weapon, *tgt);
                out = {uid, from, g.unit_by_uid(uid) ? g.unit_by_uid(uid)->pos : from, a.target, txt};
                return true;
            }
        }
        if (u.canrecruit || u.guardian || u.mp <= 0) continue;

        // garde de zone : rejoindre sa zone
        HexCoord target;
        if (u.zone_x >= 0) target = {u.zone_x, u.zone_y};
        else target = nearest_target(g, u, u.hp * 2 < u.max_hp);
        if (target.x < 0) continue;
        auto r = g.reach(u);
        HexCoord best = u.pos;
        int bd = hex_distance(u.pos, target) * 100 + g.defense_of(u, u.pos);
        for (auto& [k, node] : r) {
            HexCoord h = g.unkey(k);
            Unit* occ = g.unit_at(h);
            if (occ && occ->uid != uid) continue;
            int d = hex_distance(h, target) * 100 + g.defense_of(u, h);
            if (d < bd) { bd = d; best = h; }
        }
        if (!(best == u.pos)) {
            g.move_unit(u, best);
            out = {uid, from, best, 0, ""};
            return true;
        }
    }
    return false;
}

}  // namespace wsg
