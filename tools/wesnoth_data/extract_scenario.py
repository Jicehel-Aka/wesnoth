#!/usr/bin/env python3
"""extract_scenario.py — état de départ d'une bataille, lu dans le vrai WML.

Les scénarios de The South Guard ne posent pas leurs unités dans des [side]
statiques : tout passe par des événements (prestart, start, puis des
événements en chaîne via [fire_event]) et des macros du cœur de Wesnoth.
Ce script est donc un mini-interpréteur WML qui EXÉCUTE ces événements de
mise en place, comme le moteur le ferait, jusqu'au début de la bataille, puis
exporte l'état obtenu :
  carte, horaire (moment de la journée), tour de départ et limite, camps
  (or, revenu, recrutement, équipe, contrôleur), unités (type, position,
  traits imposés, garde...), villages possédés, objectifs, conditions de
  victoire/défaite liées à la mort d'une unité, modificateur d'XP.

Lorsqu'un [message] propose des options (ex. « passer le tutoriel »), le
choix est donné par --choose (texte recherché dans l'événement déclenché).

Les événements qui ne sont PAS de la mise en place (tour N, attaque, mort,
déplacement...) sont listés dans "runtime_events" : ils devront être joués
par le moteur embarqué. Ce qui n'est pas encore pris en charge est signalé
dans "unsupported" plutôt qu'ignoré en silence.
"""
import argparse
import glob
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wml  # noqa: E402

DIFF_INDEX = {"EASY": 0, "NORMAL": 1, "HARD": 2}
SCHEDULE = ["dawn", "morning", "afternoon", "dusk", "first_watch", "second_watch"]


# ---------------------------------------------------------------------------
# Macros du cœur de Wesnoth (data/core/macros, non fournis) : réécrites ici
# à l'identique de leur effet documenté.
# ---------------------------------------------------------------------------
def make_builtins(difficulty):
    di = DIFF_INDEX[difficulty]

    def on_diff(pp, a):
        return a[di] if len(a) > di else (a[-1] if a else "")

    def on_diff4(pp, a):
        return a[di] if len(a) > di else ""

    def generic_unit(pp, a):
        s, t, x, y = (a + ["", "", "", ""])[:4]
        return f"[unit]\nside={s}\ntype={t}\nx={x}\ny={y}\nrandom_traits=yes\n[/unit]\n"

    def generic_unitc(pp, a):
        s, t, x, y = (a + ["", "", "", ""])[:4]
        wml_ = a[4] if len(a) > 4 else ""
        if t.strip() in ("none", '"none"'):
            return ""
        return generic_unit(pp, [s, t, x, y]) + wml_

    def named_unit(pp, a):
        s, t, x, y, uid, name = (a + [""] * 7)[:6]
        wml_ = a[6] if len(a) > 6 else ""
        return (f"[unit]\nside={s}\ntype={t}\nx={x}\ny={y}\nid={uid}\nname={name}\n"
                f"random_traits=yes\n{wml_}\n[/unit]\n")

    def trait(name):
        tid = name[len("TRAIT_"):].lower()
        if tid.endswith("_musthave"):
            tid = tid[: -len("_musthave")]
        if tid == "loyal_hero":
            tid = "loyal"
        return lambda pp, a: f"[trait]\nid={tid}\n[/trait]\n"

    def schedule(start):
        return lambda pp, a: f"__schedule_start={start}\n"

    b = {
        "ON_DIFFICULTY": on_diff,
        "ON_DIFFICULTY4": on_diff4,
        "GENERIC_UNIT": generic_unit,
        "GENERIC_UNITC": generic_unitc,
        "NAMED_UNIT": named_unit,
        # Dans le cœur, FACING et GUARDIAN rouvrent l'unité précédente : [+unit]
        "FACING": lambda pp, a: f"[+unit]\nfacing={a[0] if a else ''}\n[/unit]\n",
        "GUARDIAN": lambda pp, a: "[+unit]\nai_special=guardian\n[/unit]\n",
        "VARIABLE": lambda pp, a: f"[set_variable]\nname={a[0]}\nvalue={a[1] if len(a) > 1 else ''}\n[/set_variable]\n",
        "CLEAR_VARIABLE": lambda pp, a: f"[clear_variable]\nname={a[0] if a else ''}\n[/clear_variable]\n",
        "VARIABLE_CONDITIONAL": lambda pp, a: f"[variable]\nname={a[0]}\n{a[1]}={a[2] if len(a) > 2 else ''}\n[/variable]\n",
        "VARIABLE_OP": lambda pp, a: f"[set_variable]\nname={a[0]}\n{a[1]}={a[2] if len(a) > 2 else ''}\n[/set_variable]\n",
        "MODIFY_UNIT": lambda pp, a: f"[modify_unit]\n[filter]\n{a[0]}\n[/filter]\n{a[1]}={a[2] if len(a) > 2 else ''}\n[/modify_unit]\n",
        "TELEPORT_UNIT": lambda pp, a: f"[teleport]\n[filter]\n{a[0]}\n[/filter]\nx={a[1]}\ny={a[2]}\n[/teleport]\n",
        "MOVE_UNIT": lambda pp, a: f"[move_unit]\n{a[0]}\nto_x={a[1]}\nto_y={a[2]}\n[/move_unit]\n",
        "KILL": None,  # défini par la campagne
        "TURNS_RUN_OUT": lambda pp, a: "[objective]\ncondition=lose\ndescription=Turns run out\n__turns=1\n[/objective]\n",
        "DEFAULT_SCHEDULE": schedule(0),
        "DEFAULT_SCHEDULE_DAWN": schedule(0),
        "DEFAULT_SCHEDULE_MORNING": schedule(1),
        "DEFAULT_SCHEDULE_AFTERNOON": schedule(2),
        "DEFAULT_SCHEDULE_DUSK": schedule(3),
        "DEFAULT_SCHEDULE_FIRST_WATCH": schedule(4),
        "DEFAULT_SCHEDULE_SECOND_WATCH": schedule(5),
    }
    for t in ("STRONG", "QUICK", "INTELLIGENT", "RESILIENT", "DEXTROUS", "HEALTHY", "WEAK", "SLOW",
              "DIM", "AGED", "FEARLESS", "FERAL", "LOYAL", "LOYAL_HERO", "UNDEAD", "MECHANICAL",
              "ELEMENTAL", "FEARLESS_MUSTHAVE", "FERAL_MUSTHAVE"):
        b["TRAIT_" + t] = trait("TRAIT_" + t)
    return {k: v for k, v in b.items() if v}


