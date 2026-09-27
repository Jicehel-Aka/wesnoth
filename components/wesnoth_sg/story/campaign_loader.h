// campaign_loader.h
//
// Charge le JSON produit par wml_story_parser.py + merge_translation.py
// (voir /tts/campaign_bilingual.json) et l'expose sous forme de structures
// C++ simples, prêtes à consommer par story_player.
//
// Dépendance : cJSON (composant "json" fourni nativement par ESP-IDF —
// idf_component.yml n'a rien à ajouter, il est dans esp-idf/components/json).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class BeatType {
    Dialogue,
    StoryScreen,
    Objectives,   // affiché mais non bloquant : on l'affiche puis on avance
    CharacterEnters,
};

struct Beat {
    BeatType type;
    std::string speaker;      // vide pour story_screen
    std::string text_en;
    std::string text_fr;      // peut être vide si pas de traduction trouvée
    bool has_fr = false;
    std::string image;        // portrait ou background, chemin relatif SD
    std::string audio_path;   // rempli séparément depuis manifest.json (TTS, anglais)
    std::string audio_path_fr;  // idem, voix française (manifest.json, lang="fr")
    bool show_title = false;  // pour story_screen : affiche le titre de la campagne
};

struct Scenario {
    std::string id;
    std::string name;
    std::string next_scenario;
    std::vector<Beat> beats;
};

struct Campaign {
    std::vector<Scenario> scenarios;
};

// Charge /sdcard/WESNOTH/campaign_bilingual.json + /sdcard/WESNOTH/manifest.json
// (chemins configurables). Renvoie false si la lecture ou le parsing échoue ;
// dans ce cas *out reste dans un état indéterminé et ne doit pas être utilisé.
bool campaign_load(const char* campaign_json_path,
                    const char* audio_manifest_json_path,
                    Campaign* out);
