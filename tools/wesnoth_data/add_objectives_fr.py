#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""add_objectives_fr.py -- ajoute la traduction francaise du menu
"Objectifs" (Ctrl+J) a un fichier scenario data/scenarios/<...>.json.

Deux champs sont peuples, tous deux lus par le moteur quand
wsg::campaign_state().lang_fr est vrai (repli silencieux sur l'anglais si
absent) :
  - "text_fr" sur chaque entree du tableau statique "objectives" (lu par
    game.cpp au chargement du scenario) ;
  - "description_fr" sur chaque noeud WML [objective] a l'interieur de
    l'arbre "events" (lu par events.cpp, action [objectives] dynamique).

Comme pour add_intro_fr.py / add_unit_names_fr.py : la traduction vient EN
PRIORITE des .po officiels de Wesnoth. La plupart des objectifs ("Turns run
out", "Death of Deoran", etc.) sont des chaines generiques du moteur
(domaine po/wesnoth/fr.po, macros data/core/macros/objective-utils.cfg),
pas du texte propre a la campagne -- d'ou l'ajout de wesnoth-base_fr.po
(extrait de po/wesnoth/fr.po) aux sources fusionnees. Le domaine
wesnoth-units/fr.po est inclus car certains objectifs citent un nom d'unite
("Death of Sir Gerrick"). Les domaines de campagne (wesnoth-tsg/wesnoth-tb)
restent prioritaires en cas de collision. Ce qui ne matche toujours pas
(contenu invente pour ce portage, ex. le tutoriel Gamebuino) est traduit a
la main via MANUAL_FR.

Usage -- tout le dossier de scenarios d'un coup (comme add_intro_fr.py) :
    python3 add_objectives_fr.py --scenarios-dir data/scenarios \\
        --po po/wesnoth-base_fr.po --po po/wesnoth-units_fr.po \\
        --po po/wesnoth-tsg_fr.po

--scenario <fichier> reste accepte pour ne traiter qu'un seul fichier.
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from add_intro_fr import load_po_dict  # noqa: E402  (reutilise le parseur .po maison)

# Objectifs propres a ce portage (contenu Gamebuino invente, absent des .po
# officiels de Wesnoth) : traduits a la main.
MANUAL_FR = {
    "Defeat the training quintain": "Vaincre la quintaine d'entraînement",
}


def _translate_objectives_array(objectives, fr_dict, manual, force, stats, missing):
    for o in objectives:
        if o.get("text_fr") and not force:
            stats["skip"] += 1
            continue
        text_en = (o.get("text") or "").strip()
        if not text_en:
            continue
        fr = fr_dict.get(text_en) or manual.get(text_en)
        if fr:
            o["text_fr"] = fr
            stats["done"] += 1
            if text_en in manual and text_en not in fr_dict:
                stats["manual"] += 1
        else:
            stats["missing"] += 1
            missing.append(text_en)


def _walk_events_objectives(node, fr_dict, manual, force, stats, missing):
    if isinstance(node, dict):
        if node.get("t") == "objectives":
            for k in node.get("c", []):
                if k.get("t") != "objective":
                    continue
                a = k.setdefault("a", {})
                if a.get("description_fr") and not force:
                    stats["skip"] += 1
                    continue
                desc_en = (a.get("description") or "").strip()
                if not desc_en:
                    continue
                fr = fr_dict.get(desc_en) or manual.get(desc_en)
                if fr:
                    a["description_fr"] = fr
                    stats["done"] += 1
                    if desc_en in manual and desc_en not in fr_dict:
                        stats["manual"] += 1
                else:
                    stats["missing"] += 1
                    missing.append(desc_en)
        for v in node.values():
            _walk_events_objectives(v, fr_dict, manual, force, stats, missing)
    elif isinstance(node, list):
        for v in node:
            _walk_events_objectives(v, fr_dict, manual, force, stats, missing)


def _process_file(path, fr_dict, force, dry_run, stats, missing):
    data = json.loads(path.read_text(encoding="utf-8"))
    before = dict(stats)  # pour savoir si CE fichier a ete modifie (log ci-dessous)

    _translate_objectives_array(data.get("objectives", []), fr_dict, MANUAL_FR,
                                 force, stats, missing)
    _walk_events_objectives(data.get("events", []), fr_dict, MANUAL_FR,
                             force, stats, missing)

    changed = stats["done"] != before["done"]
    if changed and not dry_run:
        path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"  écrit : {path.name}", file=sys.stderr)
    elif changed:
        print(f"  (dry-run) modifierait : {path.name}", file=sys.stderr)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    grp = ap.add_mutually_exclusive_group(required=True)
    grp.add_argument("--scenarios-dir",
                      help="Dossier data/scenarios a traiter en entier (in-place)")
    grp.add_argument("--scenario",
                      help="Un seul fichier data/scenarios/<...>.json a traiter")
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

    stats = {"done": 0, "manual": 0, "skip": 0, "missing": 0}
    missing = []

    if args.scenarios_dir:
        for path in sorted(Path(args.scenarios_dir).glob("*.json")):
            _process_file(path, fr_dict, args.force, args.dry_run, stats, missing)
    else:
        _process_file(Path(args.scenario), fr_dict, args.force, args.dry_run, stats, missing)

    print(f"\n{stats['done']} text_fr/description_fr ajoutés ({stats['manual']} manuelles, "
          f"{stats['done'] - stats['manual']} depuis les .po), {stats['skip']} déjà présents, "
          f"{stats['missing']} introuvables.", file=sys.stderr)
    for m in missing:
        print(f"  introuvable : {m!r}", file=sys.stderr)


if __name__ == "__main__":
    main()