# ---------------------------------------------------------------------------
class State:
    def __init__(self, scen, map_dir, terrain):
        self.scen = scen
        self.map_dir = map_dir
        self.terrain = terrain
        self.map_file = scen.get("map_file")
        self.load_map()
        self.units = []
        self.sides = {}
        self.villages = {}          # (x,y) -> side
        self.vars = {}
        self.turns = -1
        self.current_turn = 1
        self.objectives = []
        self.gold_carryover = None
        self.events = {}            # nom -> [Node]
        self.runtime = []
        self.unsupported = {}
        self.messages = 0
        self.intro = []             # messages affichés pendant la mise en place
        self.consumed = set()       # événements déjà déclenchés (first_time_only)
        self.removed = set()

    def load_map(self):
        self.grid = []
        path = os.path.join(self.map_dir, self.map_file)
        for line in open(path, encoding="utf-8"):
            if not line.strip():
                continue
            row = []
            for c in line.split(","):
                c = c.strip()
                if c:
                    row.append(c.split()[-1])
            self.grid.append(row)

    def is_village(self, x, y):
        if 0 <= y < len(self.grid) and 0 <= x < len(self.grid[y]):
            return self.terrain.get(self.grid[y][x], {}).get("village") == 1
        return False

    def find(self, flt):
        res = []
        for u in self.units:
            ok = True
            for k in ("id", "side", "type"):
                if k in flt.attrs and str(u.get(k)) != flt.attrs[k]:
                    ok = False
            if "x" in flt.attrs and str(u["x"]) != flt.attrs["x"]:
                ok = False
            if "y" in flt.attrs and str(u["y"]) != flt.attrs["y"]:
                ok = False
            if ok:
                res.append(u)
        return res

    def note(self, what):
        self.unsupported[what] = self.unsupported.get(what, 0) + 1


def hex_dist(ax, ay, bx, by):
    def cube(x, y):
        z = x
        cx = y - (x - (x & 1)) // 2
        return cx, -cx - z, z
    a, b = cube(ax, ay), cube(bx, by)
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]), abs(a[2] - b[2]))


