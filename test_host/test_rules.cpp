// test_rules.cpp — test hôte du moteur de règles sur les VRAIES données
// générées (units.json, terrain.json, scénario 1), sans affichage.
#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
#include "battle/game.h"
#include "battle/ai.h"
#include "battle/events.h"
#include <map>

static std::string slurp(const std::string& p) {
    std::ifstream f(p); std::stringstream s; s << f.rdbuf(); return s.str();
}

int main(int argc, char** argv) {
    std::string root = argc > 1 ? argv[1] : "sdcard_files/WESNOTH_SG";
    int games = argc > 2 ? atoi(argv[2]) : 20;
    wsg::GameData d;
    if (!d.load(slurp(root + "/data/units.json"), slurp(root + "/data/terrain.json"))) { puts("data KO"); return 1; }
    printf("types charges: %zu\n", d.type_count());
    std::string scen = slurp(root + "/data/scenarios/01_Born_to_the_Banner.json");
    std::string map = slurp(root + "/maps/01b_Born_to_the_Banner.map");

    // --- vérifications ponctuelles ---------------------------------------
    {
        wsg::Game g(d);
        assert(g.load_scenario(scen, map));
        g.begin();
        printf("carte %dx%d, tour %d/%d, %s, camp %d\n", g.width(), g.height(), g.turn(), g.turn_limit(), g.tod_name(), g.current_side());
        for (auto& u : g.units())
            printf("  %-14s %-22s camp %d (%d,%d) PV %d/%d PM %d XP 0/%d traits:", u.id.c_str(), u.type_id.c_str(), u.side,
                   u.pos.x, u.pos.y, u.hp, u.max_hp, u.max_mp, u.max_xp),
            [&]{ for (auto& t : u.traits) printf(" %s", t.c_str()); puts(""); }();
        wsg::Unit* deo = g.unit_by_id("Deoran");
        assert(deo && deo->max_hp == 47 && deo->max_xp == 22);  // 30*90% = 27, intelligent -20% -> 22
        printf("Or camp 1: %d, villages: %d, revenu: %d\n", g.side(1)->gold, g.village_count(1), g.income(1));
        printf("Défense Deoran sur donjon: %d%% touché ; en forêt Gs^Fds: %d%%\n",
               g.defense_of(*deo, deo->pos), d.defense(*deo->t, "Gs^Fds"));
        auto r = g.reach(*deo);
        printf("Deoran : %zu cases atteignables\n", r.size());
        // recrutement
        auto hx = g.recruit_hexes(*deo);
        printf("cases de recrutement: %zu\n", hx.size());
        assert(!hx.empty());
        assert(g.recruit(1, "Spearman", hx[0]));
        printf("Or après recrue: %d\n", g.side(1)->gold);
        // arrondi Wesnoth
        assert(wsg::round_damage(7, 12500, 10000) == 9);   // 8.75 -> 9
        assert(wsg::round_damage(5, 7500, 10000) == 4);    // 3.75 -> 4
        assert(wsg::round_damage(6, 7500, 10000) == 5);    // 4.5 -> vers la base : 5
        assert(wsg::round_damage(6, 12500, 10000) == 7);   // 7.5 -> vers la base : 7
        // prévision : Thug contre Deoran (depuis une case adjacente libre)
        wsg::Unit* urza = g.unit_by_id("Urza Mathin");
        wsg::Unit* thug = nullptr;
        for (auto& u : g.units()) if (u.type_id == "Thug") { thug = &u; break; }
        for (auto h : hex_neighbors(deo->pos)) if (!g.unit_at(h) && d.move_cost(*thug->t, g.code(h)) < wsg::UNREACHABLE) {
            wsg::CombatPreview p = g.preview(*thug, 0, *deo, h);
            printf("Prévision Thug->Deoran depuis (%d,%d): %dx%d à %d%% / riposte %dx%d à %d%% ; P(Deoran meurt)=%.3f\n",
                   h.x, h.y, p.att.damage, p.att.strikes, p.att.cth, p.def.damage, p.def.strikes, p.def.cth, p.def.p_dead);
            break;
        }
        (void)urza;
    }

    // --- parties complètes IA contre IA ----------------------------------
    int vic = 0, def = 0, other = 0, msgs = 0, fired = 0;
    std::map<std::string, int> unsup;
    for (int gnum = 0; gnum < games; ++gnum) {
        wsg::Game g(d);
        g.load_scenario(scen, map);
        wsg::EventEngine ev(g);
        ev.load(scen);
        ev.attach();
        for (int i = 0; i < gnum * 7; ++i) g.rand_u32();
        g.begin();
        int guard = 0;
        while (g.outcome() == wsg::Outcome::None && guard++ < 5000) {
            wsg::AiStep st;
            double aggr = g.current_side() == 4 ? 0.4 : 0.5;
            while (ev.has_message()) {           // lecture des dialogues
                if (!ev.message().options.empty()) ev.choose(0); else ev.pop_message();
                ++msgs;
            }
            if (!wsg::ai_step(g, aggr, st)) g.end_turn_of_current_side();
        }
        while (ev.has_message()) { if (!ev.message().options.empty()) ev.choose(0); else ev.pop_message(); ++msgs; }
        fired += ev.fired_count();
        for (auto& kv : ev.unsupported()) unsup[kv.first] += kv.second;
        if (g.outcome() == wsg::Outcome::Victory) ++vic;
        else if (g.outcome() == wsg::Outcome::Defeat) ++def;
        else ++other;
        if (gnum < 3) printf("partie %d : %s (%s) au tour %d\n", gnum,
                             g.outcome() == wsg::Outcome::Victory ? "victoire" : g.outcome() == wsg::Outcome::Defeat ? "défaite" : "?",
                             g.outcome_reason().c_str(), g.turn());
    }
    printf("%d parties IA/IA : %d victoires, %d défaites, %d indécises\n", games, vic, def, other);
    printf("événements déclenchés: %d, messages affichés: %d\n", fired, msgs);
    for (auto& kv : unsup) printf("  non pris en charge: %s x%d\n", kv.first.c_str(), kv.second);
    return 0;
}
