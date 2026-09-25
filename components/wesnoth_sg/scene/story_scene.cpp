// scene/story_scene.cpp
#include "scene/story_scene.h"
#include "scene/battle_scene.h"

#include <algorithm>
#include <cctype>

#include "audio/story_audio.h"
#include "platform/gb_port.h"

#if defined(ESP_PLATFORM)
#include "aka_runtime/aka_runtime.h"
#endif

namespace wesnoth_sg {

namespace {
// Dérive un nom de fichier portrait par défaut depuis le nom du personnage
// ("Sir Gerrick" -> "sir-gerrick.png"), pour les répliques qui n'ont pas de
// portrait explicite dans le WML d'origine (la plupart -- Wesnoth utilise
// alors le portrait par défaut du personnage, pas encore propagé par notre
// parseur Python). Pas de portrait pour narrator/unit/second_unit : ce ne
// sont pas des personnages nommés.
std::string default_portrait_for(const std::string& speaker) {
    if (speaker.empty() || speaker == "narrator" || speaker == "unit" || speaker == "second_unit") {
        return "";
    }
    std::string slug;
    for (char c : speaker) {
        if (c == ' ') slug += '-';
        else if (isalnum((unsigned char)c)) slug += (char)tolower((unsigned char)c);
    }
    return "portraits/" + slug + ".png";
}

// Notre moteur ne décode que du BMP (voir gb_port_aka.cpp) ; le WML
// d'origine référence des .jpg/.png -- on bascule l'extension
// automatiquement, donc générer/déposer les fichiers en .bmp sous le même
// nom de base suffit (pas besoin de toucher au JSON).
std::string as_bmp(const std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot != std::string::npos && dot + 1 < path.size()) {
        std::string ext = path.substr(dot);
        if (ext == ".jpg" || ext == ".png" || ext == ".webp" || ext == ".jpeg") return path.substr(0, dot) + ".bmp";
    }
    return path;
}
}  // namespace

StoryScene& story_scene() { static StoryScene s; return s; }