def unit_from(node, st):
    u = {k: v for k, v in node.attrs.items()
         if k in ("id", "type", "side", "name", "canrecruit", "facing", "ai_special",
                  "variation", "x", "y", "upkeep", "role", "random_traits")}
    for k in ("x", "y", "side"):
        if k in u:
            u[k] = int(u[k])
    traits = []
    mods = node.first("modifications")
    if mods is not None:
        for t in mods.all("trait"):
            if t.get("id"):
                tid = t.get("id")
                traits.append("loyal" if tid.startswith("loyal") else tid)
        if mods.all("object"):
            u["objects"] = [{"effects": [e.attrs for e in o.all("effect")]} for o in mods.all("object")]
    if traits:
        u["traits"] = traits
    if node.get("ai_special") == "guardian":
        u["guardian"] = 1
    return u


def run_actions(node, st, choose):
    for c in node.children:
        run_action(c, st, choose)


def eval_cond(node, st):
    """[if]/[show_if] : conditions implicites (ET) + [or]/[not]."""
    ok = True
    for c in node.children:
        if c.tag == "have_unit":
            ok = ok and bool(st.find(c))
        elif c.tag == "variable":
            v = st.vars.get(c.get("name"))
            for op, val in c.attrs.items():
                if op == "name":
                    continue
                if op in ("equals", "boolean_equals"):
                    ok = ok and str(v) == val
                elif op in ("not_equals", "boolean_not_equals"):
                    ok = ok and str(v) != val
                elif op in ("numerical_equals", "greater_than", "less_than",
                            "greater_than_equal_to", "less_than_equal_to"):
                    try:
                        a, b = float(v or 0), float(val)
                    except ValueError:
                        a, b = 0, 0
                    ok = ok and {"numerical_equals": a == b, "greater_than": a > b, "less_than": a < b,
                                 "greater_than_equal_to": a >= b, "less_than_equal_to": a <= b}[op]
        elif c.tag == "not":
            ok = ok and not eval_cond(c, st)
        elif c.tag == "or":
            ok = ok or eval_cond(c, st)
        elif c.tag in ("then", "else", "elseif"):
            continue
        else:
            st.note("condition:" + c.tag)
    return ok


