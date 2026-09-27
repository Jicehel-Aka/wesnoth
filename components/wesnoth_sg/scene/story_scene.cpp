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
    // Le premier écran est le menu titre (Nouvelle partie / Continuer /
    // Charger une sauvegarde), pas directement le premier scénario -- voir
    // story/savegame.h. start_new_game() met en place scenario_index_ etc.
    at_title_ = true;
    title_mode_ = TitleMode::Menu;
    title_sel_ = 0;
}

void StoryScene::start_new_game() {
    at_title_ = false;
    scenario_index_ = 0;
    beat_index_ = 0;
    beat_just_entered_ = true;
    dlg_page_ = 0;
    need_skip_ = true;
}

void StoryScene::start_from_save(int slot, SceneManager& mgr) {
    SaveSlotInfo info = slot_info(slot);
    if (!info.used) return;
    at_title_ = false;
    if (info.kind == "battle") {
        BattleSaveRaw raw = load_battle_raw(slot);
        if (raw.scenario_id.empty()) { at_title_ = true; return; }
        battle_scene().resume_from_autosave(raw.scenario_id);
        mgr.set(SceneId::BATTLE);
    } else {
        std::string next_id;
        if (load_story_point(slot, &next_id, &wsg::campaign_state()) && !next_id.empty()) {
            need_skip_ = false;
            pending_resume_ = next_id;
        } else {
            at_title_ = true;
        }
    }
}

void StoryScene::update_title(SceneManager& mgr) {
    uint32_t p = gb::buttons_pressed();
    if (title_mode_ == TitleMode::Menu) {
        const bool has_continue = slot_info(kAutoSlot).used;
        const int n = 3;
        if (p & gb::BTN_UP) title_sel_ = (title_sel_ + n - 1) % n;
        if (p & gb::BTN_DOWN) title_sel_ = (title_sel_ + 1) % n;
        if (p & gb::BTN_A) {
            if (title_sel_ == 0) {
                start_new_game();
            } else if (title_sel_ == 1) {
                if (has_continue) start_from_save(kAutoSlot, mgr);
            } else {
                load_list_ = list_manual_slots();
                title_mode_ = TitleMode::LoadList;
                title_sel_ = 0;
            }
        }
    } else {  // LoadList
        const int n = (int)load_list_.size() + 1;   // +1 pour "Retour"
        if (p & gb::BTN_UP) title_sel_ = (title_sel_ + n - 1) % n;
        if (p & gb::BTN_DOWN) title_sel_ = (title_sel_ + 1) % n;
        if (p & gb::BTN_B) {
            title_mode_ = TitleMode::Menu;
            title_sel_ = 2;
        } else if (p & gb::BTN_A) {
            if (title_sel_ == (int)load_list_.size()) {
                title_mode_ = TitleMode::Menu;
                title_sel_ = 2;
            } else if (load_list_[title_sel_].used) {
                start_from_save(title_sel_ + 1, mgr);
            }
        }
    }
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
    if (at_title_) { update_title(mgr); return; }

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
        // Voix française si dispo et langue courante = français ; sinon on
        // retombe sur l'anglais (comme pour le texte, cf. current_text()).
        const std::string& path = (language_ == Language::French && !beat.audio_path_fr.empty())
                                       ? beat.audio_path_fr
                                       : beat.audio_path;
        if (!path.empty()) {
            std::string full_path = std::string("/sdcard/WESNOTH_SG/audio/") + path;
            story_audio::play_line(full_path);
        }
        beat_just_entered_ = false;
    }

    if (gb::buttons_pressed() & gb::BTN_A) {
        int max_lines = dialog_max_lines(beat);
        int width = (beat.type == BeatType::StoryScreen)
                        ? gb::SCREEN_W - 20
                        : gb::SCREEN_W - dialog_text_x(beat) - 10;
        int total_lines = (int)gb::wrap_text_lines(width, current_text(beat)).size();
        if ((dlg_page_ + 1) * max_lines < total_lines) {
            ++dlg_page_;  // encore du texte : page suivante avant de passer au beat suivant
        } else {
            advance_to_next_beat(mgr);
        }
    }
}

// Lignes de texte qui tiennent dans la boîte de ce beat sans déborder de
// l'écran. Dialogue : boîte fixe (96px de portrait + marges) en bas d'écran.
// StoryScreen : boîte élastique (voir render()), mais plafonnée à la hauteur
// d'écran -- au-delà, on paginė aussi plutôt que de laisser déborder.
int StoryScene::dialog_max_lines(const Beat& beat) const {
    if (beat.type == BeatType::Dialogue) {
        const int kPortraitSize = 96;
        const int kBoxTop = gb::SCREEN_H - kPortraitSize - 8;
        return std::max(1, (gb::SCREEN_H - (kBoxTop + 20) - 6) / 12);
    }
    return std::max(1, (gb::SCREEN_H - 16 - 4) / 12);
}

