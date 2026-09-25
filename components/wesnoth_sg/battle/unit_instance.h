// battle/unit_instance.h
#pragma once
#include <string>
#include <unordered_map>
#include <queue>
#include "hex.h"
#include "unit.h"
#include "battle_map.h"
#include "terrain.h"

struct UnitInstance {
    std::string type_id;   // clé dans unit_type_table()
    std::string name;       // "Deoran", "Urza Mathin", ou vide pour anonyme
    int side = 1;            // 1 = joueur, 2 = ennemi (etendable si besoin)
    HexCoord pos;
    int hp = 0;               // PV actuels (initialisés au max du type a la creation)
    int moves_left = 0;
    bool acted_this_turn = false;

    const UnitType& type() const { return unit_type_table().at(type_id); }
    bool alive() const { return hp > 0; }
};

// Portée de déplacement (Dijkstra simplifié -- les coûts de terrain sont
// petits entiers positifs, une simple relaxation BFS-par-coût suffit sans
// vraie file de priorité). N'implémente PAS encore la zone de contrôle
// (unités ennemies adjacentes bloquant le passage) -- prochaine étape une
// fois la version de base validée.
inline std::unordered_map<int, int> movement_range(
    const BattleMap& map, HexCoord start, int max_moves) {
    // clé = x*10000+y (assez pour des cartes Wesnoth, qui ne dépassent
    // jamais 200 de large)
    auto key = [](HexCoord h) { return h.x * 10000 + h.y; };

    std::unordered_map<int, int> cost_so_far;
    cost_so_far[key(start)] = 0;
    std::queue<HexCoord> frontier;
    frontier.push(start);

    while (!frontier.empty()) {
        HexCoord cur = frontier.front(); frontier.pop();
        int cur_cost = cost_so_far[key(cur)];

        for (HexCoord next : hex_neighbors(cur)) {
            if (!map.in_bounds(next)) continue;
            const TerrainInfo& info = terrain_table().at(map.terrain_at(next));
            int new_cost = cur_cost + info.move_cost;
            if (new_cost > max_moves) continue;

            auto it = cost_so_far.find(key(next));
            if (it == cost_so_far.end() || new_cost < it->second) {
                cost_so_far[key(next)] = new_cost;
                frontier.push(next);
            }
        }
    }
    return cost_so_far;  // clé encodée -> cout ; le beat/scene la decode au besoin
}
