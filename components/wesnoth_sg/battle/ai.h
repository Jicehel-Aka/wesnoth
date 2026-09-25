// battle/ai.h — IA des camps non humains.
//
// Inspirée des grandes étapes de l'IA par défaut de Wesnoth (RCA) :
//   1. recrutement par le chef depuis son donjon ;
//   2. attaques évaluées sur la prévision exacte de combat (dégâts espérés,
//      probabilité de tuer, risque de mourir pondéré par l'agressivité,
//      défense du terrain d'où l'on frappe) ;
//   3. retraite vers un village quand l'unité est blessée ;
//   4. capture de villages et avancée vers l'ennemi.
// Comportements spéciaux repris des scénarios : garde (guardian), garde de
// zone (micro-IA zone_guardian), chef qui ne quitte pas son donjon.
#pragma once
#include <string>
#include <vector>

#include "battle/game.h"

namespace wsg {

struct AiStep {            // une action jouée, pour l'animation/le journal
    int unit_uid = 0;
    HexCoord from, to;
    int target_uid = 0;    // 0 = pas d'attaque
    std::string text;
};

// Joue une action de l'IA pour le camp courant. Renvoie false quand le camp
// n'a plus rien à faire (l'appelant termine alors le tour).
bool ai_step(Game& g, double aggression, AiStep& out);

}  // namespace wsg
