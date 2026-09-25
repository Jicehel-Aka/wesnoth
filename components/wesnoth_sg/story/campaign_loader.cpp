// campaign_loader.cpp
#include "campaign_loader.h"
#include "platform/gb_port.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "cJSON.h"
#include "esp_log.h"

static const char* TAG = "campaign_loader";

// Lit un fichier entier en RAM. Les JSON de campagne pèsent quelques dizaines
// de Ko (texte seul, pas d'audio dedans) donc un chargement complet en RAM
// est raisonnable même sur ESP32-S3 sans PSRAM dédiée à ça.
static char* read_whole_file(const char* path, size_t* out_len) {
    FILE* f = fopen(gb::sd_path(path).c_str(), "rb");
    if (!f) {
        ESP_LOGE(TAG, "Impossible d'ouvrir %s", path);
        return nullptr;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        return nullptr;
    }
    char* buf = static_cast<char*>(malloc(size + 1));
    if (!buf) {
        ESP_LOGE(TAG, "malloc(%ld) echoue pour %s", size, path);
        fclose(f);
        return nullptr;
    }
    size_t read = fread(buf, 1, size, f);
    fclose(f);
    buf[read] = '\0';
    if (out_len) *out_len = read;
    return buf;
}

static BeatType parse_beat_type(const char* s) {
    if (!s) return BeatType::Dialogue;
    if (strcmp(s, "story_screen") == 0) return BeatType::StoryScreen;
    if (strcmp(s, "objectives") == 0) return BeatType::Objectives;
    if (strcmp(s, "character_enters") == 0) return BeatType::CharacterEnters;
    return BeatType::Dialogue;
}

static std::string cjson_str(cJSON* obj, const char* key, const char* fallback = "") {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (item && cJSON_IsString(item) && item->valuestring) {
        return std::string(item->valuestring);
    }
    return std::string(fallback);
}

static bool cjson_bool(cJSON* obj, const char* key) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return item && cJSON_IsBool(item) && cJSON_IsTrue(item);
}

// Construit une clé "scenario_id#beat_index" pour retrouver le fichier audio
// correspondant dans le manifest — même convention que generate_audio.py.
static std::string beat_key(const std::string& scenario_id, int beat_index) {
    return scenario_id + "#" + std::to_string(beat_index);
}

bool campaign_load(const char* campaign_json_path,
                    const char* audio_manifest_json_path,
                    Campaign* out) {
    if (!out) return false;

    size_t len = 0;
    char* raw = read_whole_file(campaign_json_path, &len);
    if (!raw) return false;

    cJSON* root = cJSON_ParseWithLength(raw, len);
    if (!root) {
        // cJSON_GetErrorPtr() pointe A L'INTERIEUR de `raw` -- ne jamais
        // liberer `raw` avant de l'avoir lu (use-after-free trouve par le
        // test hote sous AddressSanitizer, cf. echange du <2026-09-14>).
        ESP_LOGE(TAG, "JSON invalide : %s", cJSON_GetErrorPtr());
        free(raw);
        return false;
    }
    free(raw);

    // --- Charge le manifeste audio (optionnel : si absent, audio_path reste vide) ---
    // clé "scenario#beat_index" -> chemin fichier, construite à partir des
    // champs "scenario" et "beat_index" de chaque entrée "lines" du manifest.
    cJSON* manifest_root = nullptr;
    char* manifest_raw = audio_manifest_json_path ? read_whole_file(audio_manifest_json_path, nullptr) : nullptr;
    if (manifest_raw) {
        manifest_root = cJSON_Parse(manifest_raw);
        free(manifest_raw);
    }

    out->scenarios.clear();

    cJSON* scenarios_json = cJSON_GetObjectItemCaseSensitive(root, "scenarios");
    // Compat : un fichier "un seul scenario" (pas de campagne chaînée) a les
    // champs directement à la racine plutôt que sous "scenarios".
    cJSON single_wrapper;
    bool is_single = false;
    if (!scenarios_json) {
        is_single = true;
    }

    auto parse_scenario_obj = [&](cJSON* scen_obj) {
        Scenario scenario;
        scenario.id = cjson_str(scen_obj, "id");
        scenario.name = cjson_str(scen_obj, "name");
        scenario.next_scenario = cjson_str(scen_obj, "next_scenario");

        cJSON* beats = cJSON_GetObjectItemCaseSensitive(scen_obj, "beats");
        int idx = 0;
        cJSON* beat_obj = nullptr;
        cJSON_ArrayForEach(beat_obj, beats) {
            Beat beat;
            beat.type = parse_beat_type(cjson_str(beat_obj, "type").c_str());
            beat.speaker = cjson_str(beat_obj, "speaker");
            beat.text_en = cjson_str(beat_obj, "text_en", cjson_str(beat_obj, "text").c_str());
            beat.text_fr = cjson_str(beat_obj, "text_fr");
            beat.has_fr = !beat.text_fr.empty();
            // "image" pour les story_screen, "portrait" pour les dialogues
            // (cf. wml_story_parser.py) -- les deux tombent dans le meme
            // champ Beat::image cote firmware, peu importe lequel des deux
            // etait renseigne cote JSON.
            beat.image = cjson_str(beat_obj, "image");
            if (beat.image.empty()) {
                beat.image = cjson_str(beat_obj, "portrait");
            }
            beat.show_title = cjson_bool(beat_obj, "show_title");

            if (manifest_root) {
                cJSON* lines = cJSON_GetObjectItemCaseSensitive(manifest_root, "lines");
                cJSON* line_obj = nullptr;
                cJSON_ArrayForEach(line_obj, lines) {
                    if (cJSON_GetObjectItemCaseSensitive(line_obj, "scenario") &&
                        cjson_str(line_obj, "scenario") == scenario.id &&
                        cJSON_GetObjectItemCaseSensitive(line_obj, "beat_index") &&
                        cJSON_GetObjectItemCaseSensitive(line_obj, "beat_index")->valueint == idx) {
                        beat.audio_path = cjson_str(line_obj, "file");
                        break;
                    }
                }
            }

            scenario.beats.push_back(std::move(beat));
            idx++;
        }
        out->scenarios.push_back(std::move(scenario));
    };

    if (is_single) {
        parse_scenario_obj(root);
    } else {
        cJSON* scen_obj = nullptr;
        cJSON_ArrayForEach(scen_obj, scenarios_json) {
            parse_scenario_obj(scen_obj);
        }
    }

    if (manifest_root) cJSON_Delete(manifest_root);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "%d scenario(s) charges depuis %s", (int)out->scenarios.size(), campaign_json_path);
    return !out->scenarios.empty();
}
