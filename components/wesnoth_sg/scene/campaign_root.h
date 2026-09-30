// scene/campaign_root.h — racine SD de la campagne active, choisie au menu
// titre (voir StoryScene::update_campaign_select). Toutes les campagnes
// suivent la même arborescence (campaign_bilingual.json, manifest.json,
// data/scenarios/, gfx/, images/, audio/, maps/) sous des dossiers SD
// distincts, pour ne jamais mélanger sauvegardes/assets de deux campagnes.
#pragma once
#include <string>

namespace wesnoth_sg {

struct CampaignInfo {
    const char* sd_folder;   // dossier sous /sdcard/, ex. "WESNOTH_SG"
    const char* title;       // affiché au menu de choix de campagne
    const char* title_fr;    // idem, traduction officielle Wesnoth (po/wesnoth-tsg,
                              // wesnoth-tb/fr.po) -- lu quand campaign_state().lang_fr
                              // est vrai, repli sur title sinon (voir story_scene.cpp).
};

// Ordre = ordre d'affichage au menu. Ajouter une campagne ici + son dossier
// sur la carte SD suffit à la rendre choisissable (aucun autre code à
// toucher : tous les chemins passent par sd_root()).
inline const CampaignInfo kCampaigns[] = {
    {"WESNOTH_SG", "The South Guard", "La Garde Sud"},
    {"WESNOTH_TB", "A Tale of Two Brothers", "L'Histoire de deux frères"},
};
inline constexpr int kCampaignCount = 2;

// Dossier SD de la campagne actuellement choisie (WESNOTH_SG par défaut :
// comportement inchangé si le menu de choix n'a pas encore été affiché,
// utile aussi pour les tests qui n'entrent pas par StoryScene::enter()).
extern std::string g_campaign_root;

inline std::string sd_root() { return "/sdcard/" + g_campaign_root + "/"; }

}  // namespace wesnoth_sg
