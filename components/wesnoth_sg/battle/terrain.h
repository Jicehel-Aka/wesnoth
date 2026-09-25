// battle/terrain.h
//
// Codes de terrain Wesnoth (voir data/core/terrain.cfg officiel) réduits
// aux types qui apparaissent réellement dans The South Guard. Coûts de
// déplacement et bonus de défense repris des valeurs publiques réelles du
// jeu (data/core/terrain.cfg + unit_type movement_costs/defense), pas
// inventés -- mais réduits à 3 classes de terrain plutôt que la trentaine
// de sous-variantes de Wesnoth (ex: toutes les forêts -> Forest), pour un
// premier moteur jouable sans devoir encoder tout terrain.cfg.
#pragma once
#include <string>
#include <unordered_map>

enum class TerrainClass {
    Grassland,   // Gg, Gs... + Aa (variante "automne" -- meme comportement de jeu que Gg, seul le visuel change selon la saison, a verifier mais coherent avec l'usage de ce mot dans les cartes)
    Road,        // Rr, Re
    Hills,       // Hh
    Mountains,   // Mm
    Forest,      // Ff, Gg^F*, Aa^F*
    Village,     // *^V*
    Castle,      // Ce, Ch, Cud, Cv... (regroupe plusieurs variantes de forteresse/camp)
    Keep,        // Kh, Ke
    Water,       // Ww, Wo
    Sand,        // Dd (desert)
    Swamp,       // Ss -- estimation raisonnable (S non confirmee contre terrain.cfg officiel), boueux/lent
    Cave,        // Uu -- souterrain, meilleure estimation (traite comme un terrain difficile type collines)
    Unwalkable,  // Xu, Xm (impassable -- convention standard Wesnoth pour le prefixe X)
};

struct TerrainInfo {
    TerrainClass cls;
    // Coût de déplacement en points de mouvement pour une unité "à pied"
    // générique. Wesnoth distingue par classe d'unité (cavalerie/pied/vol) ;
    // simplifié ici à un seul profil "infanterie" pour le premier jet --
    // à affiner par type d'unité si besoin plus tard.
    int move_cost;
    // Bonus de défense en % (Wesnoth : plus haut = meilleur, ex. 60 = 60%
    // de chance d'éviter un coup). Valeurs reprises de terrain.cfg réel
    // pour un fantassin standard.
    int defense_bonus;
};

inline const std::unordered_map<TerrainClass, TerrainInfo>& terrain_table() {
    static const std::unordered_map<TerrainClass, TerrainInfo> table = {
        {TerrainClass::Grassland,  {TerrainClass::Grassland,  1, 30}},
        {TerrainClass::Road,       {TerrainClass::Road,       1, 20}},
        {TerrainClass::Hills,      {TerrainClass::Hills,      2, 50}},
        {TerrainClass::Mountains,  {TerrainClass::Mountains,  3, 60}},
        {TerrainClass::Forest,     {TerrainClass::Forest,     2, 50}},
        {TerrainClass::Village,    {TerrainClass::Village,    1, 40}},
        {TerrainClass::Castle,     {TerrainClass::Castle,     1, 40}},
        {TerrainClass::Keep,       {TerrainClass::Keep,       1, 40}},
        {TerrainClass::Water,      {TerrainClass::Water,      99, 20}},  // infranchissable a pied
        {TerrainClass::Sand,       {TerrainClass::Sand,       2, 20}},
        {TerrainClass::Swamp,      {TerrainClass::Swamp,      2, 20}},   // estimation : lent, peu de couverture
        {TerrainClass::Cave,       {TerrainClass::Cave,       2, 40}},   // estimation : proche des collines
        {TerrainClass::Unwalkable, {TerrainClass::Unwalkable, 99, 0}},
    };
    return table;
}

// Décode le code de terrain WML (ex: "Gg", "Hh", "Ce", "Gg^Fms", "Wo") en
// TerrainClass. Ne lit que la partie base (avant le "^") pour la plupart
// des cas, sauf overlay foret/village qui priment sur le terrain de base
// (comme dans Wesnoth : une case "forêt sur herbe" se comporte comme une
// forêt pour le mouvement/la défense).
inline TerrainClass parse_terrain_code(const std::string& code) {
    std::string base = code.substr(0, code.find('^'));
    std::string overlay = code.find('^') != std::string::npos
        ? code.substr(code.find('^') + 1) : "";

    if (!overlay.empty() && overlay[0] == 'F') return TerrainClass::Forest;
    if (!overlay.empty() && overlay[0] == 'V') return TerrainClass::Village;

    if (base.empty()) return TerrainClass::Grassland;
    char c0 = base[0];
    switch (c0) {
        case 'G': return TerrainClass::Grassland;
        case 'A': return TerrainClass::Grassland;  // variante saisonniere (automne) -- meme comportement
        case 'R': return TerrainClass::Road;
        case 'H': return TerrainClass::Hills;
        case 'M': return TerrainClass::Mountains;
        case 'F': return TerrainClass::Forest;
        case 'C': return TerrainClass::Castle;
        case 'K': return TerrainClass::Keep;
        case 'W': return TerrainClass::Water;
        case 'D': return TerrainClass::Sand;
        case 'S': return TerrainClass::Swamp;
        case 'U': return TerrainClass::Cave;
        case 'X': return TerrainClass::Unwalkable;
        case 'I': return TerrainClass::Cave;  // glace/grotte -- estimation, rare (I, 42 occurrences)
        case 'T': return TerrainClass::Grassland;  // tres rare (6 occurrences), repli neutre
        default:  return TerrainClass::Grassland;  // repli neutre plutôt qu'un crash
    }
}
