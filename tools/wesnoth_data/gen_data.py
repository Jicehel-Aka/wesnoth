#!/usr/bin/env python3
"""gen_data.py — génère les données de jeu depuis les VRAIS fichiers Wesnoth.

Entrées (chemins passés en argument, voir --help) :
  * data/core/units.cfg          : movetypes, races, traits globaux
  * data/core/units/             : un .cfg par type d'unité
  * data/core/terrain.cfg        : types de terrain et alias
  * data/campaigns/The_South_Guard : unités propres à la campagne + cartes

Sorties (dans --out, à copier dans /sdcard/WESNOTH_SG/data/) :
  * units.json    : types d'unités utiles à la campagne (+ lignées d'évolution),
                    movetypes, races, traits
  * terrain.json  : pour chaque code de terrain présent dans les cartes de la
                    campagne, l'expression d'alias résolue (meilleur/pire de ...)
                    pour le déplacement et la défense, + drapeaux village/
                    château/donjon/soins.

Rien n'est saisi à la main SAUF les effets des traits : leurs macros sont dans
data/core/macros/traits.cfg, qui ne fait pas partie des fichiers fournis. Les
valeurs standard de Wesnoth sont reprises ci-dessous (TRAIT_EFFECTS) et
signalées comme telles dans units.json.
"""
import argparse
import glob
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wml  # noqa: E402

# ---------------------------------------------------------------------------
# Traits (valeurs standard Wesnoth 1.18 ; source attendue :
# data/core/macros/traits.cfg — à vérifier dès que ce fichier est fourni)
# ---------------------------------------------------------------------------
TRAIT_EFFECTS = {
    "strong":      {"melee_damage": 1, "hp": 1},
    "quick":       {"mp": 1, "hp_percent": -5},
    "intelligent": {"xp_percent": -20},
    "resilient":   {"hp": 4, "hp_per_level": 1},
    "dextrous":    {"ranged_damage": 1},
    "healthy":     {"hp_per_level": 1, "always_rest_heal": 1},
    "weak":        {"melee_damage": -1, "hp": -1},
    "slow":        {"mp": -1, "hp_percent": 5},
    "dim":         {"xp_percent": 20},
    "aged":        {"hp": -8, "mp": -1, "melee_damage": -1},
    "fearless":    {"fearless": 1},
    "feral":       {"village_defense_cap": 50},
    "loyal":       {"loyal": 1},
    "undead":      {"immune_poison": 1, "immune_drain": 1, "immune_plague": 1},
    "mechanical":  {"immune_poison": 1, "immune_drain": 1, "immune_plague": 1},
    "elemental":   {"immune_poison": 1, "immune_drain": 1, "immune_plague": 1},
}
TRAIT_MACROS = {}   # rempli depuis data/core/macros/traits.cfg si fourni


def load_real_traits(macros_dir):
    """Lit les VRAIES définitions de traits (data/core/macros/traits.cfg) et
    convertit leurs [effect] dans le format compact du moteur."""
    path = os.path.join(macros_dir, "traits.cfg")
    if not os.path.exists(path):
        return False
    pp = wml.Preprocessor()
    pp.load_defines(path)
    effects = {}
    for name, (params, body) in pp.macros.items():
        if not name.startswith("TRAIT_"):
            continue
        root = wml.parse(pp.expand(body))
        tr = root.first("trait")
        if tr is None or not tr.get("id"):
            continue
        tid = tr.get("id")
        TRAIT_MACROS[name] = (tid, tr.get("availability") == "musthave")
        e = {}
        for ef in tr.all("effect"):
            at = ef.get("apply_to", "")
            if at == "attack":
                key = {"melee": "melee_damage", "ranged": "ranged_damage"}.get(ef.get("range"), "all_damage")
                e[key] = e.get(key, 0) + to_int(ef.get("increase_damage"))
            elif at == "hitpoints" and ef.get("increase_total"):
                v = ef.get("increase_total")
                if v.endswith("%"):
                    e["hp_percent"] = e.get("hp_percent", 0) + to_int(v[:-1])
                elif ef.get("times") == "per level":
                    e["hp_per_level"] = e.get("hp_per_level", 0) + to_int(v)
                else:
                    e["hp"] = e.get("hp", 0) + to_int(v)
            elif at == "movement":
                e["mp"] = e.get("mp", 0) + to_int(ef.get("increase"))
            elif at == "max_experience":
                v = ef.get("increase", "0")
                e["xp_percent"] = e.get("xp_percent", 0) + to_int(v.rstrip("%"))
            elif at == "loyal":
                e["loyal"] = 1
            elif at == "healthy":
                e["always_rest_heal"] = 1
            elif at == "fearless":
                e["fearless"] = 1
            elif at == "status":
                for st in csv(ef.get("add")):
                    k = {"unpoisonable": "immune_poison", "undrainable": "immune_drain",
                         "unplagueable": "immune_plague"}.get(st)
                    if k:
                        e[k] = 1
            elif at == "defense":
                d = ef.first("defense")
                if d is not None and d.get("village"):
                    e["village_defense_cap"] = abs(to_int(d.get("village")))
        effects[tid] = e
    TRAIT_EFFECTS.clear()
    TRAIT_EFFECTS.update(effects)
    return True


