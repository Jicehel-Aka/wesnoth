// story/savegame.cpp — voir savegame.h
#include "story/savegame.h"

#include <cstdio>
#include <fstream>
#include <sstream>

#include "battle/events.h"
#include "battle/game.h"
#include "cJSON.h"
#include "platform/gb_port.h"
#include "scene/campaign_root.h"

namespace wesnoth_sg {
namespace {

std::string path_for(int slot) {
    // sd_root() (pas un chemin fixe "WESNOTH_SG") : chaque campagne a ses
    // propres emplacements de sauvegarde, cf. scene/campaign_root.h.
    std::string name = (slot == kAutoSlot) ? "save_auto.json" : ("save_slot" + std::to_string(slot) + ".json");
    return sd_root() + name;
}

std::string slurp(const std::string& logical_path) {
    std::ifstream f(gb::sd_path(logical_path.c_str()), std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

bool write_all(const std::string& logical_path, const std::string& data) {
    FILE* f = fopen(gb::sd_path(logical_path.c_str()).c_str(), "wb");
    if (!f) return false;
    size_t n = fwrite(data.data(), 1, data.size(), f);
    fclose(f);
    return n == data.size();
}

std::string jstr(const cJSON* o, const char* k, const char* d = "") {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
    return (v && cJSON_IsString(v) && v->valuestring) ? v->valuestring : d;
}
int jint(const cJSON* o, const char* k, int d = 0) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
    return (v && cJSON_IsNumber(v)) ? v->valueint : d;
}
bool jbool(const cJSON* o, const char* k, bool d = false) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
    return v ? cJSON_IsTrue(v) : d;
}

void write_roster(cJSON* out, const std::vector<wsg::PersistentUnit>& roster) {
    cJSON* arr = cJSON_AddArrayToObject(out, "roster");
    for (const auto& p : roster) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "type_id", p.type_id.c_str());
        cJSON_AddStringToObject(o, "id", p.id.c_str());
        cJSON_AddStringToObject(o, "name", p.name.c_str());
        cJSON_AddStringToObject(o, "role", p.role.c_str());
        cJSON* tr = cJSON_AddArrayToObject(o, "traits");
        for (const auto& t : p.traits) cJSON_AddItemToArray(tr, cJSON_CreateString(t.c_str()));
        cJSON_AddNumberToObject(o, "side", p.side);
        cJSON_AddNumberToObject(o, "hp", p.hp);
        cJSON_AddNumberToObject(o, "xp", p.xp);
        cJSON_AddBoolToObject(o, "canrecruit", p.canrecruit);
        cJSON_AddBoolToObject(o, "recallable", p.recallable);
        cJSON_AddItemToArray(arr, o);
    }
}

std::vector<wsg::PersistentUnit> read_roster(const cJSON* in) {
    std::vector<wsg::PersistentUnit> out;
    const cJSON* e = nullptr;
    cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(in, "roster")) {
        wsg::PersistentUnit p;
        p.type_id = jstr(e, "type_id"); p.id = jstr(e, "id"); p.name = jstr(e, "name"); p.role = jstr(e, "role");
        const cJSON* tr = nullptr;
        cJSON_ArrayForEach(tr, cJSON_GetObjectItemCaseSensitive(e, "traits"))
            if (cJSON_IsString(tr)) p.traits.push_back(tr->valuestring);
        p.side = jint(e, "side", 1);
        p.hp = jint(e, "hp", 1);
        p.xp = jint(e, "xp", 0);
        p.canrecruit = jbool(e, "canrecruit");
        p.recallable = jbool(e, "recallable");
        out.push_back(std::move(p));
    }
    return out;
}

}  // namespace

bool save_battle(int slot, const std::string& scenario_id, const std::string& scenario_name,
                  const wsg::Game& g, const wsg::EventEngine& ev) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "kind", "battle");
    cJSON_AddStringToObject(root, "scenario_id", scenario_id.c_str());
    cJSON_AddStringToObject(root, "scenario_name", scenario_name.c_str());
    cJSON_AddNumberToObject(root, "turn", g.turn());
    cJSON* game_obj = cJSON_AddObjectToObject(root, "game");
    g.save_state(game_obj);
    cJSON* events_obj = cJSON_AddObjectToObject(root, "events");
    ev.save_state(events_obj);
    char* text = cJSON_PrintUnformatted(root);
    bool ok = text && write_all(path_for(slot), text);
    if (text) cJSON_free(text);
    cJSON_Delete(root);
    return ok;
}

