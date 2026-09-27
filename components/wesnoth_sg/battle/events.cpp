// battle/events.cpp — voir events.h
#include "battle/events.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "battle/campaign_state.h"
#include "cJSON.h"
#include "platform/gb_port.h"
#include "text_markup.h"

namespace wsg {

// ---------------------------------------------------------------------------
// WNode
// ---------------------------------------------------------------------------
const std::string* WNode::find(const std::string& k) const {
    for (const auto& a : attrs) if (a.first == k) return &a.second;
    return nullptr;
}
std::string WNode::get(const std::string& k, const std::string& d) const {
    const std::string* v = find(k);
    return v ? *v : d;
}
const WNode* WNode::child(const std::string& t) const {
    for (const auto& c : kids) if (c.tag == t) return &c;
    return nullptr;
}
WNode WNode::from_json(const cJSON* j) {
    WNode n;
    const cJSON* t = cJSON_GetObjectItemCaseSensitive(j, "t");
    if (t && cJSON_IsString(t)) n.tag = t->valuestring;
    const cJSON* e = nullptr;
    cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(j, "a"))
        if (cJSON_IsString(e)) n.attrs.emplace_back(e->string, e->valuestring);
    cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(j, "c")) n.kids.push_back(from_json(e));
    return n;
}

cJSON* WNode::to_json() const {
    cJSON* o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "t", tag.c_str());
    cJSON* a = cJSON_AddObjectToObject(o, "a");
    for (const auto& kv : attrs) cJSON_AddStringToObject(a, kv.first.c_str(), kv.second.c_str());
    cJSON* c = cJSON_AddArrayToObject(o, "c");
    for (const auto& k : kids) cJSON_AddItemToArray(c, k.to_json());
    return o;
}

namespace {

std::string norm(std::string s) {            // « attack_end » == « attack end »
    std::string o;
    for (char c : s) {
        c = (char)std::tolower((unsigned char)(c == '_' ? ' ' : c));
        if (c == ' ' && (o.empty() || o.back() == ' ')) continue;
        o += c;
    }
    while (!o.empty() && o.back() == ' ') o.pop_back();
    return o;
}
std::vector<std::string> split(const std::string& s, char sep = ',') {
    std::vector<std::string> r;
    std::stringstream ss(s);
    std::string x;
    while (std::getline(ss, x, sep)) {
        size_t a = x.find_first_not_of(" \t"), b = x.find_last_not_of(" \t");
        if (a != std::string::npos) r.push_back(x.substr(a, b - a + 1));
    }
    return r;
}
// « 3 », « 2-5 », « 1,4,7-9 »
bool in_ranges(const std::string& spec, int v) {
    for (const auto& p : split(spec)) {
        size_t d = p.find('-', 1);
        if (d == std::string::npos) { if (atoi(p.c_str()) == v) return true; }
        else if (v >= atoi(p.substr(0, d).c_str()) && v <= atoi(p.substr(d + 1).c_str())) return true;
    }
    return false;
}
std::string lower_slug(std::string s) {
    for (auto& c : s) c = (char)(c == ' ' ? '-' : std::tolower((unsigned char)c));
    return s;
}
std::string safe(std::string n) {
    for (auto& c : n) { if (c == '^') c = '-'; else if (c == ' ') c = '_'; else if (c == ':') c = '+'; }
    return n;
}
const char* kPortraits = "/sdcard/WESNOTH_SG/images/portraits/";

// Même convention que slugify() dans tools/wesnoth_data/generate_audio.py :
// ne garde que les lettres/chiffres ASCII (nos noms de personnages n'ont pas
// d'accents), "unknown" si le résultat est vide.
std::string slug_for_audio(const std::string& name) {
    std::string out;
    for (char c : name) if (std::isalnum((unsigned char)c)) out += c;
    return out.empty() ? "unknown" : out;
}

}  // namespace