# nom de macro -> (id du trait, obligatoire ?)
def trait_from_macro(name):
    if name in TRAIT_MACROS:
        return TRAIT_MACROS[name]
    if not name.startswith("TRAIT_"):
        return None
    base = name[len("TRAIT_"):]
    must = base.endswith("_MUSTHAVE")
    if must:
        base = base[: -len("_MUSTHAVE")]
    tid = base.lower()
    if tid == "loyal_hero":
        tid = "loyal"
    return tid, must


RESIST_TYPES = ["blade", "pierce", "impact", "fire", "cold", "arcane"]


def to_int(v, d=0):
    try:
        return int(str(v).strip())
    except (TypeError, ValueError):
        return d


def csv(v):
    return [x.strip() for x in str(v or "").split(",") if x.strip()]


# ---------------------------------------------------------------------------
# units.cfg : movetypes, races, traits
# ---------------------------------------------------------------------------
def load_core(units_cfg):
    pp = wml.Preprocessor()
    root = wml.load(units_cfg, pp)
    u = root.first("units")
    movetypes = {}
    for m in u.all("movetype"):
        entry = {
            "costs": {k: to_int(v, 99) for k, v in (m.first("movement_costs").attrs if m.first("movement_costs") else {}).items()},
            "defense": {k: to_int(v, 100) for k, v in (m.first("defense").attrs if m.first("defense") else {}).items()},
            "resist": {k: to_int(v, 100) for k, v in (m.first("resistance").attrs if m.first("resistance") else {}).items()},
        }
        if m.get("flies") == "yes" or m.get("flying") == "yes":
            entry["flying"] = 1
        movetypes[m.get("name")] = entry
    global_traits = [t for t in (trait_from_macro(n) for n, _ in u.macros) if t]
    races = {}
    for r in u.all("race"):
        traits = [t for t in (trait_from_macro(n) for n, _ in r.macros) if t]
        races[r.get("id")] = {
            "num_traits": to_int(r.get("num_traits"), 0),
            "ignore_global": 1 if r.get("ignore_global_traits") == "yes" else 0,
            "traits": [t[0] for t in traits],
        }
    return movetypes, [t[0] for t in global_traits], races