def run_action(n, st, choose):
    t = n.tag
    if t == "unit":
        u = unit_from(n, st)
        st.units.append(u)
    elif t == "modify_unit":
        f = n.first("filter")
        for u in st.find(f) if f is not None else []:
            for k, v in n.attrs.items():
                if k in ("name", "facing", "variation", "canrecruit", "side", "ai_special", "role"):
                    u[k] = int(v) if k == "side" else v
                elif k == "status.guardian":
                    if v == "yes":
                        u["guardian"] = 1
                    else:
                        u.pop("guardian", None)
    elif t == "teleport":
        for u in st.find(n.first("filter")):
            u["x"], u["y"] = int(n.get("x")), int(n.get("y"))
    elif t == "move_unit":
        for u in st.find(n):
            u["x"], u["y"] = int(n.get("to_x").split(",")[-1]), int(n.get("to_y").split(",")[-1])
    elif t == "kill":
        for u in st.find(n):
            st.units.remove(u)
    elif t == "replace_map":
        st.map_file = n.get("map_file")
        st.load_map()
    elif t == "capture_village":
        side = int(n.get("side", "0"))
        xs, ys = n.get("x"), n.get("y")
        r = int(n.get("radius", "0"))
        if xs and ys:
            cx, cy = int(xs), int(ys)
            for y in range(len(st.grid)):
                for x in range(len(st.grid[y])):
                    if hex_dist(cx, cy, x, y) <= r and st.is_village(x, y):
                        st.villages[(x, y)] = side
    elif t == "modify_side":
        s = st.sides.setdefault(int(n.get("side")), {})
        for k in ("gold", "income", "hidden", "team_name", "controller", "recruit"):
            if k in n.attrs:
                s[k] = n.attrs[k]
    elif t == "gold":
        s = st.sides.setdefault(int(n.get("side")), {})
        s["gold"] = str(int(s.get("gold", 0)) + int(n.get("amount")))
    elif t == "modify_turns":
        if n.get("value"):
            st.turns = int(n.get("value"))
        if n.get("current"):
            st.current_turn = int(n.get("current"))
        if n.get("add"):
            st.turns += int(n.get("add"))
    elif t == "objectives":
        for o in n.all("objective"):
            st.objectives.append({"condition": o.get("condition"), "text": o.get("description")})
        gc = n.first("gold_carryover")
        if gc is not None:
            st.gold_carryover = {"bonus": gc.get("bonus") == "yes",
                                 "percent": int(gc.get("carryover_percentage", "80"))}
    elif t == "store_unit":
        # [store_unit] : copie les unités (dict) dans une variable, kill=yes
        # les retire de la carte en attendant [unstore_unit]
        f = n.first("filter")
        found = st.find(f) if f is not None else []
        st.vars[n.get("variable", "unit")] = [dict(u) for u in found]
        if n.get("kill") == "yes":
            for u in found:
                st.units.remove(u)
    elif t == "unstore_unit":
        v = st.vars.get(n.get("variable", "unit"))
        for u in (v if isinstance(v, list) else [v] if isinstance(v, dict) else []):
            st.units.append(u)
    elif t == "foreach":
        arr = st.vars.get(n.get("array"), [])
        for item in list(arr) if isinstance(arr, list) else []:
            st.vars["this_item"] = item
            for d in n.all("do"):
                run_actions(d, st, choose)
        st.vars.pop("this_item", None)
    elif t == "set_variable" and "." in n.get("name", ""):
        # this_item.facing = ... : champ d'une unité stockée
        base, _, field = n.get("name").partition(".")
        obj = st.vars.get(base)
        if isinstance(obj, dict) and "value" in n.attrs:
            v = n.get("value")
            obj[field] = int(v) if field in ("x", "y", "side") and v.lstrip("-").isdigit() else v
    elif t == "set_variable":
        name = n.get("name")
        if "value" in n.attrs:
            st.vars[name] = n.get("value")
        elif "add" in n.attrs:
            st.vars[name] = str(float(st.vars.get(name, 0) or 0) + float(n.get("add")))
    elif t == "clear_variable":
        st.vars.pop(n.get("name"), None)
    elif t == "fire_event":
        fire(n.get("name"), st, choose)
    elif t == "if":
        branch = "then" if eval_cond(n, st) else "else"
        for b in n.all(branch):
            run_actions(b, st, choose)
    elif t == "message":
        st.messages += 1
        if n.get("message"):
            st.intro.append(message_json(n))
        opts = n.all("option")
        if opts:
            pick = None
            for o in opts:
                if choose and choose in repr_node(o):
                    pick = o
            pick = pick or opts[0]
            for cmd in pick.all("command"):
                run_actions(cmd, st, choose)
    elif t == "remove_event":
        for i in [x.strip() for x in n.get("id", "").split(",") if x.strip()]:
            st.removed.add(i)
    elif t == "store_time_of_day":
        pass
    elif t == "event":
        for name in wml.csv_names(n.get("name", "")) if hasattr(wml, "csv_names") else [x.strip() for x in n.get("name", "").split(",")]:
            st.events.setdefault(name, []).append(n)
            st.runtime.append(summarize_event(name, n))
    elif t in ("delay", "scroll_to", "sound", "music", "store_unit", "unstore_unit", "redraw", "animate_unit",
               "fade_out_music", "print", "micro_ai", "modify_ai", "allow_undo", "remove_event",
               "set_menu_item", "object", "lua", "tutor", "set_achievement", "hide_unit", "unhide_unit",
               "lock_view", "unlock_view", "color_adjust", "screen_fade", "move_units_fake", "terrain_mask",
               "item", "remove_item", "label", "floating_text", "clear_menu_item", "set_variables"):
        if t in ("micro_ai", "modify_ai", "object", "lua", "set_variables", "terrain_mask"):
            st.note(t)
    else:
        st.note(t)


def repr_node(n):
    s = [n.tag, str(n.attrs)]
    for c in n.children:
        s.append(repr_node(c))
    return " ".join(s)


def summarize_event(name, n):
    e = {"name": name}
    for f in ("filter", "filter_second"):
        fn = n.first(f)
        if fn is not None:
            e[f] = fn.attrs
    el = [c for c in n.walk() if c.tag == "endlevel"]
    if el:
        e["endlevel"] = el[0].get("result")
    return e


