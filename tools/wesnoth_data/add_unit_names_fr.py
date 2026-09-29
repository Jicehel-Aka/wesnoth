#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""add_unit_names_fr.py -- ajoute "name_fr" a chaque type d'unite de
data/units.json, lu par UnitTypeDef::name_fr (wdata.h/.cpp) et affiche par
uname()/type_name() dans battle_scene.cpp et events.cpp quand la langue
courante est le francais (wsg::campaign_state().lang_fr).

Comme pour add_intro_fr.py : la traduction vient EN PRIORITE de la
traduction francaise officielle de Wesnoth (le nom d'un type d'unite se
matche sur son champ "name", pas sur la cle du dictionnaire -- qui est
l'id WML interne, ex. "Cavalier Commander", pas forcement egal au nom
affiche, ex. "Cavalier"). Deux sources sont fusionnees : le domaine
generique po/wesnoth-units/fr.po (noms d'unites de tout Wesnoth) et
po/wesnoth-tsg/fr.po (unites propres a la campagne, ex. "Master Fencer" =
promotion de Sir Gerrick) -- ce dernier prioritaire en cas de collision.

Usage :
    python3 add_unit_names_fr.py --units-json <chemin> \\
        --po po/wesnoth-units_fr.po --po po/wesnoth-tsg_fr.po
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from add_intro_fr import load_po_dict  # noqa: E402  (reutilise le parseur .po maison)

# Types sans correspondance dans les .po officiels (contenu propre a ce
# portage ou trop marginal pour y figurer) : traduits a la main.
MANUAL_FR = {
    "Prince of Wesnoth": "Prince de Wesnoth",
}


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--units-json", required=True)
    ap.add_argument("--po", action="append", required=True,
                     help="Fichier(s) .po ; repetable, le dernier gagne en cas de collision")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    fr_dict = {}
    for po in args.po:
        d = load_po_dict(Path(po))
        print(f"{len(d)} chaines chargees depuis {po}", file=sys.stderr)
        fr_dict.update(d)

    path = Path(args.units_json)
    data = json.loads(path.read_text(encoding="utf-8"))
    types = data.get("types", {})

    n_done = n_manual = n_skip = n_missing = 0
    missing = []
    for tid, t in types.items():
        if t.get("name_fr") and not args.force:
            n_skip += 1
            continue
        name_en = t.get("name", tid)
        fr = fr_dict.get(name_en) or MANUAL_FR.get(name_en)
        if fr:
            t["name_fr"] = fr
            n_done += 1
            if name_en in MANUAL_FR and name_en not in fr_dict:
                n_manual += 1
        else:
            n_missing += 1
            missing.append((tid, name_en))

    if not args.dry_run:
        path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"écrit : {path}", file=sys.stderr)

    print(f"\n{n_done} name_fr ajoutés ({n_manual} manuelles, {n_done - n_manual} "
          f"depuis les .po), {n_skip} déjà présents, {n_missing} introuvables.",
          file=sys.stderr)
    for tid, nm in missing:
        print(f"  introuvable : {tid} -> {nm!r}", file=sys.stderr)


if __name__ == "__main__":
    main()