// Nombre de lignes que produira gb::text_wrapped (coupure aux espaces et aux
// retours à la ligne), pour dimensionner la boîte avant de dessiner.
static int count_wrapped_lines(const std::string& s, int max_w) {
    int lines = 0;
    size_t start = 0;
    while (start <= s.size()) {
        size_t nl = s.find('\n', start);
        std::string para = s.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        std::string cur;
        size_t i = 0;
        int para_lines = 1;
        while (i < para.size()) {
            size_t sp = para.find(' ', i);
            std::string w = para.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
            std::string trial = cur.empty() ? w : cur + " " + w;
            if (!cur.empty() && gb::text_width(trial.c_str()) > max_w) { ++para_lines; cur = w; }
            else cur = trial;
            if (sp == std::string::npos) break;
            i = sp + 1;
        }
        lines += para_lines;
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return lines;
}

// TODO(à ajuster avec de vrais assets) : chemins de campagne sur SD. Suit la
// convention akaRuntime : /sdcard/<gameId>/... -- gameId="WESNOTH_SG" fixé
// dans main.cpp (akaRuntime.begin("WESNOTH_SG")).
static constexpr const char* kCampaignPath = "/sdcard/WESNOTH_SG/campaign_bilingual.json";
static constexpr const char* kManifestPath = "/sdcard/WESNOTH_SG/manifest.json";

void StoryScene::enter() {
    if (loaded_) return;
    loaded_ = campaign_load(kCampaignPath, kManifestPath, &campaign_);
    if (!loaded_) {
        gb::log("StoryScene: echec chargement campagne");
        return;
    }
    story_audio::init();
    scenario_index_ = 0;
    beat_index_ = 0;
    beat_just_entered_ = true;
}

const std::string& StoryScene::current_text(const Beat& beat) const {
    if (language_ == Language::French && beat.has_fr) {
        return beat.text_fr;
    }
    return beat.text_en;
}

void StoryScene::update(SceneManager& mgr) {
    if (!loaded_) {
        mgr.set(SceneId::QUIT);
        return;
    }

#if defined(ESP_PLATFORM)
    // La langue est choisie dans le menu système d'akaRuntime (déjà géré,
    // pas de sélecteur à réécrire) -- on se contente de la lire.
    const char* lang = akaRuntime.getLanguage();
    language_ = (lang && lang[0] == 'f') ? Language::French : Language::English;
#endif

    if (!pending_resume_.empty()) {
        std::string id = pending_resume_;
        pending_resume_.clear();
        need_skip_ = false;
        go_to(id, mgr);
        return;
    }
    if (need_skip_) {                 // premier passage : sauter ce qui est joué en bataille
        need_skip_ = false;
        skip_hidden(mgr);
        return;
    }
    if (scenario_index_ >= campaign_.scenarios.size()) {
        mgr.set(SceneId::QUIT);
        return;
    }
    Scenario& scenario = campaign_.scenarios[scenario_index_];
    if (beat_index_ >= scenario.beats.size()) {
        advance_to_next_beat(mgr);
        return;
    }
    const Beat& beat = scenario.beats[beat_index_];

    if (beat_just_entered_) {
        if (!beat.audio_path.empty()) {
            std::string full_path = std::string("/sdcard/WESNOTH_SG/audio/") + beat.audio_path;
            story_audio::play_line(full_path);
        }
        beat_just_entered_ = false;
    }

    if (gb::buttons_pressed() & gb::BTN_A) {
        advance_to_next_beat(mgr);
    }
}

// Récit et bataille : pour un scénario dont la bataille est jouable (JSON
// présent dans data/scenarios/), le récit ne montre que les écrans [story]
// d'ouverture ; les dialogues sont joués par les événements pendant la
// bataille (comme dans Wesnoth). Sinon, tout le scénario est lu comme avant.
bool StoryScene::has_battle(const std::string& id) const {
    std::string p = "/sdcard/WESNOTH_SG/data/scenarios/" + id + ".json";
    return gb::file_exists(p.c_str());
}

bool StoryScene::beat_visible(const Scenario& sc, const Beat& b) const {
    return !has_battle(sc.id) || b.type == BeatType::StoryScreen;
}

void StoryScene::skip_hidden(SceneManager& mgr) {
    Scenario& sc = campaign_.scenarios[scenario_index_];
    while (beat_index_ < sc.beats.size() && !beat_visible(sc, sc.beats[beat_index_])) ++beat_index_;
    if (beat_index_ >= sc.beats.size()) end_of_scenario(mgr);
    beat_just_entered_ = true;
}

void StoryScene::end_of_scenario(SceneManager& mgr) {
    Scenario& sc = campaign_.scenarios[scenario_index_];
    if (has_battle(sc.id)) {
        battle_scene().set_scenario(sc.id);
        mgr.set(SceneId::BATTLE);
        return;
    }
    go_to(sc.next_scenario, mgr);
}

void StoryScene::go_to(const std::string& id, SceneManager& mgr) {
    for (size_t i = 0; i < campaign_.scenarios.size(); ++i) {
        if (campaign_.scenarios[i].id == id) {
            scenario_index_ = i;
            beat_index_ = 0;
            skip_hidden(mgr);
            return;
        }
    }
    gb::log("Fin de campagne chargee");
    mgr.set(SceneId::QUIT);
}

void StoryScene::advance_to_next_beat(SceneManager& mgr) {
    story_audio::stop();
    ++beat_index_;
    skip_hidden(mgr);
}

void story_scene_resume(const std::string& id) { story_scene().resume_at(id); }

void StoryScene::render() {
    if (!loaded_ || scenario_index_ >= campaign_.scenarios.size()) return;
    const Scenario& scenario = campaign_.scenarios[scenario_index_];
    if (beat_index_ >= scenario.beats.size()) return;
    const Beat& beat = scenario.beats[beat_index_];

    const gb::Color kBg = gb::rgb(20, 16, 24);
    const gb::Color kText = gb::rgb(230, 220, 200);
    const gb::Color kSpeaker = gb::rgb(230, 190, 80);
    const gb::Color kBoxBg = gb::rgb(10, 8, 12);

    switch (beat.type) {
        case BeatType::StoryScreen: {
            std::string bg_path = "/sdcard/WESNOTH_SG/images/" + as_bmp(beat.image);
            if (beat.image.empty() || !gb::blit_bmp(bg_path.c_str())) {
                gb::clear(kBg);
            }
            // Bande de texte en bas d'écran, sur le fond narratif : sa hauteur
            // suit la longueur du texte (les longs écrans ne débordent plus).
            const std::string& txt = current_text(beat);
            int lines = count_wrapped_lines(txt, gb::SCREEN_W - 20);
            int top = std::max(4, gb::SCREEN_H - 16 - lines * 12);
            gb::fill_rect(0, top - 8, gb::SCREEN_W, gb::SCREEN_H - top + 8, kBoxBg);
            gb::text_wrapped(10, top, gb::SCREEN_W - 20, 12, txt, kText);
            break;
        }
        case BeatType::Dialogue: {
            const int kPortraitSize = 96;
            const int kBoxTop = gb::SCREEN_H - kPortraitSize - 8;  // 96 + marges haut/bas
            const int kPortraitY = kBoxTop + 4;

            gb::fill_rect(0, kBoxTop, gb::SCREEN_W, gb::SCREEN_H - kBoxTop, kBoxBg);

            std::string portrait_path = beat.image.empty()
                ? default_portrait_for(beat.speaker)
                : beat.image;

            int text_x = 10;
            if (!portrait_path.empty()) {
                std::string full = "/sdcard/WESNOTH_SG/images/" + as_bmp(portrait_path);
                if (gb::draw_image(full.c_str(), 4, kPortraitY)) {
                    text_x = kPortraitSize + 14;  // décale le texte à droite du portrait
                }
            }

            gb::text(text_x, kBoxTop + 5, beat.speaker.c_str(), kSpeaker);
            gb::text_wrapped(text_x, kBoxTop + 20, gb::SCREEN_W - text_x - 10, 12, current_text(beat), kText);
            break;
        }
        case BeatType::Objectives:
        case BeatType::CharacterEnters:
            // Pas de rendu dédié pour l'instant (voir TODO story_player
            // d'origine) -- l'important à ce stade est l'ordre de passage.
            break;
    }
}

}  // namespace wesnoth_sg