def contains_recall(n):
    if n.tag == "recall":
        return True
    return any(contains_recall(c) for c in n.children)


def fire(name, st, choose):
    for ev in list(st.events.get(name, [])):
        if contains_recall(ev):
            # Dépend du roster reporté (inconnu à l'extraction, propre à
            # chaque partie) : on n'exécute PAS ce bloc hors ligne, il doit
            # rester un événement rejoué en jeu par l'interpréteur embarqué,
            # qui lui a accès à campaign_state (voir battle/campaign_state.h).
            continue
        if ev.get("first_time_only", "yes") != "no":
            st.consumed.add(id(ev))
        run_actions(ev, st, choose)


def message_json(n):
    m = {k: n.get(k) for k in ("speaker", "image", "message", "caption") if n.get(k)}
    for k in ("id", "side", "type", "role"):
        if n.get(k) and "speaker" not in m:
            m.setdefault("filter", {})[k] = n.get(k)
    return m


def node_json(n):
    """Arbre WML -> JSON compact {t, a, c} pour l'interpréteur embarqué."""
    d = {"t": n.tag}
    if n.attrs:
        d["a"] = {k: v for k, v in n.attrs.items() if k not in ("help_text", "description") or n.tag in ("objective", "objectives")}
    kids = [node_json(c) for c in n.children
            if c.tag not in ("animation", "frame", "effect", "ai", "candidate_action", "set_menu_item",
                             "display_tip", "sound", "music", "animate_unit", "item", "remove_item")]
    if kids:
        d["c"] = kids
    return d


SETUP_NAMES = {"prestart", "start"}