// ---------------------------------------------------------------------------
void EventEngine::load(const std::string& scenario_json, bool queue_intro_messages) {
    cJSON* j = cJSON_Parse(scenario_json.c_str());
    if (!j) return;
    const cJSON* e = nullptr;
    cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(j, "events")) add_event(WNode::from_json(e));
    // messages de la mise en place (événement « start »), joués en ouverture
    // -- pas rejoués lors de la reprise d'une sauvegarde (déjà vus une fois).
    if (queue_intro_messages) {
        const cJSON* id_j = cJSON_GetObjectItemCaseSensitive(j, "id");
        std::string scenario_id = (id_j && cJSON_IsString(id_j)) ? id_j->valuestring : "";
        int idx = 0;
        cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(j, "intro_messages")) {
            WNode m;
            m.tag = "message";
            const cJSON* a = nullptr;
            cJSON_ArrayForEach(a, e) if (cJSON_IsString(a)) m.attrs.emplace_back(a->string, a->valuestring);
            size_t before = queue_.size();
            queue_message(m, Ctx{});
            // Doublage : chemin déterministe, calculé de la même façon que
            // tools/wesnoth_data/generate_audio.py --scenario-intro (voir ce
            // script) -- pas besoin de manifest.json pour ces messages fixes,
            // juste de vérifier au dessin si le fichier existe (cf.
            // battle_scene.cpp::draw_dialog).
            if (queue_.size() > before && !scenario_id.empty()) {
                const std::string* sp = m.find("speaker");
                std::string slug = slug_for_audio(sp && !sp->empty() ? *sp : "narrator");
                char idx_buf[16];
                std::snprintf(idx_buf, sizeof idx_buf, "%03d", idx);
                queue_.back().audio_path = scenario_id + "/intro_" + idx_buf + "_" + slug + ".wav";
                queue_.back().audio_path_fr = scenario_id + "_fr/intro_" + idx_buf + "_" + slug + ".wav";
            }
            ++idx;
        }
    }
    cJSON_Delete(j);
}

void EventEngine::attach() {
    g_.set_hook([this](const std::string& n, int a, int b, HexCoord w) { fire(n, a, b, w); });
}

void EventEngine::save_state(cJSON* out) const {
    cJSON_AddNumberToObject(out, "fired", fired_);
    cJSON* evs = cJSON_AddArrayToObject(out, "events");
    for (const auto& ev : events_) {
        cJSON* eo = cJSON_CreateObject();
        cJSON_AddBoolToObject(eo, "dead", ev.dead);
        cJSON_AddItemToObject(eo, "node", ev.node.to_json());
        cJSON_AddItemToArray(evs, eo);
    }
    cJSON* vars = cJSON_AddObjectToObject(out, "vars");
    for (const auto& kv : vars_) cJSON_AddStringToObject(vars, kv.first.c_str(), kv.second.c_str());
}

void EventEngine::load_state(const cJSON* in) {
    // Reconstruit events_ dans le même ordre qu'à la sauvegarde, en repassant
    // par add_event() (recalcule names_/once_/l'effet « même id » exactement
    // comme au chargement normal) puis en réappliquant le fanion dead --
    // couvre aussi bien les événements du scénario que ceux ajoutés
    // dynamiquement en jeu via [event] (cf. add_event() appelé depuis exec()).
    events_.clear();
    const cJSON* f = cJSON_GetObjectItemCaseSensitive(in, "fired");
    fired_ = (f && cJSON_IsNumber(f)) ? f->valueint : 0;
    const cJSON* evs = cJSON_GetObjectItemCaseSensitive(in, "events");
    const cJSON* e = nullptr;
    cJSON_ArrayForEach(e, evs) {
        const cJSON* node = cJSON_GetObjectItemCaseSensitive(e, "node");
        add_event(WNode::from_json(node));
        const cJSON* d = cJSON_GetObjectItemCaseSensitive(e, "dead");
        events_.back().dead = d && cJSON_IsTrue(d);
    }
    vars_.clear();
    const cJSON* vars = cJSON_GetObjectItemCaseSensitive(in, "vars");
    cJSON* v = nullptr;
    cJSON_ArrayForEach(v, vars) if (cJSON_IsString(v)) vars_[v->string] = v->valuestring;
}

void EventEngine::add_event(const WNode& n) {
    Ev ev;
    ev.node = n;
    for (const auto& s : split(n.get("name"))) ev.names.push_back(norm(s));
    ev.id = n.get("id");
    ev.once = n.get("first_time_only", "yes") != "no";
    if (!ev.id.empty())                       // même id : remplace l'ancien
        for (auto& o : events_) if (o.id == ev.id) o.dead = true;
    events_.push_back(std::move(ev));
}

