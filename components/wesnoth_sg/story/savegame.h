// story/savegame.h — sauvegarde/chargement de la progression.
//
// Deux natures de sauvegarde, choisies automatiquement selon le moment où
// elles sont écrites :
//   - "battle"  : bataille en cours, capturée à chaque fin de tour (le
//     joueur ou l'IA). Contient l'état complet du moteur (wsg::Game) et des
//     événements (wsg::EventEngine) -- la reprise retombe exactement là où
//     le tour s'est arrêté, comme une vraie sauvegarde Wesnoth.
//   - "story"   : point de passage entre deux scénarios (juste après une
//     victoire, avant l'écran de récit du scénario suivant). Ne contient
//     que ce qui est reporté d'un scénario à l'autre (roster, or, astuces)
//     et l'identifiant du scénario suivant -- bien plus léger, et suffisant
//     puisqu'un scénario redémarre toujours de zéro.
//
// Emplacements (fichiers plats à la racine de la carte SD, pas de sous-
// dossier à créer) :
//   - kAutoSlot (0)      -> save_auto.json      : sauvegarde automatique,
//     écrasée à chaque fin de tour ET à chaque fin de scénario. Jamais
//     proposée dans la liste des 5 emplacements (elle sert au "Continuer"
//     du menu titre).
//   - 1..kNumManualSlots -> save_slot<N>.json    : sauvegardes manuelles,
//     proposées au joueur uniquement en fin de scénario (jamais en cours de
//     bataille -- voir l'appelant, battle_scene.cpp).
#pragma once
#include <string>
#include <vector>

#include "battle/campaign_state.h"

namespace wsg {
class Game;
class EventEngine;
}

namespace wesnoth_sg {

constexpr int kAutoSlot = 0;
constexpr int kNumManualSlots = 5;

struct SaveSlotInfo {
    bool used = false;
    std::string kind;          // "battle" ou "story", vide si used=false
    std::string scenario_name; // nom affichable du scénario en cours/à venir
    int turn = 0;               // 0 pour un point "story" (pas de bataille en cours)
};

// --- sauvegarde/chargement d'une bataille en cours (mi-scénario) -----------
// scenario_id : identifiant du fichier data/scenarios/<id>.json (nécessaire
// pour recharger la carte/les types d'unité avant de réappliquer l'état).
bool save_battle(int slot, const std::string& scenario_id, const std::string& scenario_name,
                  const wsg::Game& g, const wsg::EventEngine& ev);
// Ne relit QUE les champs JSON : à l'appelant de recréer Game/EventEngine
// (load_scenario + events_->load(sj,/*queue_intro_messages=*/false)) puis
// d'appliquer game->load_state()/events->load_state() avec le contenu lu.
// Renvoie scenario_id vide si l'emplacement est absent ou n'est pas une
// sauvegarde de bataille.
struct BattleSaveRaw { std::string scenario_id; std::string game_json; std::string events_json; };
BattleSaveRaw load_battle_raw(int slot);

// --- sauvegarde/chargement d'un point de reprise entre deux scénarios -----
bool save_story_point(int slot, const std::string& next_scenario_id, const std::string& scenario_name,
                       const wsg::CampaignState& cs);
// Renvoie false si l'emplacement est absent ou n'est pas un point "story".
bool load_story_point(int slot, std::string* next_scenario_id, wsg::CampaignState* cs);

SaveSlotInfo slot_info(int slot);                 // kAutoSlot ou 1..kNumManualSlots
std::vector<SaveSlotInfo> list_manual_slots();    // les 5 emplacements, dans l'ordre
void delete_save(int slot);

}  // namespace wesnoth_sg
