// scene/battle_scene.cpp — écran de bataille.
//
// Commandes (adaptées de Wesnoth à la manette de l'AKA) :
//   Croix  : déplacer le curseur (répétition si maintenue)
//   A      : sélectionner une unité / la déplacer / attaquer (fenêtre de
//            prévision avec choix de l'arme)
//   B      : annuler
//   MENU   : fin de tour, recruter, unité suivante, objectifs
//   RUN    : unité suivante ayant encore des actions (touche « n » de Wesnoth)
#include "scene/battle_scene.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include "audio/story_audio.h"
#include "battle/ai.h"
#include "cJSON.h"
#include "platform/gb_port.h"
#include "story/savegame.h"

namespace wesnoth_sg {

BattleScene& battle_scene() { static BattleScene s; return s; }
void story_scene_resume(const std::string& id);   // scene/story_scene.cpp

namespace {
constexpr int HEX_W = 42, HEX_H = 44, COL_STEP = 31;   // tuiles générées en 42x44
constexpr int TOP = 12;                                   // bandeau du haut
constexpr int BOTTOM = 26;                                // panneau du bas
constexpr int VIEW_H = gb::SCREEN_H - TOP - BOTTOM;
constexpr uint16_t KEY = 0xF81F;
const char* kRoot = "/sdcard/WESNOTH_SG/";

std::string slurp(const std::string& p) {
    std::ifstream f(gb::sd_path(p.c_str()), std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}
std::string safe(std::string n) {
    for (auto& c : n) { if (c == '^') c = '-'; else if (c == ' ') c = '_'; else if (c == ':') c = '+'; }
    return n;
}
std::string color_name(const wsg::Side* s) {
    if (s && !s->color.empty()) return s->color;
    int id = s ? s->id : 1;
    return id == 1 ? "red" : id == 2 ? "blue" : id == 3 ? "green" : "purple";
}
gb::Color side_color(const wsg::Side* s) {
    std::string c = color_name(s);
    if (c == "red") return gb::rgb(230, 30, 30);
    if (c == "blue") return gb::rgb(60, 90, 230);
    if (c == "yellow") return gb::rgb(230, 220, 40);
    if (c == "green") return gb::rgb(90, 200, 90);
    if (c == "purple") return gb::rgb(170, 40, 190);
    return gb::rgb(200, 200, 200);
}
std::string uname(const wsg::Unit& u) { return u.name.empty() ? u.t->name : u.name; }

const gb::Color WHITE = gb::rgb(240, 240, 240), GREY = gb::rgb(150, 150, 150),
                YELLOW = gb::rgb(250, 220, 60), RED = gb::rgb(230, 50, 50),
                GREEN = gb::rgb(60, 210, 60), PANEL = gb::rgb(20, 22, 30),
                BLUE = gb::rgb(90, 140, 255);
}  // namespace

bool Aki::load(const std::string& path) {
    std::string b = slurp(path);
    if (b.size() < 8 || memcmp(b.data(), "AKI1", 4) != 0) return false;
    w = (uint8_t)b[4] | ((uint8_t)b[5] << 8);
    h = (uint8_t)b[6] | ((uint8_t)b[7] << 8);
    if (b.size() < 8 + (size_t)w * h * 2) { w = h = 0; return false; }
    px.resize((size_t)w * h);
    memcpy(px.data(), b.data() + 8, px.size() * 2);
    return true;
}

// ---------------------------------------------------------------------------
bool BattleScene::load() {
    if (!data_ok_) {
        data_ok_ = data_.load(slurp(std::string(kRoot) + "data/units.json"),
                              slurp(std::string(kRoot) + "data/terrain.json"));
        if (!data_ok_) { gb::log("BattleScene: données units/terrain introuvables"); return false; }
    }
    std::string sj = slurp(std::string(kRoot) + "data/scenarios/" + scenario_id_ + ".json");
    cJSON* j = cJSON_Parse(sj.c_str());
    if (!j) { gb::log("BattleScene: scénario introuvable"); return false; }
    const cJSON* mf = cJSON_GetObjectItemCaseSensitive(j, "map_file");
    std::string map_file = (mf && cJSON_IsString(mf)) ? mf->valuestring : "";
    const cJSON* ns = cJSON_GetObjectItemCaseSensitive(j, "next_scenario");
    next_scenario_ = (ns && cJSON_IsString(ns)) ? ns->valuestring : "";
    const cJSON* nm = cJSON_GetObjectItemCaseSensitive(j, "name");
    scenario_name_ = (nm && cJSON_IsString(nm)) ? nm->valuestring : scenario_id_;
    cJSON_Delete(j);
    game_.reset(new wsg::Game(data_));
    if (!game_->load_scenario(sj, slurp(std::string(kRoot) + "maps/" + map_file))) {
        gb::log("BattleScene: échec du chargement du scénario");
        return false;
    }
    end_fired_ = false;
    recalled_roster_idx_.clear();
    msg_audio_pending_ = true;

    // Reprise d'une sauvegarde automatique de bataille (mi-scénario) : ne
    // rejoue ni les messages d'ouverture ni prestart/start/new turn (déjà
    // vus une fois), on réapplique juste l'état exact au tour où on s'est
    // arrêté. Ignoré (reprise silencieusement en scénario neuf) si l'unique
    // autosave présent ne correspond pas au scénario demandé.
    BattleSaveRaw save;
    if (resume_from_autosave_) save = load_battle_raw(kAutoSlot);
    const bool resuming = resume_from_autosave_ && save.scenario_id == scenario_id_;
    resume_from_autosave_ = false;

    events_.reset(new wsg::EventEngine(*game_));
    events_->load(sj, /*queue_intro_messages=*/!resuming);
    events_->attach();
    // Or reporté du scénario précédent (remplace l'or de départ du
    // scénario, comme le fait Wesnoth -- pas de cumul avec la valeur du
    // .json). Absent pour le tout premier scénario joué dans la session.
    if (!resuming && wsg::campaign_state().has_gold) {
        if (wsg::Side* s = game_->side(1)) s->gold = wsg::campaign_state().gold;
    }
    build_canvas();
    if (resuming) {
        cJSON* gj = cJSON_Parse(save.game_json.c_str());
        cJSON* ej = cJSON_Parse(save.events_json.c_str());
        if (gj) { game_->load_state(gj); cJSON_Delete(gj); }
        if (ej) { events_->load_state(ej); cJSON_Delete(ej); }
    } else {
        game_->begin();
    }
    return true;
}

void BattleScene::autosave() {
    if (!loaded_ || !game_ || !events_) return;
    if (events_->has_message()) return;              // état incomplet (dialogue en attente)
    if (game_->outcome() != wsg::Outcome::None) return;  // la bataille est finie, rien à reprendre
    save_battle(kAutoSlot, scenario_id_, scenario_name_, *game_, *events_);
}

void BattleScene::hex_center(HexCoord h, int& cx, int& cy) const {
    cx = h.x * COL_STEP + HEX_W / 2;
    cy = h.y * HEX_H + HEX_H / 2 + ((h.x & 1) ? HEX_H / 2 : 0);
}

void BattleScene::build_canvas() {
    // Carte entière pré-rendue une fois (tuiles officielles détourées) ;
    // chaque image n'en recopie ensuite que la partie visible.
    wsg::Game& g = *game_;
    cw_ = (g.width() - 1) * COL_STEP + HEX_W;
    ch_ = g.height() * HEX_H + HEX_H / 2;
    canvas_.assign((size_t)cw_ * ch_, gb::rgb(10, 10, 14));
    std::unordered_map<std::string, Aki> tiles;
    for (int y = 0; y < g.height(); ++y)
        for (int x = 0; x < g.width(); ++x) {
            const std::string& code = g.code({x, y});
            auto it = tiles.find(code);
            if (it == tiles.end()) {
                Aki a;
                a.load(std::string(kRoot) + "gfx/terrain/" + safe(code) + ".aki");
                it = tiles.emplace(code, std::move(a)).first;
            }
            const Aki& t = it->second;
            int cx, cy; hex_center({x, y}, cx, cy);
            int ox = cx - t.w / 2, oy = cy - t.h / 2;
            for (int j = 0; j < t.h; ++j) {
                int yy = oy + j; if (yy < 0 || yy >= ch_) continue;
                for (int i = 0; i < t.w; ++i) {
                    int xx = ox + i; if (xx < 0 || xx >= cw_) continue;
                    uint16_t c = t.px[(size_t)j * t.w + i];
                    if (c != KEY) canvas_[(size_t)yy * cw_ + xx] = c;
                }
            }
        }
}

const Aki* BattleScene::sprite(const wsg::Unit& u) {
    std::string k = safe(u.type_id) + "__" + color_name(game_->side(u.side));
    auto it = sprites_.find(k);
    if (it == sprites_.end()) {
        Aki a;
        a.load(std::string(kRoot) + "gfx/units/" + k + ".aki");
        it = sprites_.emplace(k, std::move(a)).first;
    }
    return it->second.w ? &it->second : nullptr;
}

void BattleScene::follow(HexCoord h) {
    int cx, cy; hex_center(h, cx, cy);
    const int margin = 50;
    if (cx - cam_x_ < margin) cam_x_ = cx - margin;
    if (cx - cam_x_ > gb::SCREEN_W - margin) cam_x_ = cx - gb::SCREEN_W + margin;
    if (cy - cam_y_ < margin) cam_y_ = cy - margin;
    if (cy - cam_y_ > VIEW_H - margin) cam_y_ = cy - VIEW_H + margin;
    cam_x_ = std::max(0, std::min(cam_x_, std::max(0, cw_ - gb::SCREEN_W)));
    cam_y_ = std::max(0, std::min(cam_y_, std::max(0, ch_ - VIEW_H)));
}

void BattleScene::enter() {
    if (loaded_) return;
    loaded_ = load();
    if (!loaded_) return;
    mode_ = Mode::Idle;
    sprites_.clear();
    log_ = "Tour " + std::to_string(game_->turn()) + " : à toi de jouer.";
    for (auto& u : game_->units())
        if (u.alive() && u.side == game_->current_side() && u.canrecruit) cursor_ = u.pos;
    int cx, cy; hex_center(cursor_, cx, cy);
    cam_x_ = std::max(0, std::min(cx - gb::SCREEN_W / 2, std::max(0, cw_ - gb::SCREEN_W)));
    cam_y_ = std::max(0, std::min(cy - VIEW_H / 2, std::max(0, ch_ - VIEW_H)));
    start_ai_if_needed();
}

// ---------------------------------------------------------------------------
// Entrées
// ---------------------------------------------------------------------------
void BattleScene::move_cursor(int dx, int dy) {
    HexCoord n{cursor_.x + dx, cursor_.y + dy};
    if (!game_->in_map(n)) return;
    cursor_ = n;
    follow(cursor_);
}

void BattleScene::build_recruit_list() {
    wsg::Game& g = *game_;
    wsg::Side* s = g.side(g.current_side());
    recruit_names_.clear(); recruit_cost_.clear(); recruit_roster_idx_.clear();
    if (!s) return;
    for (const auto& t : s->recruit) {
        const wsg::UnitTypeDef* td = data_.type(t);
        recruit_names_.push_back(td ? td->name : t);
        recruit_cost_.push_back(td ? td->cost : 0);
        recruit_roster_idx_.push_back(-1);
    }
    const auto& roster = wsg::campaign_state().roster;
    for (size_t i = 0; i < roster.size(); ++i) {
        const wsg::PersistentUnit& p = roster[i];
        if (!p.recallable) continue;                            // personnages scriptés exclus (rappel par id dans le WML)
        if (!p.id.empty() && g.has_live_id(p.id)) continue;    // déjà présent sur la carte
        if (std::find(recalled_roster_idx_.begin(), recalled_roster_idx_.end(), (int)i) != recalled_roster_idx_.end())
            continue;                                          // déjà rappelé ce scénario (id vide : on suit l'index nous-mêmes)
        const wsg::UnitTypeDef* td = data_.type(p.type_id);
        recruit_names_.push_back((p.name.empty() ? (td ? td->name : p.type_id) : p.name) + " (rappel)");
        recruit_cost_.push_back(wsg::Game::kRecallCost);
        recruit_roster_idx_.push_back((int)i);
    }
}

void BattleScene::refresh_reach() {
    reach_.clear();
    wsg::Unit* u = game_->unit_by_uid(sel_uid_);
    if (u && u->alive()) reach_ = game_->reach(*u);
}

void BattleScene::open_attack(int a, int d, HexCoord from) {
    wsg::Unit* u = game_->unit_by_uid(a);
    wsg::Unit* t = game_->unit_by_uid(d);
    if (!u || !t || u->t->attacks.empty()) return;
    att_uid_ = a; def_uid_ = d; att_from_ = from;
    weapons_.clear();
    for (int i = 0; i < (int)u->t->attacks.size(); ++i) weapons_.push_back(i);
    // arme proposée : celle qui inflige le plus de dégâts espérés
    double best = -1;
    weapon_ = 0;
    for (size_t i = 0; i < weapons_.size(); ++i) {
        wsg::CombatPreview p = game_->preview(*u, weapons_[i], *t, from);
        double e = t->hp - p.def.exp_hp;
        if (e > best) { best = e; weapon_ = (int)i; }
    }
    mode_ = Mode::Attack;
}

void BattleScene::on_a() {
    wsg::Game& g = *game_;
    wsg::Unit* here = g.unit_at(cursor_);
    if (mode_ == Mode::Idle || mode_ == Mode::Selected) {
        wsg::Unit* sel = mode_ == Mode::Selected ? g.unit_by_uid(sel_uid_) : nullptr;
        if (here && here->side == g.current_side() && (!sel || here->uid != sel->uid)) {
            sel_uid_ = here->uid;
            mode_ = Mode::Selected;
            refresh_reach();
            return;
        }
        if (!sel) return;
        if (here && !g.allied(here->side, sel->side)) {
            if (sel->attacked) { log_ = "Cette unité a déjà attaqué."; return; }
            if (hex_distance(sel->pos, here->pos) == 1) { open_attack(sel->uid, here->uid, sel->pos); return; }
            // se placer sur la case libre voisine de la cible la plus sûre
            HexCoord best{-1, -1};
            int bd = 1 << 30;
            for (HexCoord n : hex_neighbors(here->pos)) {
                auto it = reach_.find(g.key(n));
                if (it == reach_.end() || !g.in_map(n) || g.unit_at(n)) continue;
                int d = g.defense_of(*sel, n) * 10 + it->second.cost;
                if (d < bd) { bd = d; best = n; }
            }
            if (best.x < 0) { log_ = "Cible hors de portée."; return; }
            open_attack(sel->uid, here->uid, best);
            return;
        }
        if (!here && reach_.count(g.key(cursor_))) {
            g.move_unit(*sel, cursor_);
            refresh_reach();
            return;
        }
        if (here && here->uid == sel->uid) { mode_ = Mode::Idle; reach_.clear(); }
        return;
    }
    if (mode_ == Mode::Attack) {
        wsg::Unit* a = g.unit_by_uid(att_uid_);
        if (a && !(a->pos == att_from_)) g.move_unit(*a, att_from_);
        a = g.unit_by_uid(att_uid_);
        wsg::Unit* d = g.unit_by_uid(def_uid_);
        if (!a || !d || !(a->pos == att_from_) || hex_distance(a->pos, d->pos) != 1) {
            log_ = "Déplacement interrompu (zone de contrôle ?).";
            mode_ = Mode::Selected;
            refresh_reach();
            return;
        }
        log_ = g.attack(*a, weapons_[weapon_], *d);
        mode_ = Mode::Idle;
        reach_.clear();
        return;
    }
    if (mode_ == Mode::Menu) {
        const std::string it = menu_items_[menu_sel_];
        mode_ = Mode::Idle;
        reach_.clear();
        if (it == "Fin de tour") {
            g.end_turn_of_current_side();
            autosave();
            if (g.outcome() == wsg::Outcome::None) start_ai_if_needed();
        } else if (it == "Recruter") {
            build_recruit_list();
            mode_ = Mode::Recruit; menu_sel_ = 0;
        } else if (it == "Unité suivante") {
            next_unit();
        } else if (it == "Objectifs") {
            mode_ = Mode::Objectives;
        } else if (it.rfind("Astuces", 0) == 0) {
            wsg::campaign_state().tips_enabled = !wsg::campaign_state().tips_enabled;
        }
        return;
    }
    if (mode_ == Mode::Recruit) {
        mode_ = Mode::Idle;
        wsg::Side* s = g.side(g.current_side());
        if (!s || recruit_names_.empty() || menu_sel_ >= (int)recruit_names_.size()) return;
        int roster_idx = recruit_roster_idx_[menu_sel_];
        int cost = recruit_cost_[menu_sel_];
        for (auto& u : g.units()) {
            if (!u.alive() || u.side != s->id || !u.canrecruit) continue;
            auto hexes = g.recruit_hexes(u);
            if (hexes.empty()) continue;
            HexCoord where = hexes[0];
            for (HexCoord h : hexes) if (h == cursor_) where = h;
            if (s->gold < cost) { log_ = "Pas assez d'or."; break; }
            if (roster_idx < 0) {
                const std::string type = s->recruit[menu_sel_];
                const wsg::UnitTypeDef* td = data_.type(type);
                if (g.recruit(s->id, type, where)) {
                    log_ = "Recrue : " + (td ? td->name : type) + ".";
                    cursor_ = where; follow(cursor_);
                } else {
                    log_ = "Pas assez d'or.";
                }
            } else {
                wsg::PersistentUnit& p = wsg::campaign_state().roster[roster_idx];
                if (wsg::Unit* u2 = g.place_recall(p, where)) {
                    s->gold -= cost;
                    recalled_roster_idx_.push_back(roster_idx);
                    log_ = "Rappel : " + (p.name.empty() ? p.type_id : p.name) + ".";
                    cursor_ = u2->pos; follow(cursor_);
                }
            }
            break;
        }
        return;
    }
    if (mode_ == Mode::Objectives) mode_ = Mode::Idle;
}

void BattleScene::on_b() {
    if (mode_ == Mode::Attack) { mode_ = Mode::Selected; return; }
    mode_ = Mode::Idle;
    reach_.clear();
}

void BattleScene::next_unit(int dir) {
    wsg::Game& g = *game_;
    auto& us = g.units();
    const size_t n = us.size();
    if (n == 0) return;
    size_t start = 0;
    for (size_t i = 0; i < n; ++i) if (us[i].uid == sel_uid_) start = i;
    for (size_t k = 1; k <= n; ++k) {
        size_t idx = (start + (dir > 0 ? k : (n * k - k)) ) % n;   // avance ou recule en bouclant
        wsg::Unit& u = us[idx];
        if (u.alive() && u.side == g.current_side() && (u.mp > 0 || !u.attacked)) {
            sel_uid_ = u.uid; cursor_ = u.pos; follow(cursor_);
            mode_ = Mode::Selected; refresh_reach();
            return;
        }
    }
    log_ = "Toutes les unités ont joué.";
}

// Banc de test uniquement : WSG_AUTOPLAY=1 fait jouer l'IA pour le joueur.
static bool autoplay() {
#if defined(ESP_PLATFORM)
    return false;
#else
    static const bool a = getenv("WSG_AUTOPLAY") != nullptr;
    return a;
#endif
}

void BattleScene::start_ai_if_needed() {
    wsg::Side* s = game_->side(game_->current_side());
    if (s && (!s->human || autoplay()) && game_->outcome() == wsg::Outcome::None) { mode_ = Mode::Ai; ai_timer_ = 0; }
}

bool BattleScene::check_end() {
    // fin de partie : événement « victory »/« defeat » (dialogues de fin),
    // puis écran de résultat une fois les messages lus
    wsg::Game& g = *game_;
    if (g.outcome() == wsg::Outcome::None) return false;
    if (!end_fired_) {
        end_fired_ = true;
        if (g.outcome() == wsg::Outcome::Victory) {
            // Report vers le scénario suivant : tout le camp du joueur
            // survivant (Wesnoth le fait via son moteur de sauvegarde,
            // save_id="player_side" -- voir campaign_state.h), plus l'or
            // selon la règle du scénario ([gold_carryover]).
            wsg::CampaignState& cs = wsg::campaign_state();
            cs.roster = g.harvest_roster(1);
            cs.gold = g.compute_carryover_gold(1);
            cs.has_gold = true;
            // Sauvegarde automatique du point de reprise (fin de scénario) --
            // écrase l'autosave de bataille : la bataille qui vient de se
            // terminer n'a plus lieu d'être reprise. Rien à reprendre si
            // c'est la dernière bataille de la campagne.
            if (!next_scenario_.empty() && next_scenario_ != "null")
                save_story_point(kAutoSlot, next_scenario_, scenario_name_, cs);
        }
        events_->fire(g.outcome() == wsg::Outcome::Victory ? "victory" : "defeat", 0, 0, {-1, -1});
    }
    mode_ = Mode::Over;
    return true;
}

const std::string& BattleScene::current_text(const wsg::EventMessage& m) const {
    if (language_ == Language::French && !m.text_fr.empty()) return m.text_fr;
    return m.text;
}

void BattleScene::update(SceneManager& mgr) {
    if (!loaded_) {
        if (gb::buttons_pressed() & (gb::BTN_A | gb::BTN_B)) mgr.set(SceneId::QUIT);
        return;
    }
#if defined(ESP_PLATFORM)
    // Même lecture que StoryScene::update() -- voir story_scene.cpp -- pour
    // rester cohérente si le joueur bascule la langue système en cours de
    // bataille (menu accessible hors bataille uniquement, mais la valeur
    // peut avoir changé depuis le dernier passage par l'écran de récit).
    const char* lang = akaRuntime.getLanguage();
    language_ = (lang && lang[0] == 'f') ? Language::French : Language::English;
#endif
    wsg::Game& g = *game_;
    uint32_t p = gb::buttons_pressed(), held = gb::buttons();

    // dialogues des événements : prioritaires sur tout le reste
    if (events_->has_message()) {
        const wsg::EventMessage& m = events_->message();
        if (wsg::Unit* u = g.unit_by_uid(m.unit_uid)) { cursor_ = u->pos; follow(cursor_); }
        if (msg_audio_pending_) {
            const std::string& path = (language_ == Language::French && !m.audio_path_fr.empty())
                                           ? m.audio_path_fr
                                           : m.audio_path;
            if (!path.empty()) story_audio::play_line(std::string(kRoot) + "audio/" + path);
            msg_audio_pending_ = false;
        }
        int max_lines = dialog_max_lines();
        int tx_check = dialog_text_x(m);
        int total_lines = (int)gb::wrap_text_lines(gb::SCREEN_W - tx_check - 6, current_text(m)).size();
        bool more_pages = (dlg_page_ + 1) * max_lines < total_lines;
        if (more_pages) {
            // Texte trop long : A/B tourne la page avant de proposer les
            // choix ou de passer au message suivant.
            if (p & (gb::BTN_A | gb::BTN_B)) ++dlg_page_;
        } else if (!m.options.empty()) {
            int n = (int)m.options.size();
            if (p & gb::BTN_UP) dlg_sel_ = (dlg_sel_ + n - 1) % n;
            if (p & gb::BTN_DOWN) dlg_sel_ = (dlg_sel_ + 1) % n;
            if (p & gb::BTN_A) {
                story_audio::stop();
                events_->choose(dlg_sel_); dlg_sel_ = 0; dlg_page_ = 0; msg_audio_pending_ = true;
            }
        } else if (p & (gb::BTN_A | gb::BTN_B)) {
            story_audio::stop();
            events_->pop_message();
            dlg_page_ = 0;
            msg_audio_pending_ = true;
        }
        check_end();
        return;
    }

    if (mode_ == Mode::Over) {
        if (g.outcome() == wsg::Outcome::Victory) {
            if (p & gb::BTN_A) {
                if (!next_scenario_.empty() && next_scenario_ != "null") {
                    story_scene_resume(next_scenario_);
                    mgr.set(SceneId::STORY);
                } else {
                    mgr.set(SceneId::QUIT);
                }
            } else if (p & gb::BTN_MENU) {
                mode_ = Mode::SaveMenu;
                save_slot_sel_ = 0;
                save_feedback_.clear();
            }
        } else {
            if (p & gb::BTN_A) { loaded_ = false; sel_uid_ = 0; reach_.clear(); enter(); }   // recommencer
            else if (p & gb::BTN_B) mgr.set(SceneId::QUIT);
        }
        return;
    }
    if (mode_ == Mode::SaveMenu) {
        if (p & gb::BTN_UP) save_slot_sel_ = (save_slot_sel_ + kNumManualSlots - 1) % kNumManualSlots;
        if (p & gb::BTN_DOWN) save_slot_sel_ = (save_slot_sel_ + 1) % kNumManualSlots;
        if (p & gb::BTN_A) {
            bool ok = save_story_point(save_slot_sel_ + 1, next_scenario_, scenario_name_, wsg::campaign_state());
            save_feedback_ = ok ? "Sauvegardé." : "Échec de la sauvegarde.";
        } else if (p & gb::BTN_B) {
            mode_ = Mode::Over;
        }
        return;
    }
    if (mode_ == Mode::Ai) {
        if (++ai_timer_ < 10) return;               // une action toutes les 10 images
        ai_timer_ = 0;
        wsg::AiStep st;
        if (wsg::ai_step(g, 0.4, st)) {             // agressivité par défaut de Wesnoth
            wsg::Unit* u = g.unit_by_uid(st.unit_uid);
            if (u && u->alive()) { cursor_ = u->pos; follow(cursor_); }
            if (!st.text.empty()) log_ = st.text;
        } else {
            g.end_turn_of_current_side();
            autosave();
            wsg::Side* s = g.side(g.current_side());
            if (s && s->human && !autoplay()) {
                mode_ = Mode::Idle;
                log_ = "Tour " + std::to_string(g.turn()) + " : à toi de jouer.";
            }
        }
        check_end();
        return;
    }

    // croix avec répétition
    uint32_t dirs = held & (gb::BTN_UP | gb::BTN_DOWN | gb::BTN_LEFT | gb::BTN_RIGHT);
    if (!dirs) repeat_ = 0;
    bool fire = (p & dirs) || (dirs && ++repeat_ > 8 && repeat_ % 3 == 0);
    int dx = 0, dy = 0;
    if (fire) {
        if (held & gb::BTN_UP) dy = -1;
        if (held & gb::BTN_DOWN) dy = 1;
        if (held & gb::BTN_LEFT) dx = -1;
        if (held & gb::BTN_RIGHT) dx = 1;
    }
    bool in_list = mode_ == Mode::Menu || mode_ == Mode::Recruit || mode_ == Mode::Attack;
    if (in_list) {
        wsg::Side* s = g.side(g.current_side());
        int n = mode_ == Mode::Attack ? (int)weapons_.size()
              : mode_ == Mode::Menu ? (int)menu_items_.size() : (int)recruit_names_.size();
        int& sel = mode_ == Mode::Attack ? weapon_ : menu_sel_;
        if (n > 0 && (p & gb::BTN_UP)) sel = (sel + n - 1) % n;
        if (n > 0 && (p & gb::BTN_DOWN)) sel = (sel + 1) % n;
    } else if (mode_ != Mode::Objectives && (dx || dy)) {
        move_cursor(dx, dy);
    }

    if (p & gb::BTN_A) on_a();
    else if (p & gb::BTN_B) on_b();
    else if ((p & gb::BTN_R1) && !in_list) next_unit(1);
    else if ((p & gb::BTN_L1) && !in_list) next_unit(-1);
    else if ((p & gb::BTN_MENU) && mode_ != Mode::Menu) {
        menu_items_ = {"Fin de tour"};
        for (auto& u : g.units())
            if (u.alive() && u.side == g.current_side() && u.canrecruit && !g.recruit_hexes(u).empty()) {
                menu_items_.push_back("Recruter");
                break;
            }
        menu_items_.push_back("Unité suivante");
        menu_items_.push_back("Objectifs");
        menu_items_.push_back(wsg::campaign_state().tips_enabled ? "Astuces : Activées" : "Astuces : Désactivées");
        menu_items_.push_back("Retour");
        menu_sel_ = 0;
        mode_ = Mode::Menu;
    }
    check_end();
}

// ---------------------------------------------------------------------------
// Rendu
// ---------------------------------------------------------------------------
void BattleScene::draw_hex_outline(HexCoord h, uint16_t c) {
    int cx, cy; hex_center(h, cx, cy);
    cx -= cam_x_; cy += TOP - cam_y_;
    const int px[6] = {cx + 20, cx + 10, cx - 10, cx - 20, cx - 10, cx + 10};
    const int py[6] = {cy, cy + 21, cy + 21, cy, cy - 21, cy - 21};
    for (int i = 0; i < 6; ++i) {
        int x0 = px[i], y0 = py[i], x1 = px[(i + 1) % 6], y1 = py[(i + 1) % 6];
        int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
        while (true) {
            if (y0 >= TOP && y0 < TOP + VIEW_H) gb::pixel(x0, y0, c);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
}

void BattleScene::draw_units() {
    wsg::Game& g = *game_;
    // drapeaux de village (couleur du propriétaire)
    for (int y = 0; y < g.height(); ++y)
        for (int x = 0; x < g.width(); ++x) {
            int o = g.village_owner({x, y});
            if (o <= 0) continue;
            int cx, cy; hex_center({x, y}, cx, cy);
            int sx = cx - cam_x_ + 8, sy = cy - cam_y_ + TOP - 18;
            if (sx < 0 || sx > gb::SCREEN_W - 8 || sy < TOP || sy > TOP + VIEW_H - 9) continue;
            gb::fill_rect(sx, sy, 1, 9, WHITE);
            gb::fill_rect(sx + 1, sy, 6, 4, side_color(g.side(o)));
        }
    for (auto& u : g.units()) {
        if (!u.alive()) continue;
        int cx, cy; hex_center(u.pos, cx, cy);
        int sx = cx - cam_x_, sy = cy - cam_y_ + TOP;
        if (sx < -30 || sx > gb::SCREEN_W + 30 || sy < TOP - 20 || sy > TOP + VIEW_H + 20) continue;
        const Aki* s = sprite(u);
        int y0 = sy - 24;
        if (s) {
            int skip = std::max(0, TOP - y0);
            int hh = std::min(s->h - skip, TOP + VIEW_H - (y0 + skip));
            if (hh > 0) gb::blit565(sx - s->w / 2, y0 + skip, s->w, hh, s->px.data() + (size_t)skip * s->w, s->w, KEY);
        } else if (sy - 8 >= TOP && sy + 8 < TOP + VIEW_H) {
            gb::fill_rect(sx - 8, sy - 8, 16, 16, side_color(g.side(u.side)));
        }
        // barres de PV/XP et orbe de mouvement, à la manière de Wesnoth
        int bx = sx + 14, by = sy - 18, bh = 22;
        if (by - 6 < TOP || by + bh >= TOP + VIEW_H) continue;
        int hp_h = bh * u.hp / std::max(1, u.max_hp);
        gb::Color hc = u.hp * 3 > u.max_hp * 2 ? GREEN : u.hp * 3 > u.max_hp ? YELLOW : RED;
        gb::fill_rect(bx, by, 3, bh, gb::rgb(30, 30, 30));
        gb::fill_rect(bx, by + bh - hp_h, 3, hp_h, hc);
        int xp_h = bh * std::min(u.xp, u.max_xp) / std::max(1, u.max_xp);
        gb::fill_rect(bx + 4, by, 2, bh, gb::rgb(30, 30, 30));
        gb::fill_rect(bx + 4, by + bh - xp_h, 2, xp_h, BLUE);
        gb::Color orb;
        if (u.side == g.current_side() && g.side(u.side) && g.side(u.side)->human)
            orb = (u.mp == u.max_mp && !u.attacked) ? GREEN : (u.mp > 0 || !u.attacked) ? YELLOW : RED;
        else
            orb = g.allied(u.side, 1) ? BLUE : RED;
        gb::fill_rect(bx - 1, by - 6, 4, 4, orb);
        if (u.canrecruit) gb::fill_rect(sx - 4, by - 6, 8, 3, YELLOW);   // chef
        if (u.poisoned) gb::fill_rect(sx - 16, by, 3, 3, GREEN);
        if (u.slowed) gb::fill_rect(sx - 16, by + 4, 3, 3, GREY);
    }
}

void BattleScene::draw_hud() {
    wsg::Game& g = *game_;
    gb::fill_rect(0, 0, gb::SCREEN_W, TOP, PANEL);
    // pastille du moment de la journée : jour / crépuscule-aube / nuit
    int lb = g.lawful_bonus_at({-1, -1});
    gb::fill_rect(2, 2, 8, 8, lb > 0 ? gb::rgb(250, 230, 90) : lb < 0 ? gb::rgb(60, 70, 170) : gb::rgb(235, 130, 70));
    char buf[96];
    if (g.turn_limit() > 0) snprintf(buf, sizeof buf, "T%d/%d %s", g.turn(), g.turn_limit(), g.tod_name());
    else snprintf(buf, sizeof buf, "T%d %s", g.turn(), g.tod_name());
    gb::text(13, 2, buf, WHITE);
    if (wsg::Side* me = g.side(1)) {
        snprintf(buf, sizeof buf, "Or %d (%+d)", me->gold, g.income(1));
        gb::text(gb::SCREEN_W - gb::text_width(buf) - 3, 2, buf, YELLOW);
    }
}

void BattleScene::draw_panel() {
    wsg::Game& g = *game_;
    int y = gb::SCREEN_H - BOTTOM;
    gb::fill_rect(0, y, gb::SCREEN_W, BOTTOM, PANEL);
    char buf[128];
    // ligne 1 : unité sous le curseur (ou sélectionnée) + terrain et défense
    wsg::Unit* u = g.unit_at(cursor_);
    if (!u) {
        // Case vide : affiche quand même le % de défense, mais celui d'une
        // unité de référence -- la défense dépend du movetype, donc sans
        // unité on ne peut rien afficher d'utile. On garde celle qui vient
        // d'être sélectionnée/déplacée (sel_uid_ n'est remis à zéro qu'au
        // « recommencer ») même après le retour en Mode::Idle qui suit un
        // déplacement/une attaque/une action de menu -- sinon le % de
        // défense disparaissait dès qu'on quittait le mode Selected, ce qui
        // rendait impossible de repérer une case à l'avance entre deux
        // actions.
        wsg::Unit* ref = g.unit_by_uid(sel_uid_);
        if (ref && ref->alive()) u = ref;
    }
    const std::string& tname = g.terrain(cursor_).name;
    if (u) {
        snprintf(buf, sizeof buf, "%s PV%d/%d XP%d/%d PM%d | %s %d%%", uname(*u).c_str(), u->hp, u->max_hp,
                 u->xp, u->max_xp, u->mp, tname.c_str(), 100 - g.defense_of(*u, cursor_));
        gb::text(3, y + 2, buf, g.allied(u->side, 1) ? WHITE : gb::rgb(255, 160, 160));
    } else {
        gb::text(3, y + 2, tname.c_str(), GREY);
    }
    // ligne 2 : journal
    gb::text(3, y + 14, log_.substr(0, 39).c_str(), YELLOW);
}

void BattleScene::draw_menu(const std::vector<std::string>& items, int sel, const char* title) {
    int w = 220, h = 18 + (int)items.size() * 12, x = (gb::SCREEN_W - w) / 2, y = 40;
    gb::fill_rect(x, y, w, h, PANEL);
    gb::fill_rect(x, y, w, 1, YELLOW);
    gb::text(x + 6, y + 4, title, YELLOW);
    for (size_t i = 0; i < items.size(); ++i) {
        if ((int)i == sel) gb::fill_rect(x + 2, y + 14 + (int)i * 12, w - 4, 11, gb::rgb(60, 60, 90));
        gb::text(x + 8, y + 16 + (int)i * 12, items[i].c_str(), WHITE);
    }
}

void BattleScene::draw_attack() {
    wsg::Game& g = *game_;
    wsg::Unit* a = g.unit_by_uid(att_uid_);
    wsg::Unit* d = g.unit_by_uid(def_uid_);
    if (!a || !d) return;
    int x = 4, y = 20, w = gb::SCREEN_W - 8, h = 30 + (int)weapons_.size() * 24;
    gb::fill_rect(x, y, w, h, PANEL);
    gb::fill_rect(x, y, w, 1, RED);
    char buf[96];
    snprintf(buf, sizeof buf, "%s -> %s", uname(*a).c_str(), uname(*d).c_str());
    gb::text(x + 4, y + 4, buf, YELLOW);
    for (size_t i = 0; i < weapons_.size(); ++i) {
        wsg::CombatPreview p = g.preview(*a, weapons_[i], *d, att_from_);
        const wsg::AttackDef& aw = a->t->attacks[weapons_[i]];
        int yy = y + 17 + (int)i * 24;
        if ((int)i == weapon_) gb::fill_rect(x + 2, yy - 2, w - 4, 22, gb::rgb(70, 45, 45));
        snprintf(buf, sizeof buf, "%s %d-%d %d%%", aw.name.c_str(), p.att.damage, p.att.strikes, p.att.cth);
        gb::text(x + 6, yy, buf, WHITE);
        if (p.def.weapon >= 0)
            snprintf(buf, sizeof buf, "%s %d-%d %d%%", d->t->attacks[p.def.weapon].name.c_str(), p.def.damage,
                     p.def.strikes, p.def.cth);
        else
            snprintf(buf, sizeof buf, "sans riposte");
        gb::text(x + 162, yy, buf, gb::rgb(255, 170, 170));
        snprintf(buf, sizeof buf, "tue %d%%  meurt %d%%  PV %.0f / %.0f", (int)(p.def.p_dead * 100 + 0.5),
                 (int)(p.att.p_dead * 100 + 0.5), p.att.exp_hp, p.def.exp_hp);
        gb::text(x + 6, yy + 10, buf, GREY);
    }
    gb::text(x + 4, y + h - 10, "A attaquer  B annuler", GREY);
}

void BattleScene::render() {
    gb::clear(gb::rgb(0, 0, 0));
    if (!loaded_) {
        gb::text(10, 100, "Données de bataille introuvables", RED);
        return;
    }
    wsg::Game& g = *game_;
    int vw = std::min(gb::SCREEN_W, cw_ - cam_x_), vh = std::min(VIEW_H, ch_ - cam_y_);
    if (vw > 0 && vh > 0) gb::blit565(0, TOP, vw, vh, canvas_.data() + (size_t)cam_y_ * cw_ + cam_x_, cw_, 0);

    wsg::Unit* sel = g.unit_by_uid(sel_uid_);
    if ((mode_ == Mode::Selected || mode_ == Mode::Attack) && sel && sel->alive()) {
        for (auto& kv : reach_) {
            HexCoord h = g.unkey(kv.first);
            if (!g.unit_at(h)) draw_hex_outline(h, gb::rgb(250, 240, 150));
        }
        if (!sel->attacked)
            for (auto& e : g.units()) {
                if (!e.alive() || g.allied(e.side, sel->side)) continue;
                bool can = hex_distance(e.pos, sel->pos) == 1;
                for (HexCoord n : hex_neighbors(e.pos)) if (reach_.count(g.key(n)) && !g.unit_at(n)) can = true;
                if (can) draw_hex_outline(e.pos, RED);
            }
    }
    draw_units();
    draw_hex_outline(cursor_, WHITE);
    draw_hud();
    draw_panel();

    if (mode_ == Mode::Menu) draw_menu(menu_items_, menu_sel_, "Menu");
    if (mode_ == Mode::Recruit) {
        wsg::Side* s = g.side(g.current_side());
        std::vector<std::string> items;
        for (size_t i = 0; i < recruit_names_.size(); ++i)
            items.push_back(recruit_names_[i] + "  " + std::to_string(recruit_cost_[i]) + " or");
        if (items.empty()) items.push_back("(rien de disponible)");
        draw_menu(items, menu_sel_, ("Recruter/Rappeler (or " + std::to_string(s ? s->gold : 0) + ")").c_str());
    }
    if (mode_ == Mode::Objectives) draw_menu(g.objectives(), -1, "Objectifs");
    if (mode_ == Mode::Attack) draw_attack();
    if (mode_ == Mode::Ai) gb::text(gb::SCREEN_W - 40, TOP + 3, "IA...", YELLOW);
    if (events_ && events_->has_message()) { draw_dialog(); return; }
    if (mode_ == Mode::Over || mode_ == Mode::SaveMenu) {
        bool win = g.outcome() == wsg::Outcome::Victory;
        gb::fill_rect(40, 84, 240, 64, PANEL);
        gb::text(60, 92, win ? "VICTOIRE" : "DÉFAITE", win ? GREEN : RED);
        gb::text_wrapped(60, 106, 210, 10, g.outcome_reason(), WHITE);
        if (mode_ == Mode::Over) {
            gb::text(60, 134, win ? "A : continuer  MENU : sauvegarder" : "A : recommencer  B : quitter", GREY);
        }
    }
    if (mode_ == Mode::SaveMenu) draw_save_menu();
}

void BattleScene::draw_save_menu() {
    const int x = 30, y = 40, w = 260, h = 160;
    gb::fill_rect(x, y, w, h, PANEL);
    gb::text(x + 8, y + 6, "Sauvegarder la progression", YELLOW);
    auto slots = list_manual_slots();
    for (int i = 0; i < (int)slots.size(); ++i) {
        int ry = y + 22 + i * 20;
        if (i == save_slot_sel_) gb::fill_rect(x + 4, ry - 2, w - 8, 18, gb::rgb(60, 60, 90));
        std::string label = "Emplacement " + std::to_string(i + 1) + " : ";
        label += slots[i].used ? (slots[i].scenario_name + (slots[i].turn > 0 ? " (tour " + std::to_string(slots[i].turn) + ")" : ""))
                                : "(vide)";
        gb::text(x + 10, ry, label.c_str(), WHITE);
    }
    gb::text(x + 8, y + h - 16, save_feedback_.empty() ? "A : sauvegarder ici   B : retour" : save_feedback_.c_str(), GREY);
}

// Lignes de texte qui tiennent dans la boîte (h=104) sans déborder de
// l'écran : 18px avant le texte (nom du personnage), ~6px de marge basse,
// interligne 10px. Fixe (ne dépend pas du portrait, qui ne change que tx).
int BattleScene::dialog_max_lines() const {
    const int h = 104;
    return std::max(1, (h - 18 - 6) / 10);
}

int BattleScene::dialog_text_x(const wsg::EventMessage& m) const {
    return (!m.portrait.empty() && gb::file_exists(m.portrait.c_str())) ? 106 : 6;
}

void BattleScene::draw_dialog() {
    // boîte de dialogue à la manière de Wesnoth : portrait, nom, texte
    const wsg::EventMessage& m = events_->message();
    const int h = 104, y = gb::SCREEN_H - h;
    gb::fill_rect(0, y, gb::SCREEN_W, h, PANEL);
    gb::fill_rect(0, y, gb::SCREEN_W, 1, YELLOW);
    int tx = 6;
    if (!m.portrait.empty() && gb::draw_image(m.portrait.c_str(), 4, y + 4)) tx = 106;
    if (!m.speaker.empty()) gb::text(tx, y + 5, m.speaker.c_str(), YELLOW);

    // Texte trop long pour la boîte : on ne dessine que la page courante
    // (dlg_page_) plutôt que de laisser déborder hors de l'écran (bug
    // signalé : dernière ligne coupée). Un petit indicateur "▼" annonce la
    // suite quand il en reste.
    auto all_lines = gb::wrap_text_lines(gb::SCREEN_W - tx - 6, current_text(m));
    int max_lines = dialog_max_lines();
    int shown = gb::draw_text_lines(tx, y + 18, 10, all_lines, WHITE, dlg_page_ * max_lines, max_lines);
    bool more = (dlg_page_ + 1) * max_lines < (int)all_lines.size();
    if (more) {
        gb::text(gb::SCREEN_W - 16, y + h - 12, "v", YELLOW);
    }

    if (!more) {
        for (size_t i = 0; i < m.options.size(); ++i) {
            int oy = y + 18 + shown * 10 + 4 + (int)i * 11;
            if ((int)i == dlg_sel_) gb::fill_rect(tx - 2, oy - 1, gb::SCREEN_W - tx - 4, 10, gb::rgb(60, 60, 90));
            gb::text(tx, oy, m.options[i].c_str(), WHITE);
        }
    }
}

}  // namespace wesnoth_sg
