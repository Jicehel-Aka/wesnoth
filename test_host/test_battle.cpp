#include <cstdio>
#include <cstdlib>
#include <ctime>
#include "battle/hex.h"
#include "battle/terrain.h"
#include "battle/battle_map.h"
#include "battle/unit.h"
#include "battle/unit_instance.h"
#include "battle/combat.h"

static void test_hex_math() {
    printf("=== Test coordonnees hexagonales ===\n");
    HexCoord a{5, 5};
    auto neighbors = hex_neighbors(a);
    printf("Voisins de (5,5) [colonne impaire]: ");
    for (auto& n : neighbors) printf("(%d,%d) ", n.x, n.y);
    printf("\n");
    printf("Distance (5,5)->(5,5) = %d (attendu 0)\n", hex_distance(a, a));
    printf("Distance (0,0)->(3,0) = %d\n", hex_distance({0,0}, {3,0}));
    printf("Distance (0,0)->(0,3) = %d\n", hex_distance({0,0}, {0,3}));
}

static void test_terrain_parsing() {
    printf("\n=== Test parsing terrain ===\n");
    struct { const char* code; const char* expect; } cases[] = {
        {"Gg", "Grassland"}, {"Hh", "Hills"}, {"Mm", "Mountains"},
        {"Gg^Fms", "Forest (overlay)"}, {"Ce", "Castle"}, {"Wo", "Water"},
        {"Gg^Vh", "Village (overlay)"},
    };
    for (auto& c : cases) {
        TerrainClass tc = parse_terrain_code(c.code);
        printf("%-8s -> classe=%d (%s)\n", c.code, (int)tc, c.expect);
    }
}

static void test_map_and_movement() {
    printf("\n=== Test carte + portee de deplacement ===\n");
    // Petite carte de test (pas encore la vraie carte du scenario 1, en
    // attente du fichier .map reel) : un couloir avec une riviere et des
    // collines, format identique a un vrai fichier Wesnoth.
    std::string test_map =
        "Gg,Gg,Gg,Hh,Hh,Mm\n"
        "Gg,Gg,Hh,Hh,Mm,Mm\n"
        "Gg,Ce,Gg,Ww,Ww,Hh\n"
        "Gg,Kh,Gg,Ww,Gg,Gg\n"
        "Gg,Gg,Gg,Gg,Gg,Gg\n";

    BattleMap map;
    bool ok = map.load_from_string(test_map);
    printf("Carte chargee: %s (%dx%d)\n", ok ? "OK" : "ECHEC", map.width, map.height);

    UnitInstance deoran;
    deoran.type_id = "Horseman Commander";
    deoran.name = "Deoran";
    deoran.pos = {1, 3};  // sur le donjon (Kh)
    deoran.hp = deoran.type().hp;
    deoran.moves_left = deoran.type().moves;

    auto range = movement_range(map, deoran.pos, deoran.moves_left);
    printf("Deoran (9 pm) depuis (1,3) peut atteindre %zu cases:\n", range.size());
    for (auto& [key, cost] : range) {
        int x = key / 10000, y = key % 10000;
        printf("  (%d,%d) cout=%d\n", x, y, cost);
    }
}

static void test_combat() {
    printf("\n=== Test resolution de combat (100 simulations) ===\n");
    std::string test_map = "Gg,Gg\nGg,Hh\n";
    BattleMap map;
    map.load_from_string(test_map);

    int deoran_wins = 0, bandit_wins = 0, both_survive = 0;
    int total_atk_dmg = 0, total_def_dmg = 0, total_atk_hits = 0, total_def_hits = 0;
    for (int trial = 0; trial < 100; ++trial) {
        UnitInstance deoran;
        deoran.type_id = "Horseman Commander";
        deoran.pos = {0, 0};
        deoran.hp = deoran.type().hp;

        UnitInstance bandit;
        bandit.type_id = "Bandit";
        bandit.pos = {1, 0};
        bandit.hp = bandit.type().hp;

        // Deoran attaque a l'epee (premiere arme de melee)
        const AttackType& weapon = deoran.type().attacks[0];
        CombatResult r = resolve_combat(deoran, bandit, weapon, map);
        total_atk_dmg += r.attacker_damage_dealt;
        total_def_dmg += r.defender_damage_dealt;
        total_atk_hits += r.attacker_hits;
        total_def_hits += r.defender_hits;

        if (!bandit.alive() && deoran.alive()) deoran_wins++;
        else if (!deoran.alive() && bandit.alive()) bandit_wins++;
        else both_survive++;
    }
    printf("Sur 100 combats Deoran vs Bandit (1 assaut) :\n");
    printf("  Bandit tue, Deoran survit : %d\n", deoran_wins);
    printf("  Deoran tue, Bandit survit : %d\n", bandit_wins);
    printf("  Aucun des deux tue        : %d\n", both_survive);
    printf("  Coups de Deoran touches   : %d/300 (%.0f%%, terrain adverse Gg -> defense 30%%, donc ~70%% attendu)\n",
           total_atk_hits, total_atk_hits/3.0);
    printf("  Coups du Bandit touches   : %d/300 (%.0f%%, terrain Deoran Gg -> defense 30%%, donc ~70%% attendu)\n",
           total_def_hits, total_def_hits/3.0);
    printf("  Degats totaux infliges a Bandit: %d / Deoran: %d\n", total_atk_dmg, total_def_dmg);
}

int main() {
    std::srand((unsigned)std::time(nullptr));
    test_hex_math();
    test_terrain_parsing();
    test_map_and_movement();
    test_combat();
    return 0;
}
