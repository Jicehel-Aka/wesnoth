// battle/campaign_state.h — état persistant entre deux scénarios.
//
// Wesnoth reporte le camp du joueur d'un scénario à l'autre par son moteur
// (save_id="player_side"), pas par du WML de campagne : tous les survivants
// du camp 1 repartent avec leurs PV, leur XP et leurs traits exacts. Le
// texte du scénario s'appuie dessus de deux façons :
//   - explicitement, via [recall] id=... x=... y=... (macro RECALL_XY) pour
//     les personnages de l'histoire (Deoran, Ethiliel, le compagnon choisi) ;
//   - implicitement, via le menu de rappel du joueur (Alt+R dans Wesnoth)
//     pour les troupes recrutées ordinaires.
// CampaignState reproduit cette liste unique (un seul camp humain dans
// cette campagne) : le champ recallable distingue les deux usages ci-dessus
// (voir Unit::recallable dans game.h).
#pragma once
#include <string>
#include <vector>

namespace wsg {

struct PersistentUnit {
    std::string type_id, id, name, role;
    std::vector<std::string> traits;
    int side = 1, hp = 1, xp = 0;
    bool canrecruit = false, recallable = false;
};

struct CampaignState {
    std::vector<PersistentUnit> roster;   // camp 1 tel qu'à la fin du dernier scénario joué
    bool has_gold = false;
    int gold = 0;                          // or reporté (déjà calculé : bonus + pourcentage)

    // Bascule "Astuces : Activées/Désactivées" du menu de bataille (voir
    // battle_scene.cpp). Persiste d'un scénario à l'autre comme le reste de
    // cet état. Seuls les messages marqués tip="yes" (WNode) sont concernés
    // -- voir EventEngine::queue_message dans events.cpp.
    bool tips_enabled = true;

    // Miroir de StoryScene/BattleScene::language_ (scene/language.h),
    // synchronisé par set_language() des deux scènes. Existe ici pour que
    // les fonctions libres sans accès à l'instance de scène (uname() dans
    // battle_scene.cpp, la substitution de $unit.name/$second_unit.name
    // dans events.cpp) puissent choisir UnitTypeDef::name vs name_fr sans
    // se passer la langue en paramètre partout.
    bool lang_fr = true;
};

CampaignState& campaign_state();

}  // namespace wsg