int StoryScene::dialog_text_x(const Beat& beat) const {
    const int kPortraitSize = 96;
    std::string portrait_path = beat.image.empty() ? default_portrait_for(beat.speaker) : beat.image;
    if (portrait_path.empty()) return 10;
    std::string full = "/sdcard/WESNOTH_SG/images/" + as_bmp(portrait_path);
    return gb::file_exists(full.c_str()) ? kPortraitSize + 14 : 10;
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
    dlg_page_ = 0;
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

void StoryScene::render_title() {
    const gb::Color kBg = gb::rgb(20, 16, 24);
    const gb::Color kSel = gb::rgb(230, 220, 200);
    const gb::Color kOff = gb::rgb(120, 112, 104);
    const gb::Color kTitle = gb::rgb(230, 190, 80);
    gb::clear(kBg);
    gb::text(96, 40, "The South Guard", kTitle);
    if (title_mode_ == TitleMode::Menu) {
        const bool has_continue = slot_info(kAutoSlot).used;
        const char* items[3] = {"Nouvelle partie", "Continuer", "Charger une sauvegarde"};
        for (int i = 0; i < 3; ++i) {
            gb::Color c = (i == 1 && !has_continue) ? gb::rgb(70, 66, 62) : (i == title_sel_ ? kSel : kOff);
            gb::text(110, 100 + i * 20, items[i], c);
        }
    } else {
        gb::text(40, 70, "Choisir un emplacement :", kSel);
        for (int i = 0; i < (int)load_list_.size(); ++i) {
            const SaveSlotInfo& s = load_list_[i];
            std::string label = "Emplacement " + std::to_string(i + 1) + " : " + (s.used ? s.scenario_name : "(vide)");
            if (s.used && s.turn > 0) label += " (tour " + std::to_string(s.turn) + ")";
            gb::text(40, 92 + i * 18, label.c_str(), i == title_sel_ ? kSel : kOff);
        }
        gb::text(40, 92 + (int)load_list_.size() * 18, "Retour",
                 title_sel_ == (int)load_list_.size() ? kSel : kOff);
    }
}

void StoryScene::render() {
    if (at_title_) { render_title(); return; }
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
            // On efface toujours l'écran avant de dessiner le fond : les BMP
            // d'arrière-plan issus de la conversion des images Wesnoth ne
            // font pas forcément exactement 320x240 (crop/format d'origine
            // variable), et blit_bmp() ne peint que les pixels couverts par
            // l'image. Sans ce clear(), toute zone hors de l'image gardait le
            // contenu de la frame précédente (ex: le texte du tout premier
            // écran narratif restait visible en permanence par-dessus les
            // écrans suivants).
            gb::clear(kBg);
            if (!beat.image.empty()) {
                std::string bg_path = "/sdcard/WESNOTH_SG/images/" + as_bmp(beat.image);
                gb::blit_bmp(bg_path.c_str());
            }
            // Bande de texte en bas d'écran, sur le fond narratif : sa hauteur
            // suit la longueur du texte, plafonnée à l'écran -- au-delà, on
            // paginė (dlg_page_) plutôt que de laisser déborder hors écran.
            const std::string& txt = current_text(beat);
            auto all_lines = gb::wrap_text_lines(gb::SCREEN_W - 20, txt);
            int max_lines = dialog_max_lines(beat);
            int shown_lines = std::min((int)all_lines.size() - dlg_page_ * max_lines, max_lines);
            shown_lines = std::max(0, shown_lines);
            int top = std::max(4, gb::SCREEN_H - 16 - shown_lines * 12);
            gb::fill_rect(0, top - 8, gb::SCREEN_W, gb::SCREEN_H - top + 8, kBoxBg);
            gb::draw_text_lines(10, top, 12, all_lines, kText, dlg_page_ * max_lines, max_lines);
            if ((dlg_page_ + 1) * max_lines < (int)all_lines.size()) {
                gb::text(gb::SCREEN_W - 16, gb::SCREEN_H - 12, "v", kSpeaker);
            }
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
            {
                auto all_lines = gb::wrap_text_lines(gb::SCREEN_W - text_x - 10, current_text(beat));
                int max_lines = dialog_max_lines(beat);
                gb::draw_text_lines(text_x, kBoxTop + 20, 12, all_lines, kText, dlg_page_ * max_lines, max_lines);
                if ((dlg_page_ + 1) * max_lines < (int)all_lines.size()) {
                    gb::text(gb::SCREEN_W - 16, gb::SCREEN_H - 12, "v", kSpeaker);
                }
            }
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