void EventEngine::fire(const std::string& raw, int uid1, int uid2, HexCoord where) {
    const std::string name = norm(raw);
    Ctx c{uid1, uid2, where};
    Unit* u1 = uid1 ? g_.unit_by_uid(uid1) : nullptr;
    Unit* u2 = uid2 ? g_.unit_by_uid(uid2) : nullptr;
    if (u1 && where.x < 0) c.loc = u1->pos;
    // on repère d'abord les événements concernés (la liste peut grandir)
    std::vector<size_t> hits;
    for (size_t i = 0; i < events_.size(); ++i) {
        Ev& ev = events_[i];
        if (ev.dead || std::find(ev.names.begin(), ev.names.end(), name) == ev.names.end()) continue;
        const WNode* f = ev.node.child("filter");
        if (f && (!u1 || !match_unit(*f, *u1, c))) continue;
        const WNode* f2 = ev.node.child("filter_second");
        if (f2 && (!u2 || !match_unit(*f2, *u2, c))) continue;
        const WNode* fc = ev.node.child("filter_condition");
        if (fc && !cond(*fc, c)) continue;
        hits.push_back(i);
    }
    // empilés à l'envers pour s'exécuter dans l'ordre de déclaration
    for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
        Ev& ev = events_[*it];
        if (ev.once) ev.dead = true;
        ++fired_;
        push(ev.node.kids, c);
    }
    run();
}

void EventEngine::run() {
    if (running_) return;                    // la boucle en cours s'en charge
    running_ = true;
    while (!stack_.empty() && !waiting_) {
        Frame& f = stack_.back();
        if (f.i >= f.list->size()) { stack_.pop_back(); continue; }
        const WNode& n = (*f.list)[f.i++];
        Ctx c = f.ctx;
        exec(n, c);
    }
    running_ = false;
}

void EventEngine::pop_message() {
    if (queue_.empty()) return;
    if (!queue_.front().options.empty()) return;   // il faut choisir
    queue_.erase(queue_.begin());
}

void EventEngine::choose(int opt) {
    if (queue_.empty() || queue_.front().options.empty()) return;
    queue_.erase(queue_.begin());
    if (opt >= 0 && opt < (int)pending_options_.size()) {
        const WNode* o = pending_options_[opt];
        owned_.emplace_back();
        for (const auto& k : o->kids)
            if (k.tag == "command") for (const auto& a : k.kids) owned_.back().push_back(a);
        push(owned_.back(), pending_ctx_);
    }
    pending_options_.clear();
    waiting_ = false;
    run();
}

// ---------------------------------------------------------------------------
// Variables et substitutions ($unit.name, $x1, $variable...)
// ---------------------------------------------------------------------------
std::string EventEngine::subst(const std::string& s, const Ctx& c) {
    if (s.find('$') == std::string::npos) return s;
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '$') { o += s[i]; continue; }
        size_t j = i + 1;
        while (j < s.size() && (std::isalnum((unsigned char)s[j]) || s[j] == '_' || s[j] == '.')) ++j;
        std::string var = s.substr(i + 1, j - i - 1);
        if (j < s.size() && s[j] == '|') ++j;
        while (!var.empty() && var.back() == '.') { var.pop_back(); --j; }
        Unit* u1 = c.u1 ? g_.unit_by_uid(c.u1) : nullptr;
        Unit* u2 = c.u2 ? g_.unit_by_uid(c.u2) : nullptr;
        std::string val;
        if (var == "unit.name" && u1) val = u1->name.empty() ? u1->t->name : u1->name;
        else if (var == "second_unit.name" && u2) val = u2->name.empty() ? u2->t->name : u2->name;
        else if (var == "unit.id" && u1) val = u1->id;
        else if (var == "x1") val = std::to_string(c.loc.x);
        else if (var == "y1") val = std::to_string(c.loc.y);
        else if (var == "turn_number") val = std::to_string(g_.turn());
        else if (vars_.count(var)) val = vars_[var];
        o += val;
        i = j - 1;
    }
    return o;
}

