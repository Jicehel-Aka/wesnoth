// battle/combat.h
//
// Résolution des combats selon la formule réelle de Wesnoth :
//   chance de toucher l'ADVERSAIRE = 100% - bonus_defense_terrain(adversaire)
// (Wesnoth a aussi "marksman" qui plafonne à 60% min, "magique" qui fixe à
// 70% -- non gérés ici, roster du scénario 1-2 n'en a pas.)
// Chaque unité frappe avec l'arme qu'elle choisit (l'attaquant choisit,
// puis le défenseur riposte avec sa MEILLEURE arme de même portée si elle
// en a une -- sinon pas de riposte, comme en vrai Wesnoth).
#pragma once
#include <cstdlib>
#include <algorithm>
#include "unit_instance.h"
#include "terrain.h"

struct CombatResult {
    int attacker_damage_dealt = 0;
    int defender_damage_dealt = 0;
    int attacker_hits = 0, attacker_strikes = 0;
    int defender_hits = 0, defender_strikes = 0;
};

inline int apply_resistance(int base_damage, const UnitType& defender, DamageType dt) {
    auto it = defender.resistances.find(dt);
    int resist_pct = (it != defender.resistances.end()) ? it->second : 0;
    int dmg = base_damage - (base_damage * resist_pct) / 100;
    return std::max(dmg, 0);
}

// Choisit la meilleure arme de riposte du défenseur : même portée
// (mêlée/distance) que l'attaque reçue, dégâts totaux (dmg*count) les plus
// élevés. Renvoie nullptr si aucune arme de portée compatible.
inline const AttackType* pick_counter_attack(const UnitType& defender, bool attack_is_ranged) {
    const AttackType* best = nullptr;
    for (const auto& atk : defender.attacks) {
        if (atk.ranged != attack_is_ranged) continue;
        if (!best || atk.damage * atk.count > best->damage * best->count) best = &atk;
    }
    return best;
}

// simple_rand : remplaçable par le vrai générateur matériel plus tard
// (ESP32 a un vrai TRNG -- esp_random()) ; ici rand() suffit pour valider
// la logique côté hôte.
inline bool roll_hit(int hit_chance_pct) {
    return (std::rand() % 100) < hit_chance_pct;
}

inline CombatResult resolve_combat(UnitInstance& attacker, UnitInstance& defender,
                                    const AttackType& attacker_weapon,
                                    const BattleMap& map) {
    CombatResult result;
    const UnitType& atk_type = attacker.type();
    const UnitType& def_type = defender.type();

    int def_terrain_bonus = terrain_table().at(map.terrain_at(defender.pos)).defense_bonus;
    int atk_terrain_bonus = terrain_table().at(map.terrain_at(attacker.pos)).defense_bonus;

    int chance_to_hit_defender = 100 - def_terrain_bonus;
    const AttackType* counter = pick_counter_attack(def_type, attacker_weapon.ranged);
    int chance_to_hit_attacker = counter ? (100 - atk_terrain_bonus) : 0;

    // Wesnoth alterne les coups (attaquant, défenseur, attaquant...) mais le
    // résultat net est identique à les résoudre en deux passes pour ce
    // moteur simplifié (pas d'effet d'ordre comme "berserk" ici).
    result.attacker_strikes = attacker_weapon.count;
    for (int i = 0; i < attacker_weapon.count && defender.alive(); ++i) {
        if (roll_hit(chance_to_hit_defender)) {
            int dmg = apply_resistance(attacker_weapon.damage, def_type, attacker_weapon.type);
            defender.hp = std::max(0, defender.hp - dmg);
            result.attacker_damage_dealt += dmg;
            result.attacker_hits++;
        }
    }

    if (counter) {
        result.defender_strikes = counter->count;
        for (int i = 0; i < counter->count && attacker.alive(); ++i) {
            if (roll_hit(chance_to_hit_attacker)) {
                int dmg = apply_resistance(counter->damage, atk_type, counter->type);
                attacker.hp = std::max(0, attacker.hp - dmg);
                result.defender_damage_dealt += dmg;
                result.defender_hits++;
            }
        }
    }

    return result;
}