# ---------------------------------------------------------------------------
# Types d'unités
# ---------------------------------------------------------------------------
def unit_from_node(n):
    d = {}
    for key, out, conv in (("name", "name", str), ("race", "race", str),
                           ("hitpoints", "hp", to_int), ("movement", "mp", to_int),
                           ("experience", "xp", to_int), ("level", "level", to_int),
                           ("alignment", "alignment", str), ("cost", "cost", to_int),
                           ("movement_type", "movetype", str),
                           ("num_traits", "num_traits", to_int)):
        if key in n.attrs:
            d[out] = conv(n.attrs[key])
    for key in ("image", "profile"):
        if key in n.attrs:
            d[key] = n.attrs[key].split("~")[0].strip()
    if "advances_to" in n.attrs:
        adv = csv(n.attrs["advances_to"])
        d["adv"] = [] if adv == ["null"] else adv
    if n.get("ignore_race_traits") == "yes":
        d["ignore_race_traits"] = 1
    if "abilities_list" in n.attrs:
        d["abilities"] = csv(n.attrs["abilities_list"])
    ab = n.first("abilities")
    if ab is not None:
        d.setdefault("abilities", [])
        for name, _ in ab.macros:
            if name.startswith("ABILITY_"):
                d["abilities"].append(name[len("ABILITY_"):].lower())
        for c in ab.children:  # [heals] id=heals4 ...
            if c.get("id"):
                d["abilities"].append(c.get("id"))
    attacks = []
    for a in n.all("attack"):
        specials = csv(a.get("specials_list"))
        sp = a.first("specials")
        if sp is not None:
            for name, _ in sp.macros:
                if name.startswith("WEAPON_SPECIAL_"):
                    specials.append(name[len("WEAPON_SPECIAL_"):].lower())
            for c in sp.children:
                if c.get("id"):
                    specials.append(c.get("id"))
        attacks.append({"name": a.get("name", ""), "type": a.get("type", "blade"),
                        "range": a.get("range", "melee"), "dmg": to_int(a.get("damage")),
                        "num": to_int(a.get("number")), "spec": specials})
    if attacks:
        d["attacks"] = attacks
    for tag, out in (("resistance", "resist"), ("defense", "defense"), ("movement_costs", "costs")):
        t = n.first(tag)
        if t is not None and t.attrs:
            d[out] = {k: to_int(v) for k, v in t.attrs.items()}
    musthave, extra = [], []
    for name, _ in n.macros:
        if name == "AMLA_DEFAULT":
            d["amla"] = 1
        t = trait_from_macro(name)
        if t:
            (musthave if t[1] else extra).append(t[0])
    if musthave:
        d["musthave"] = musthave
    if extra:
        d["extra_traits"] = extra
    return d


def load_unit_types(unit_dirs, pp_defines_files):
    pp = wml.Preprocessor()
    for f in pp_defines_files:
        pp.load_defines(f)
    types, raw = {}, {}
    files = []
    for d in unit_dirs:
        files += sorted(glob.glob(os.path.join(d, "**", "*.cfg"), recursive=True))
    for f in files:
        root = wml.load(f, pp)
        for n in root.walk():
            if n.tag == "unit_type" and n.get("id"):
                raw[n.get("id")] = n
    # héritage [base_unit] puis variations
    def build(uid, seen=()):
        if uid in types:
            return types[uid]
        n = raw[uid]
        d = {}
        b = n.first("base_unit")
        if b is not None and b.get("id") in raw and b.get("id") not in seen:
            d = json.loads(json.dumps(build(b.get("id"), seen + (uid,))))
        d.update(unit_from_node(n))
        types[uid] = d
        for v in n.all("variation"):
            vid = v.get("variation_id")
            if not vid:
                continue
            vd = json.loads(json.dumps(d)) if v.get("inherit") == "yes" else {}
            vd.update(unit_from_node(v))
            types[uid + ":" + vid] = vd
        return d
    for uid in list(raw):
        build(uid)
    return types


def needed_types(types, tsg_dir):
    """Types référencés quelque part dans la campagne, + lignées complètes."""
    text = ""
    for f in glob.glob(os.path.join(tsg_dir, "**", "*.cfg"), recursive=True):
        text += open(f, encoding="utf-8").read() + "\n"
    ids = [t for t in types if ":" not in t]
    found = set()
    for t in ids:
        if re.search(r"(?<![A-Za-z])" + re.escape(t) + r"(?![A-Za-z])", text):
            found.add(t)
    # fermeture par advances_to (et bases des variations)
    stack = list(found)
    while stack:
        t = stack.pop()
        for a in types.get(t, {}).get("adv", []):
            if a in types and a not in found:
                found.add(a); stack.append(a)
    for t in list(types):
        if ":" in t and t.split(":")[0] in found:
            found.add(t)
    return found


# ---------------------------------------------------------------------------
# Terrain
# ---------------------------------------------------------------------------
def load_terrain(terrain_cfg):
    pp = wml.Preprocessor()
    root = wml.load(terrain_cfg, pp)
    tt = {}
    for n in root.all("terrain_type"):
        s = n.get("string")
        if s:
            tt[s.strip()] = n
    return tt


