#!/usr/bin/env python3
"""Nettoie le texte affiche au joueur dans les JSON generes (campagne ET
scenarios de bataille) : retire les balises de mise en forme Pango/WML
(<span color='...'>, <b>, <i>, ...) laissees telles quelles par le WML
d'origine -- notre police 8x8 ne sait pas les interpreter, donc sans ce
nettoyage elles s'affichent litteralement a l'ecran -- et applique un
fichier de VERROUS TEXTE (text_overrides.json) : des remplacements
exacte-sous-chaine -> texte final, qui remplacent les instructions de
raccourcis clavier PC (Ctrl+R, Ctrl+Space...) sans sens sur l'AKA, ou
toute autre reecriture manuelle qu'on veut voir survivre a une prochaine
regeneration complete des donnees.

Usage:
    python3 fix_story_hotkeys.py FICHIER.json [FICHIER2.json ...] [-o sortie.json]
                                  [--overrides text_overrides.json]

Sans -o (ou avec plusieurs fichiers en entree), chaque fichier est corrige
en place (une sauvegarde .bak est ecrite a cote au premier passage).
-o n'est utilisable qu'avec un seul fichier en entree.

Ne nettoie que les cles clairement textuelles (voir TEXT_KEYS) : le reste
de l'arbre JSON (ids, coordonnees, chemins d'image...) n'est jamais touche,
pour eviter de casser un attribut qui contiendrait un '<'/'>' non lie a de
la mise en forme (peu probable mais on ne prend pas le risque).

--- Verrouiller un texte pour de bon ---
Quand un texte a ete corrige/reecrit a la main (traduction, reformulation,
suppression d'un passage qui ne colle plus a notre moteur...) et qu'on ne
veut pas avoir a refaire ce travail a chaque fois qu'on regenere les
donnees depuis les sources Wesnoth (nouvelle campagne, mise a jour amont,
etc.), on l'ajoute a text_overrides.json plutot que de l'editer une seule
fois dans le JSON genere :

    {"old": "<texte original extrait tel quel du WML>",
     "new": "<texte final qu'on veut voir affiche>"}

Ce script est deja le dernier maillon de la chaine de generation (relance
apres extract_scenario.py) : tant que le "old" apparait mot pour mot dans
un texte genere, le verrou s'applique automatiquement, quelle que soit la
scene/le fichier/l'index concerne. Si le WML amont change ce texte (nouvelle
version de la campagne, correction upstream...), le "old" ne matchera plus
nulle part -- le script le signale explicitement en fin d'execution
("verrou(s) OBSOLETE(S)") pour qu'on aille verifier/reecrire l'entree au
lieu de laisser le verrou disparaitre silencieusement.

Hypothese de controle AKA a confirmer avec Jicehel : Recruter / Rappeler /
Objectifs / Fin de tour passent tous par le menu ouvert avec le bouton MENU.
Si ce n'est pas le cas, ajuste text_overrides.json (pas de logique
eparpillee ailleurs) et relance le script.
"""
import argparse
import json
import os
import shutil
import sys

# Cles dont la valeur est du texte affiche au joueur (a nettoyer). Tout le
# reste de l'arbre (id, x, y, type, image, terrain...) est laisse tel quel.
TEXT_KEYS = {
    "text", "text_fr", "text_en", "message", "reason", "caption",
    "objectives", "note", "label",
}

DEFAULT_OVERRIDES_PATH = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "text_overrides.json"
)


def strip_markup(s: str) -> str:
    out = []
    in_tag = False
    for ch in s:
        if ch == "<":
            in_tag = True
            continue
        if ch == ">":
            in_tag = False
            continue
        if not in_tag:
            out.append(ch)
    s = "".join(out)
    for frm, to in (("&amp;", "&"), ("&lt;", "<"), ("&gt;", ">"),
                    ("&quot;", "\""), ("&apos;", "'")):
        s = s.replace(frm, to)
    return s


def load_overrides(path: str):
    """Charge la liste de verrous texte depuis un fichier JSON.

    Format : liste d'objets {"old": ..., "new": ...} (un "_comment" optionnel
    est ignore). Absence de fichier -> liste vide (pas une erreur : le
    nettoyage de balises reste utile seul).
    """
    if not path or not os.path.isfile(path):
        return []
    with open(path, encoding="utf-8") as f:
        raw = json.load(f)
    overrides = []
    for entry in raw:
        old, new = entry.get("old"), entry.get("new")
        if old is None or new is None:
            continue
        overrides.append((old, new))
    return overrides


def walk(obj, overrides, counter, stripped):
    if isinstance(obj, dict):
        for k, v in list(obj.items()):
            if k in TEXT_KEYS and isinstance(v, str):
                cleaned = strip_markup(v)
                if cleaned != v:
                    stripped[0] += 1
                for old, repl in overrides:
                    if old in cleaned:
                        cleaned = cleaned.replace(old, repl)
                        counter[old] = counter.get(old, 0) + 1
                obj[k] = cleaned
            else:
                walk(v, overrides, counter, stripped)
    elif isinstance(obj, list):
        for v in obj:
            walk(v, overrides, counter, stripped)


def process(path, out_path, overrides, counter, stripped):
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    walk(data, overrides, counter, stripped)
    dest = out_path or path
    if dest == path:
        shutil.copyfile(path, path + ".bak")
    with open(dest, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("json_paths", nargs="+")
    ap.add_argument("-o", "--output", default=None)
    ap.add_argument("--overrides", default=DEFAULT_OVERRIDES_PATH,
                     help="Fichier JSON de verrous texte "
                          "(defaut : text_overrides.json a cote du script)")
    args = ap.parse_args()
    if args.output and len(args.json_paths) > 1:
        sys.exit("-o n'est utilisable qu'avec un seul fichier en entree")

    overrides = load_overrides(args.overrides)

    counter, stripped = {}, [0]
    for p in args.json_paths:
        process(p, args.output, overrides, counter, stripped)

    print(f"{stripped[0]} valeur(s) avec balises retirees ; "
          f"{sum(counter.values())} verrou(s) texte applique(s) "
          f"sur {len(counter)}/{len(overrides)} motif(s) connus:")
    for old, n in counter.items():
        print(f"  [{n}x] {old[:70]}...")

    stale = [old for old, _ in overrides if old not in counter]
    if stale:
        print(f"\n/!\\ {len(stale)} verrou(s) OBSOLETE(S) (plus trouve(s) dans "
              f"les fichiers traites -- le texte source a peut-etre change, "
              f"a verifier dans {os.path.basename(args.overrides)}):")
        for old in stale:
            print(f"  - {old[:70]}...")


if __name__ == "__main__":
    sys.exit(main())
