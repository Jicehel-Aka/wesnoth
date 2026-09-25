// test_recall.cpp — vérifie le report entre scénarios (roster + or) sur les
// VRAIES données extraites : joue le scénario 1 jusqu'à la victoire, récolte
// le roster/l'or comme le fait BattleScene, puis charge le scénario 2 et
// s'assure que Deoran (et l'or reporté) sont bien présents.
#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
#include "battle/game.h"
#include "battle/ai.h"
#include "battle/events.h"
#include "battle/campaign_state.h"
#include <memory>

static std::string slurp(const std::string& p) {
    std::ifstream f(p); std::stringstream s; s << f.rdbuf(); return s.str();
}

int main(int argc, char** argv) {
    std::string root = argc > 1 ? argv[1] : "sdcard_files/WESNOTH_SG";
    wsg::GameData d;
    assert(d.load(slurp(root + "/data/units.json"), slurp(root + "/data/terrain.json")));

    std::string s1 = slurp(root + "/data/scenarios/01_Born_to_the_Banner.json");
    std::string m1 = slurp(root + "/maps/01b_Born_to_the_Banner.map");
    std::unique_ptr<wsg::Game> pg1;
    std::unique_ptr<wsg::EventEngine> pev1;
    int seed_tries = argc > 2 ? atoi(argv[2]) : 15;
    for (int t = 0; t < seed_tries; ++t) {
        pg1.reset(new wsg::Game(d));
        assert(pg1->load_scenario(s1, m1));
        for (int i = 0; i < t * 11; ++i) pg1->rand_u32();
        pev1.reset(new wsg::EventEngine(*pg1));
        pev1->load(s1);
        pev1->attach();
        pg1->begin();
        for (auto& u : pg1->units())
            if (u.canrecruit && u.side == 1)
                for (auto h : pg1->recruit_hexes(u)) { pg1->recruit(1, "Spearman", h); break; }
        int guard = 0;
        while (pg1->outcome() == wsg::Outcome::None && guard++ < 5000) {
            while (pev1->has_message()) { if (!pev1->message().options.empty()) pev1->choose(0); else pev1->pop_message(); }
            wsg::AiStep st;
            double aggr = pg1->current_side() == 4 ? 0.4 : 0.5;
            if (!wsg::ai_step(*pg1, aggr, st)) pg1->end_turn_of_current_side();
        }
        while (pev1->has_message()) { if (!pev1->message().options.empty()) pev1->choose(0); else pev1->pop_message(); }
        printf("essai %d : %s (%s) au tour %d\n", t, pg1->outcome() == wsg::Outcome::Victory ? "victoire" : "défaite",
               pg1->outcome_reason().c_str(), pg1->turn());
        if (pg1->outcome() == wsg::Outcome::Victory) break;
    }
    wsg::Game& g1 = *pg1;
    assert(g1.outcome() == wsg::Outcome::Victory && "aucune victoire sur les essais -- augmenter seed_tries");
    printf("or a la victoire=%d\n", g1.side(1)->gold);

    // récolte comme le fait BattleScene::check_end()
    wsg::CampaignState& cs = wsg::campaign_state();
    cs.roster = g1.harvest_roster(1);
    cs.gold = g1.compute_carryover_gold(1);
    cs.has_gold = true;
    printf("roster reporté : %zu unité(s), or reporté = %d (bonus=%d, %%=%d)\n", cs.roster.size(), cs.gold,
           g1.gold_carryover_bonus(), g1.gold_carryover_percent());
    for (auto& p : cs.roster)
        printf("  id=%-12s type=%-24s PV=%-3d XP=%-3d recallable=%d canrecruit=%d\n", p.id.c_str(),
               p.type_id.c_str(), p.hp, p.xp, p.recallable, p.canrecruit);
    bool has_deoran = false, has_recruit = false;
    for (auto& p : cs.roster) { if (p.id == "Deoran") has_deoran = true; if (p.recallable) has_recruit = true; }
    assert(has_deoran && "Deoran doit survivre au scénario 1 (défaite sinon)");
    assert(has_recruit && "au moins une recrue générique doit être dans le roster");

    // scénario 2 : Deoran doit apparaître via [recall], l'or doit être celui reporté
    std::string s2 = slurp(root + "/data/scenarios/02_Proven_by_the_Sword.json");
    std::string m2 = slurp(root + "/maps/02_Proven_by_the_Sword.map");
    wsg::Game g2(d);
    assert(g2.load_scenario(s2, m2));
    if (cs.has_gold) g2.side(1)->gold = cs.gold;
    wsg::EventEngine ev2(g2);
    ev2.load(s2);
    ev2.attach();
    int gold_before_begin = g2.side(1)->gold;
    g2.begin();   // déclenche prestart/start -> {RECALL_XY Deoran ...}, spawn de Mari
    while (ev2.has_message()) { if (!ev2.message().options.empty()) ev2.choose(0); else ev2.pop_message(); }
    printf("scénario 2 après begin() : %zu unité(s) sur la carte, or=%d (attendu %d)\n",
           g2.units().size(), g2.side(1)->gold, cs.gold);
    wsg::Unit* deo = g2.unit_by_id("Deoran");
    assert(deo && deo->alive() && "Deoran doit être rappelé au début du scénario 2");
    assert(deo->canrecruit && "Deoran doit rester le chef (canrecruit)");
    assert(g2.side(1)->gold == cs.gold && gold_before_begin == cs.gold && "l'or reporté doit remplacer l'or de départ");
    wsg::Unit* mari = g2.unit_by_id("Mari");
    assert(mari && mari->alive() && "Mari doit être réapparue (personnage recréé, pas reporté)");
    printf("OK : Deoran PV=%d/%d XP=%d, Mari présente, or=%d\n", deo->hp, deo->max_hp, deo->xp, g2.side(1)->gold);
    for (auto& kv : ev2.unsupported()) printf("  non pris en charge (scénario 2): %s x%d\n", kv.first.c_str(), kv.second);

    printf("Test de report reussi.\n");
    return 0;
}