// ---------------------------------------------------------------------------
// Filtres et conditions
// ---------------------------------------------------------------------------
bool EventEngine::match_unit(const WNode& f, const Unit& u, const Ctx& c) {
    if (!u.alive()) return false;
    for (const auto& a : f.attrs) {
        const std::string k = a.first, v = subst(a.second, c);
        if (k == "id") { auto ids = split(v); if (std::find(ids.begin(), ids.end(), u.id) == ids.end()) return false; }
        else if (k == "side") { if (!in_ranges(v, u.side)) return false; }
        else if (k == "type") { auto ts = split(v); if (std::find(ts.begin(), ts.end(), u.type_id.substr(0, u.type_id.find(':'))) == ts.end()) return false; }
        else if (k == "race") { auto rs = split(v); if (std::find(rs.begin(), rs.end(), u.t->race) == rs.end()) return false; }
        else if (k == "role") { if (u.role != v) return false; }
        else if (k == "canrecruit") { if ((v == "yes") != u.canrecruit) return false; }
        else if (k == "level") { if (!in_ranges(v, u.level())) return false; }
        else if (k == "x") { if (!in_ranges(v, u.pos.x)) return false; }
        else if (k == "y") { if (!in_ranges(v, u.pos.y)) return false; }
        else if (k == "count" || k == "search_recall_list" || k == "animate") { }
        else if (k == "formula") return false;          // WFL non pris en charge
        else { unsupported_["filter:" + k]++; return false; }   // clé inconnue : prudence
    }
    for (const auto& ch : f.kids) {
        if (ch.tag == "not") { if (match_unit(ch, u, c)) return false; }
        else if (ch.tag == "and") { if (!match_unit(ch, u, c)) return false; }
        else if (ch.tag == "or") { }
        else if (ch.tag == "filter_adjacent") {
            bool any = false;
            for (auto& o : g_.units())
                if (o.alive() && o.uid != u.uid && hex_distance(o.pos, u.pos) == 1 && match_unit(ch, o, c)) { any = true; break; }
            if (!any) return false;
        } else if (ch.tag == "filter_location") {
            HexCoord p{atoi(ch.get("x", "-99").c_str()), atoi(ch.get("y", "-99").c_str())};
            int r = atoi(ch.get("radius", "0").c_str());
            if (ch.find("x") && hex_distance(p, u.pos) > r) return false;
        } else if (ch.tag == "filter_side" || ch.tag == "filter_vision" || ch.tag == "filter_wml") {
            unsupported_["filter:" + ch.tag]++;
        }
    }
    // [or] : si l'une des branches [or] correspond, la condition est vraie
    for (const auto& ch : f.kids) if (ch.tag == "or" && match_unit(ch, u, c)) return true;
    return true;
}

std::vector<Unit*> EventEngine::find_units(const WNode& f, const Ctx& c) {
    std::vector<Unit*> r;
    for (auto& u : g_.units()) if (u.alive() && match_unit(f, u, c)) r.push_back(&u);
    return r;
}

bool EventEngine::cond(const WNode& n, const Ctx& c) {
    bool ok = true;
    for (const auto& ch : n.kids) {
        if (ch.tag == "then" || ch.tag == "else" || ch.tag == "elseif" || ch.tag == "or") continue;
        bool r = true;
        if (ch.tag == "variable") {
            std::string name = ch.get("name");
            std::string v = name == "time_of_day.id" && !vars_.count(name) ? g_.tod_id() : vars_[name];
            for (const auto& a : ch.attrs) {
                if (a.first == "name") continue;
                std::string w = subst(a.second, c);
                double x = atof(v.c_str()), y = atof(w.c_str());
                if (a.first == "equals" || a.first == "boolean_equals") r = r && v == w;
                else if (a.first == "not_equals" || a.first == "boolean_not_equals") r = r && v != w;
                else if (a.first == "numerical_equals") r = r && x == y;
                else if (a.first == "numerical_not_equals") r = r && x != y;
                else if (a.first == "greater_than") r = r && x > y;
                else if (a.first == "less_than") r = r && x < y;
                else if (a.first == "greater_than_equal_to") r = r && x >= y;
                else if (a.first == "less_than_equal_to") r = r && x <= y;
                else if (a.first == "contains") r = r && v.find(w) != std::string::npos;
            }
        } else if (ch.tag == "have_unit") {
            int n_match = (int)find_units(ch, c).size();
            std::string cnt = ch.get("count");
            r = cnt.empty() ? n_match > 0 : in_ranges(cnt, n_match);
        } else if (ch.tag == "not") {
            r = !cond(ch, c);
        } else if (ch.tag == "and") {
            r = cond(ch, c);
        } else {
            unsupported_["cond:" + ch.tag]++;
            r = false;
        }
        ok = ok && r;
    }
    for (const auto& ch : n.kids) if (ch.tag == "or" && cond(ch, c)) return true;
    return ok;
}