def alias_expr(tt, code, which, depth=0):
    """Expression {"best"|"worst": [...]} ou id de terrain de base (str)."""
    if depth > 12:
        return "impassable"
    base, _, over = code.partition("^")
    node = tt.get(code)
    bas_code = base
    if node is None and over:
        node = tt.get("^" + over)
    if node is None:
        node = tt.get(base)
        bas_code = base
        if node is None:
            return "impassable"
    key = {"mvt": "mvt_alias", "def": "def_alias"}[which]
    alias = node.get(key) or node.get("aliasof")
    if not alias:
        return node.get("id")
    toks = csv(alias.replace(" ", ""))
    mode, args = "best", []
    for t in toks:
        if t == "-":
            mode = "worst"; continue
        if t == "+":
            mode = "best"; continue
        if t == "_bas":
            if code == bas_code:  # pas de superposition : _bas = rien
                continue
            args.append(alias_expr(tt, bas_code, which, depth + 1))
        else:
            args.append(alias_expr(tt, t, which, depth + 1))
    if not args:
        return node.get("id")
    if len(args) == 1:
        return args[0]
    return {mode: args}


def terrain_flags(tt, code):
    base, _, over = code.partition("^")
    nodes = [tt.get(code)] if tt.get(code) is not None else [tt.get(base), tt.get("^" + over) if over else None]
    f = {}
    for n in nodes:
        if n is None:
            continue
        if n.get("gives_income") == "yes":
            f["village"] = 1
        if n.get("heals"):
            f["heals"] = to_int(n.get("heals"))
        if n.get("recruit_onto") == "yes":
            f["castle"] = 1
        if n.get("recruit_from") == "yes":
            f["keep"] = 1
    names = [n.get("name") for n in nodes if n is not None and n.get("name")]
    if names:
        f["name"] = names[-1]
    return f


def map_codes(path):
    codes = set()
    for line in open(path, encoding="utf-8"):
        for c in line.split(","):
            c = c.strip()
            if not c:
                continue
            if " " in c:  # "1 Re" : position de départ
                c = c.split()[-1]
            codes.add(c)
    return codes


# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--units-cfg", required=True)
    ap.add_argument("--units-dir", required=True)
    ap.add_argument("--terrain-cfg", required=True)
    ap.add_argument("--campaign", required=True, help="dossier The_South_Guard")
    ap.add_argument("--macros", help="dossier data/core/macros (traits réels)")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    real_traits = bool(a.macros) and load_real_traits(a.macros)

    movetypes, global_traits, races = load_core(a.units_cfg)
    utils = sorted(glob.glob(os.path.join(a.campaign, "utils", "*.cfg")))
    types = load_unit_types([a.units_dir, os.path.join(a.campaign, "units")], utils)
    need = needed_types(types, a.campaign)
    out_types = {t: types[t] for t in sorted(need)}
    used_mt = {types[t].get("movetype") for t in need}
    used_races = {types[t].get("race") for t in need}
    units_json = {
        "source": "Battle for Wesnoth (GPL/CC-BY-SA) — généré par gen_data.py",
        "trait_effects_note": ("lus dans data/core/macros/traits.cfg" if real_traits
                               else "valeurs standard Wesnoth saisies (traits.cfg non fourni)"),
        "global_traits": global_traits,
        "trait_effects": TRAIT_EFFECTS,
        "races": {r: races[r] for r in sorted(x for x in used_races if x in races)},
        "movetypes": {m: movetypes[m] for m in sorted(x for x in used_mt if x in movetypes)},
        "types": out_types,
    }
    with open(os.path.join(a.out, "units.json"), "w", encoding="utf-8") as f:
        json.dump(units_json, f, ensure_ascii=False, separators=(",", ":"))

    tt = load_terrain(a.terrain_cfg)
    codes = set()
    for m in glob.glob(os.path.join(a.campaign, "maps", "*.map")):
        codes |= map_codes(m)
    terrain = {}
    for c in sorted(codes):
        e = {"mvt": alias_expr(tt, c, "mvt"), "def": alias_expr(tt, c, "def")}
        e.update(terrain_flags(tt, c))
        terrain[c] = e
    with open(os.path.join(a.out, "terrain.json"), "w", encoding="utf-8") as f:
        json.dump({"codes": terrain}, f, ensure_ascii=False, separators=(",", ":"))

    missing_mt = sorted(x for x in used_mt if x and x not in movetypes)
    print(f"types: {len(out_types)} (sur {len(types)}), movetypes: {len(units_json['movetypes'])}, "
          f"races: {len(units_json['races'])}, terrains: {len(terrain)}")
    if missing_mt:
        print("ATTENTION movetypes inconnus:", missing_mt)


if __name__ == "__main__":
    main()