BattleSaveRaw load_battle_raw(int slot) {
    BattleSaveRaw out;
    std::string raw = slurp(path_for(slot));
    if (raw.empty()) return out;
    cJSON* root = cJSON_Parse(raw.c_str());
    if (!root) return out;
    if (jstr(root, "kind") != "battle") { cJSON_Delete(root); return out; }
    out.scenario_id = jstr(root, "scenario_id");
    cJSON* game_obj = cJSON_GetObjectItemCaseSensitive(root, "game");
    cJSON* events_obj = cJSON_GetObjectItemCaseSensitive(root, "events");
    if (game_obj) { char* t = cJSON_PrintUnformatted(game_obj); out.game_json = t; cJSON_free(t); }
    if (events_obj) { char* t = cJSON_PrintUnformatted(events_obj); out.events_json = t; cJSON_free(t); }
    cJSON_Delete(root);
    return out;
}

bool save_story_point(int slot, const std::string& next_scenario_id, const std::string& scenario_name,
                       const wsg::CampaignState& cs) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "kind", "story");
    cJSON_AddStringToObject(root, "next_scenario_id", next_scenario_id.c_str());
    cJSON_AddStringToObject(root, "scenario_name", scenario_name.c_str());
    cJSON* cso = cJSON_AddObjectToObject(root, "campaign_state");
    cJSON_AddBoolToObject(cso, "has_gold", cs.has_gold);
    cJSON_AddNumberToObject(cso, "gold", cs.gold);
    cJSON_AddBoolToObject(cso, "tips_enabled", cs.tips_enabled);
    write_roster(cso, cs.roster);
    char* text = cJSON_PrintUnformatted(root);
    bool ok = text && write_all(path_for(slot), text);
    if (text) cJSON_free(text);
    cJSON_Delete(root);
    return ok;
}

bool load_story_point(int slot, std::string* next_scenario_id, wsg::CampaignState* cs) {
    std::string raw = slurp(path_for(slot));
    if (raw.empty()) return false;
    cJSON* root = cJSON_Parse(raw.c_str());
    if (!root) return false;
    if (jstr(root, "kind") != "story") { cJSON_Delete(root); return false; }
    if (next_scenario_id) *next_scenario_id = jstr(root, "next_scenario_id");
    const cJSON* cso = cJSON_GetObjectItemCaseSensitive(root, "campaign_state");
    if (cs && cso) {
        cs->has_gold = jbool(cso, "has_gold");
        cs->gold = jint(cso, "gold");
        cs->tips_enabled = jbool(cso, "tips_enabled", true);
        cs->roster = read_roster(cso);
    }
    cJSON_Delete(root);
    return true;
}

SaveSlotInfo slot_info(int slot) {
    SaveSlotInfo info;
    std::string raw = slurp(path_for(slot));
    if (raw.empty()) return info;
    cJSON* root = cJSON_Parse(raw.c_str());
    if (!root) return info;
    info.used = true;
    info.kind = jstr(root, "kind");
    info.scenario_name = jstr(root, "scenario_name", jstr(root, "scenario_id").c_str());
    info.turn = jint(root, "turn", 0);
    cJSON_Delete(root);
    return info;
}

std::vector<SaveSlotInfo> list_manual_slots() {
    std::vector<SaveSlotInfo> out;
    for (int i = 1; i <= kNumManualSlots; ++i) out.push_back(slot_info(i));
    return out;
}

void delete_save(int slot) {
    // Pas de suppression de fichier exposée par gb:: (pas nécessaire ailleurs
    // dans le projet) -- un fichier vide est traité comme "absent" par
    // slurp()+cJSON_Parse ci-dessus, donc l'écraser suffit.
    write_all(path_for(slot), "");
}

}  // namespace wesnoth_sg