def runtime_nodes(st):
    """Événements encore actifs après la mise en place, à rejouer en jeu."""
    out, seen = [], set()
    for name, evs in st.events.items():
        for ev in evs:
            if id(ev) in seen:
                continue
            seen.add(id(ev))
            if id(ev) in st.consumed:
                # Exécuté (et first_time_only) pendant l'extraction : son
                # contenu est déjà figé dans l'état statique du scénario.
                continue
            if ev.get("id") and ev.get("id") in st.removed:
                continue
            out.append(node_json(ev))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--campaign", required=True)
    ap.add_argument("--scenario", required=True, help="fichier .cfg du scénario")
    ap.add_argument("--terrain-json", required=True)
    ap.add_argument("--difficulty", default="NORMAL", choices=list(DIFF_INDEX))
    ap.add_argument("--choose", default="skip_tutorial",
                    help="texte à rechercher pour choisir une option de [message]")
    ap.add_argument("--macros", help="dossier data/core/macros : macros réelles du cœur")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    pp = wml.Preprocessor(defines=(a.difficulty, "CAMPAIGN_THE_SOUTH_GUARD"),
                          builtins=make_builtins(a.difficulty))
    # macros réelles du cœur de Wesnoth d'abord, puis celles de la campagne
    if a.macros:
        for f in sorted(glob.glob(os.path.join(a.macros, "*.cfg"))):
            pp.load_defines(f)
    for f in sorted(glob.glob(os.path.join(a.campaign, "utils", "*.cfg"))):
        pp.load_defines(f)
    # Les réécritures Python ne servent plus que de secours : une macro dont
    # la vraie définition est disponible est toujours développée telle quelle.
    # Exception : GENERIC_UNITC (campagne) passe par une variable WML ; sa
    # version intégrée donne le même résultat sans interpréteur de variables.
    for k in list(pp.builtins):
        if k in pp.macros and k not in ("GENERIC_UNITC", "MODIFY_UNIT"):
            del pp.builtins[k]
    # [modify_unit] est une vraie balise WML équivalente à la macro (qui passe
    # par store/foreach/unstore) : plus simple à rejouer côté console.
    for k in ("GENERIC_UNITC", "MODIFY_UNIT"):
        pp.macros.pop(k, None)
    root = wml.load(a.scenario, pp)
    scen = root.first("scenario")
    terrain = json.load(open(a.terrain_json, encoding="utf-8"))["codes"]
    st = State(scen, os.path.join(a.campaign, "maps"), terrain)
    st.turns = int(scen.get("turns", "-1"))

    for s in scen.all("side"):
        sd = {k: v for k, v in s.attrs.items()
              if k in ("side", "controller", "team_name", "user_team_name", "gold", "income",
                       "recruit", "no_leader", "hidden", "color", "defeat_condition", "save_id")}
        st.sides[int(s.get("side"))] = sd
        for u in s.all("unit"):
            uu = unit_from(u, st); uu["side"] = int(s.get("side"))
            st.units.append(uu)
        if s.get("type"):  # chef déclaré directement dans [side]
            uu = unit_from(s, st); uu["side"] = int(s.get("side")); uu["canrecruit"] = "yes"
            st.units.append(uu)
    # événements globaux de la campagne ([campaign] de _main.cfg) : ils
    # s'appliquent à chaque scénario (ex. défaite à la mort de Deoran)
    main_cfg = os.path.join(a.campaign, "_main.cfg")
    camp_events = []
    if os.path.exists(main_cfg):
        mroot = wml.load(main_cfg, pp)
        for c in mroot.walk():
            if c.tag == "campaign":
                camp_events = [e for e in c.children if e.tag == "event"]
    for n in list(scen.children) + camp_events:
        if n.tag == "event":
            for name in [x.strip() for x in n.get("name", "").split(",")]:
                st.events.setdefault(name, []).append(n)
    # déroulé de la mise en place
    fire("prestart", st, a.choose)
    fire("start", st, a.choose)

    # victoire / défaite liées à la mort d'une unité (événements de haut niveau
    # ET événements posés pendant la mise en place)
    death_win, death_lose = [], []
    for name in ("die", "last breath"):
        for ev in st.events.get(name, []):
            f = ev.first("filter")
            res = [c.get("result") for c in ev.walk() if c.tag == "endlevel"]
            if f is not None and res:
                (death_win if res[0] == "victory" else death_lose).append(f.attrs)

    # micro-IA « zone_guardian » posées par des événements (ex. Mari garde le
    # pont) : exportées comme comportement d'unité, appliqué dès le départ
    for evs in st.events.values():
        for ev in evs:
            for m in ev.walk():
                if m.tag == "micro_ai" and "zone_guardian" in m.get("ai_type", "") + m.get("side,action,ai_type", ""):
                    f = m.first("filter")
                    fl = m.first("filter_location")
                    if f is None or not m.get("station_x"):
                        continue
                    for u in st.find(f):
                        u["zone_guardian"] = {"x": int(m.get("station_x")), "y": int(m.get("station_y")),
                                              "radius": int(fl.get("radius", "1")) if fl is not None else 1}
    # horaire réel : [time] du scénario (macros DEFAULT_SCHEDULE_*) avec
    # current_time ; à défaut, l'horaire par défaut
    times = [{"id": t.get("id"), "name": t.get("name"), "lawful_bonus": int(t.get("lawful_bonus", "0"))}
             for t in scen.all("time")]
    start = int(scen.get("current_time", scen.attrs.get("__schedule_start", "0")))
    out = {
        "id": scen.get("id"),
        "name": scen.get("name"),
        "next_scenario": scen.get("next_scenario"),
        "difficulty": a.difficulty,
        "map_file": st.map_file,
        "schedule": {"start_index": start, "times": times} if times
                    else {"type": "default", "start_index": start, "order": SCHEDULE},
        "experience_modifier": int(scen.get("experience_modifier", "100")),
        "turns": st.turns,
        "current_turn": st.current_turn,
        "sides": [dict(v, side=k) for k, v in sorted(st.sides.items())],
        "units": st.units,
        "villages": [{"x": x, "y": y, "side": s} for (x, y), s in sorted(st.villages.items())],
        "objectives": st.objectives,
        "gold_carryover": st.gold_carryover,
        "victory_on_death": death_win,
        "defeat_on_death": death_lose,
        "runtime_events": st.runtime,
        "intro_messages": st.intro,
        "events": runtime_nodes(st),
        "unsupported": st.unsupported,
    }
    with open(a.out, "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=1)
    print(f"{out['id']}: carte={out['map_file']} unités={len(st.units)} villages={len(st.villages)} "
          f"tours={st.turns} (départ {st.current_turn}) événements runtime={len(st.runtime)}")
    if st.unsupported:
        print("non pris en charge:", st.unsupported)


if __name__ == "__main__":
    main()