// ---------------------------------------------------------------------------
// Messages
// ---------------------------------------------------------------------------
std::string EventEngine::portrait_for(const Unit* u, const std::string& image) const {
    auto exists = [](const std::string& p) { return gb::file_exists(p.c_str()); };
    if (!image.empty() && image.find("wesnoth-icon") == std::string::npos) {
        std::string b = image.substr(image.find_last_of('/') + 1);
        b = b.substr(0, b.find('~'));
        b = b.substr(0, b.find('.'));
        std::string p = std::string(kPortraits) + b + ".bmp";
        if (exists(p)) return p;
    }
    if (!u) return "";
    std::string p = std::string(kPortraits) + lower_slug(u->id) + ".bmp";
    if (!u->id.empty() && exists(p)) return p;
    p = std::string(kPortraits) + "types/" + safe(u->type_id) + ".bmp";
    if (exists(p)) return p;
    p = std::string(kPortraits) + "types/" + safe(u->type_id.substr(0, u->type_id.find(':'))) + ".bmp";
    return exists(p) ? p : "";
}

void EventEngine::queue_message(const WNode& n, const Ctx& c) {
    // Bascule « Astuces : Activées/Désactivées » du menu de bataille : les
    // messages marqués tip="yes" (nos propres conseils, pas le WML original
    // qui n'a pas cette distinction) sont simplement sautés quand désactivés.
    if (n.get("tip") == "yes" && !wsg::campaign_state().tips_enabled) return;
    std::string sp = n.get("speaker");
    Unit* u = nullptr;
    if (sp == "unit") u = c.u1 ? g_.unit_by_uid(c.u1) : nullptr;
    else if (sp == "second_unit") u = c.u2 ? g_.unit_by_uid(c.u2) : nullptr;
    else if (!sp.empty() && sp != "narrator") {
        for (auto& x : g_.units()) if (x.alive() && x.id == sp) { u = &x; break; }
    } else if (sp.empty()) {
        // filtre d'unité en attributs directs (ex. side=4 + [filter_adjacent])
        WNode f = n;
        f.attrs.erase(std::remove_if(f.attrs.begin(), f.attrs.end(), [](const auto& a) {
                          return a.first == "message" || a.first == "image" || a.first == "caption";
                      }), f.attrs.end());
        f.kids.erase(std::remove_if(f.kids.begin(), f.kids.end(), [](const WNode& k) {
                         return k.tag == "option" || k.tag == "show_if";
                     }), f.kids.end());
        if (!f.attrs.empty() || !f.kids.empty()) {
            auto us = find_units(f, c);
            if (us.empty()) return;              // Wesnoth : pas d'orateur, pas de message
            u = us.front();
        }
    }
    const bool narrator = sp == "narrator" || (sp.empty() && !u);
    if (!narrator && !u) return;                 // orateur absent ou mort
    EventMessage m;
    m.text = wesnoth_sg::strip_markup(subst(n.get("message"), c));
    if (m.text.empty() && !n.child("option")) return;
    // Traduction française pré-calculée par tools/wesnoth_data/add_intro_fr.py
    // (uniquement pour la séquence intro_messages, cf. load() ci-dessous) --
    // vide pour les messages dynamiques (die/moveto/...), qui restent
    // affichés en anglais comme avant.
    if (!n.get("message_fr").empty())
        m.text_fr = wesnoth_sg::strip_markup(subst(n.get("message_fr"), c));
    m.speaker = n.get("caption", u ? (u->name.empty() ? u->t->name : u->name) : "");
    m.portrait = portrait_for(u, n.get("image"));
    m.unit_uid = u ? u->uid : 0;
    std::vector<const WNode*> opts;
    for (const auto& k : n.kids) {
        if (k.tag != "option") continue;
        const WNode* si = k.child("show_if");
        if (si && !cond(*si, c)) continue;
        m.options.push_back(wesnoth_sg::strip_markup(subst(k.get("message", k.get("label")), c)));
        opts.push_back(&k);
    }
    if (!opts.empty()) {
        pending_options_ = opts;
        pending_ctx_ = c;
        waiting_ = true;                         // la suite attend le choix
    }
    queue_.push_back(std::move(m));
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------
void EventEngine::exec(const WNode& n, const Ctx& c) {
    const std::string& t = n.tag;
    auto side_of = [&](const WNode& x) { return atoi(subst(x.get("side", "1"), c).c_str()); };
    auto top_filter = [&](const WNode& x) {       // filtre écrit directement dans la balise
        WNode f;
        f.tag = "filter";
        for (const auto& a : x.attrs)
            if (a.first != "to_x" && a.first != "to_y" && a.first != "animate" && a.first != "fire_event" &&
                a.first != "amount" && a.first != "restore_statuses" && a.first != "fire_event")
                f.attrs.push_back(a);
        for (const auto& k : x.kids) if (k.tag != "secondary_unit") f.kids.push_back(k);
        return f;
    };
    if (t == "message") {
        queue_message(n, c);
    } else if (t == "unit") {
        std::vector<std::string> traits;
        if (const WNode* m = n.child("modifications"))
            for (const auto& k : m->kids)
                if (k.tag == "trait") { std::string id = k.get("id"); traits.push_back(id.compare(0, 5, "loyal") == 0 ? "loyal" : id); }
        std::string type = subst(n.get("type"), c);
        std::string var = n.get("variation");
        if (!var.empty() && g_.data().type(type + ":" + var)) type += ":" + var;
        if (!g_.data().type(type)) { unsupported_["unit:" + type]++; return; }
        HexCoord p{atoi(subst(n.get("x"), c).c_str()), atoi(subst(n.get("y"), c).c_str())};
        Unit u = g_.make_unit(type, side_of(n), p, traits, n.get("random_traits", "yes") != "no");
        u.id = n.get("id");
        u.name = subst(n.get("name"), c);
        u.role = n.get("role");
        u.canrecruit = n.get("canrecruit") == "yes";
        u.guardian = n.get("ai_special") == "guardian";
        g_.spawn(u);
    } else if (t == "kill") {
        WNode f = top_filter(n);
        const bool ev = n.get("fire_event") == "yes";
        for (Unit* u : find_units(f, c)) {
            if (ev) g_.kill_unit(*u, nullptr);
            else u->hp = 0;
        }
    } else if (t == "modify_unit") {
        const WNode* f = n.child("filter");
        if (!f) return;
        for (Unit* u : find_units(*f, c)) {
            for (const auto& a : n.attrs) {
                std::string v = subst(a.second, c);
                if (a.first == "name") u->name = v;
                else if (a.first == "side") u->side = atoi(v.c_str());
                else if (a.first == "role") u->role = v;
                else if (a.first == "canrecruit") u->canrecruit = v == "yes";
                else if (a.first == "ai_special") u->guardian = v == "guardian";
                else if (a.first == "status.guardian") u->guardian = v == "yes";
                else if (a.first == "hitpoints") u->hp = std::min(u->max_hp, atoi(v.c_str()));
                else if (a.first == "moves") u->mp = atoi(v.c_str());
                else if (a.first == "experience") u->xp = atoi(v.c_str());
                else if (a.first == "facing" || a.first == "variation") { }
                else unsupported_["modify_unit:" + a.first]++;
            }
        }
    } else if (t == "move_unit" || t == "teleport") {
        WNode f = t == "teleport" && n.child("filter") ? *n.child("filter") : top_filter(n);
        if (t == "move_unit") f.attrs.erase(std::remove_if(f.attrs.begin(), f.attrs.end(),
                                              [](const auto& a) { return a.first == "check_passability"; }), f.attrs.end());
        auto xs = split(subst(n.get(t == "teleport" ? "x" : "to_x"), c));
        auto ys = split(subst(n.get(t == "teleport" ? "y" : "to_y"), c));
        if (xs.empty() || ys.empty()) return;
        HexCoord to{atoi(xs.back().c_str()), atoi(ys.back().c_str())};
        for (Unit* u : find_units(f, c)) {
            Unit* occ = g_.unit_at(to);
            if (occ && occ->uid != u->uid) {      // case occupée : case libre voisine
                for (HexCoord nb : hex_neighbors(to)) if (g_.in_map(nb) && !g_.unit_at(nb)) { to = nb; break; }
            }
            u->pos = to;
            break;
        }
    } else if (t == "heal_unit") {
        const WNode* f = n.child("filter");
        for (auto& u : g_.units()) {
            if (!u.alive() || (f && !match_unit(*f, u, c))) continue;
            std::string amt = n.get("amount", "full");
            u.hp = amt == "full" ? u.max_hp : std::min(u.max_hp, u.hp + atoi(amt.c_str()));
            if (n.get("restore_statuses", "yes") == "yes") u.poisoned = u.slowed = false;
        }
    } else if (t == "gold") {
        for (const auto& s : split(subst(n.get("side", "1"), c)))
            if (Side* sd = g_.side(atoi(s.c_str()))) sd->gold += atoi(subst(n.get("amount"), c).c_str());
    } else if (t == "modify_side") {
        for (const auto& s : split(subst(n.get("side", "1"), c))) {
            Side* sd = g_.side(atoi(s.c_str()));
            if (!sd) continue;
            if (n.find("gold")) sd->gold = atoi(subst(n.get("gold"), c).c_str());
            if (n.find("income")) sd->income_mod = atoi(subst(n.get("income"), c).c_str());
            if (n.find("recruit")) sd->recruit = split(n.get("recruit"));
            if (n.find("team_name")) sd->teams = split(n.get("team_name"));
        }
    } else if (t == "modify_turns") {
        if (n.find("value")) g_.set_turn_limit(atoi(n.get("value").c_str()));
        if (n.find("add")) g_.set_turn_limit(g_.turn_limit() + atoi(n.get("add").c_str()));
        if (n.find("current")) g_.set_turn(atoi(n.get("current").c_str()));
    } else if (t == "objectives") {
        std::vector<std::string> o;
        for (const auto& k : n.kids)
            if (k.tag == "objective") o.push_back((k.get("condition") == "win" ? "+ " : "- ") + subst(k.get("description"), c));
        if (!o.empty()) g_.set_objectives(o);
    } else if (t == "endlevel") {
        std::string r = n.get("result", "victory");
        g_.set_outcome(r == "defeat" ? Outcome::Defeat : Outcome::Victory, r == "defeat" ? "Défaite" : "Victoire");
    } else if (t == "recall") {
        std::string id = subst(n.get("id"), c);
        if (id.empty() || g_.has_live_id(id)) return;    // déjà présent, ou pas de nom
        const PersistentUnit* found = nullptr;
        for (const auto& p : campaign_state().roster) if (p.id == id) { found = &p; break; }
        if (!found || !g_.data().type(found->type_id)) { unsupported_["recall:" + id]++; return; }
        HexCoord where{-1, -1};
        if (n.find("x") && n.find("y")) where = {atoi(subst(n.get("x"), c).c_str()), atoi(subst(n.get("y"), c).c_str())};
        if (where.x < 0) {                              // pas de position donnée : premier donjon venu
            for (int y = 0; y < g_.height() && where.x < 0; ++y)
                for (int x = 0; x < g_.width(); ++x)
                    if (g_.terrain({x, y}).keep) { where = {x, y}; break; }
        }
        Unit* placed = g_.place_recall(*found, where);
        if (placed) fire("recall", placed->uid, 0, placed->pos);
    } else if (t == "terrain") {
        auto xs = split(n.get("x")), ys = split(n.get("y"));
        std::string code = n.get("terrain");
        for (size_t i = 0; i < xs.size() && i < ys.size(); ++i)
            g_.set_terrain({atoi(xs[i].c_str()), atoi(ys[i].c_str())}, code);
    } else if (t == "capture_village") {
        auto xs = split(n.get("x")), ys = split(n.get("y"));
        for (size_t i = 0; i < xs.size() && i < ys.size(); ++i)
            g_.capture({atoi(xs[i].c_str()), atoi(ys[i].c_str())}, side_of(n));
    } else if (t == "set_variable") {
        std::string name = subst(n.get("name"), c);
        std::string& v = vars_[name];
        if (n.find("value")) v = subst(n.get("value"), c);
        else if (n.find("add")) v = std::to_string(atof(v.c_str()) + atof(subst(n.get("add"), c).c_str()));
        else if (n.find("sub")) v = std::to_string(atof(v.c_str()) - atof(subst(n.get("sub"), c).c_str()));
        else if (n.find("multiply")) v = std::to_string(atof(v.c_str()) * atof(subst(n.get("multiply"), c).c_str()));
        // entiers affichés sans décimales
        if (v.find('.') != std::string::npos && atof(v.c_str()) == (double)atoi(v.c_str())) v = std::to_string(atoi(v.c_str()));
    } else if (t == "clear_variable") {
        for (const auto& s : split(n.get("name"))) {
            for (auto it = vars_.begin(); it != vars_.end();)
                it = (it->first == s || it->first.compare(0, s.size() + 1, s + ".") == 0) ? vars_.erase(it) : std::next(it);
        }
    } else if (t == "store_time_of_day") {
        vars_[n.get("variable", "time_of_day") + ".id"] = g_.tod_id();
    } else if (t == "if") {
        if (cond(n, c)) {
            for (const auto& k : n.kids) if (k.tag == "then") push(k.kids, c);
        } else {
            for (const auto& k : n.kids) {
                if (k.tag == "elseif" && cond(k, c)) {
                    for (const auto& th : k.kids) if (th.tag == "then") push(th.kids, c);
                    return;
                }
            }
            for (const auto& k : n.kids) if (k.tag == "else") push(k.kids, c);
        }
    } else if (t == "fire_event") {
        std::string name = subst(n.get("name"), c);
        // empilé au-dessus : s'exécute avant la suite de l'événement courant
        fire(name, c.u1, c.u2, c.loc);
    } else if (t == "event") {
        add_event(n);
    } else if (t == "remove_event") {
        auto ids = split(n.get("id"));
        for (auto& ev : events_) if (std::find(ids.begin(), ids.end(), ev.id) != ids.end()) ev.dead = true;
    } else if (t == "micro_ai") {
        std::string type = n.get("ai_type");
        if (type == "zone_guardian" && n.get("action", "add") != "delete") {
            const WNode* f = n.child("filter");
            const WNode* fl = n.child("filter_location");
            if (!f) return;
            for (Unit* u : find_units(*f, c)) {
                u->zone_x = atoi(n.get("station_x").c_str());
                u->zone_y = atoi(n.get("station_y").c_str());
                u->zone_r = fl ? atoi(fl->get("radius", "1").c_str()) : 1;
            }
        } else {
            unsupported_["micro_ai:" + type]++;
        }
    } else if (t == "delay" || t == "redraw" || t == "scroll_to" || t == "scroll_to_unit" || t == "sound" ||
               t == "music" || t == "animate_unit" || t == "allow_undo" || t == "allow_end_turn" ||
               t == "disallow_end_turn" || t == "cancel_action" || t == "lock_view" || t == "unlock_view" ||
               t == "display_tip" || t == "item" || t == "remove_item" || t == "set_menu_item" ||
               t == "clear_menu_item" || t == "print" || t == "floating_text" || t == "fade_out_music" ||
               t == "color_adjust" || t == "move_unit_fake" || t == "hide_unit" || t == "unhide_unit" ||
               t == "filter" || t == "filter_second" || t == "filter_condition" || t == "set_achievement" ||
               t == "screen_fade" || t == "label" || t == "unsynced" || t == "set_global_variable" ||
               t == "display_tip") {
        // purement visuel/sonore, succès/astuces, ou déjà traité
    } else if (t == "modify_ai") {
        unsupported_["modify_ai (réglage IA ignoré)"]++;
        // purement visuel/sonore, ou déjà traité
    } else {
        unsupported_[t]++;
    }
}

}  // namespace wsg
