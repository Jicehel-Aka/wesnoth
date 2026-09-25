// battle/unit.h
//
// Statistiques réelles des types d'unité de Wesnoth (data/core/units/*.cfg
// officiel) pour le roster qui apparaît dans les scénarios 1-2 de The South
// Guard. Pas de valeurs inventées -- juste réduites à ce dont on a besoin
// (HP, mouvement, attaques, résistances principales), sans les traits,
// l'expérience/promotion, ni les capacités spéciales (berserk, marksman...)
// pour ce premier moteur.
#pragma once
#include <string>
#include <vector>
#include <unordered_map>

enum class DamageType { Blade, Pierce, Impact, Fire, Cold, Arcane };

struct AttackType {
    std::string name;
    int damage;
    int count;       // nombre de coups
    DamageType type;
    bool ranged;      // melee=false, ranged (arc/lance...) = true
};

struct UnitType {
    std::string id;
    int hp;
    int moves;        // points de mouvement par tour
    std::vector<AttackType> attacks;
    // Résistance en % (positif = réduit les dégâts reçus de ce type,
    // négatif = vulnérabilité). 0 = valeur par défaut Wesnoth.
    std::unordered_map<DamageType, int> resistances;
};

inline const std::unordered_map<std::string, UnitType>& unit_type_table() {
    static const std::unordered_map<std::string, UnitType> table = {
        {"Peasant", {
            "Peasant", 20, 5,
            {{"Peasant Sickle", 5, 1, DamageType::Blade, false}},
            {}
        }},
        {"Spearman", {
            "Spearman", 36, 5,
            {{"Spear", 6, 3, DamageType::Pierce, false}},
            {{DamageType::Pierce, 10}}
        }},
        {"Bowman", {
            "Bowman", 32, 6,
            {{"Short Sword", 5, 3, DamageType::Blade, false},
             {"Bow", 8, 2, DamageType::Pierce, true}},
            {}
        }},
        {"Horseman Commander", {
            "Horseman Commander", 42, 9,
            {{"Sword", 8, 3, DamageType::Blade, false},
             {"Lance", 15, 2, DamageType::Pierce, false}},
            {}
        }},
        {"Infantry Lieutenant", {
            "Infantry Lieutenant", 44, 5,
            {{"Sword", 8, 4, DamageType::Blade, false}},
            {}
        }},
        {"Bandit", {
            "Bandit", 32, 5,
            {{"Footpad Club", 7, 3, DamageType::Impact, false}},
            {}
        }},
        {"Outlaw", {
            "Outlaw", 32, 6,
            {{"Sword", 7, 3, DamageType::Blade, false},
             {"Crossbow", 5, 2, DamageType::Pierce, true}},
            {}
        }},
        {"Thug", {
            "Thug", 32, 4,
            {{"Club", 8, 2, DamageType::Impact, false}},
            {}
        }},
        {"Footpad", {
            "Footpad", 13, 6,
            {{"Club", 3, 2, DamageType::Impact, false}},
            {}
        }},
        {"Poacher", {
            "Poacher", 12, 5,
            {{"Crossbow", 5, 2, DamageType::Pierce, true}},
            {}
        }},
        {"Thief", {
            "Thief", 17, 6,
            {{"Dagger", 4, 3, DamageType::Blade, false}},
            {}
        }},
        {"White Mage", {
            "White Mage", 27, 5,
            {{"Staff", 5, 3, DamageType::Impact, false},
             {"Lightbeam", 7, 4, DamageType::Arcane, true}},
            {}
        }},
    };
    return table;
}
