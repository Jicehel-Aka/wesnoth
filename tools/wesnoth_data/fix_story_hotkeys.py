#!/usr/bin/env python3
"""Nettoie le texte affiche au joueur dans les JSON generes (campagne ET
scenarios de bataille) : retire les balises de mise en forme Pango/WML
(<span color='...'>, <b>, <i>, ...) laissees telles quelles par le WML
d'origine -- notre police 8x8 ne sait pas les interpreter, donc sans ce
nettoyage elles s'affichent litteralement a l'ecran -- et corrige les
instructions de raccourcis clavier PC (Ctrl+R, Ctrl+Space, Ctrl+J, Alt+R,
touche "u", clic droit...) qui n'ont pas de sens sur l'AKA.

Usage:
    python3 fix_story_hotkeys.py FICHIER.json [FICHIER2.json ...] [-o sortie.json]

Sans -o (ou avec plusieurs fichiers en entree), chaque fichier est corrige
en place (une sauvegarde .bak est ecrite a cote au premier passage).
-o n'est utilisable qu'avec un seul fichier en entree.

Ne nettoie que les cles clairement textuelles (voir TEXT_KEYS) : le reste
de l'arbre JSON (ids, coordonnees, chemins d'image...) n'est jamais touche,
pour eviter de casser un attribut qui contiendrait un '<'/'>' non lie a de
la mise en forme (peu probable mais on ne prend pas le risque).

Hypothese de controle AKA a confirmer avec Jicehel : Recruter / Rappeler /
Objectifs / Fin de tour passent tous par le menu ouvert avec le bouton MENU.
Si ce n'est pas le cas, ajuste REPLACEMENTS ci-dessous (une seule table,
pas de logique eparpillee ailleurs) et relance le script.
"""
import argparse
import json
import shutil
import sys

# Cles dont la valeur est du texte affiche au joueur (a nettoyer). Tout le
# reste de l'arbre (id, x, y, type, image, terrain...) est laisse tel quel.
TEXT_KEYS = {
    "text", "text_fr", "text_en", "message", "reason", "caption",
    "objectives", "note", "label",
}


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


# (motif exact, une fois les balises retirees -> texte de remplacement)
# Les motifs sont des sous-chaines simples (pas de regex) pour rester
# faciles a relire/completer.
REPLACEMENTS = [
    (
        "To end your turn, click the “End Turn” button in the "
        "bottom-right corner of the screen, or press Ctrl+Space.",
        "To end your turn, open the menu (MENU button) and choose "
        "“End Turn”.",
    ),
    (
        "Press Ctrl+R to recruit new soldiers. (Or right-click on a keep hex.)",
        "Move onto your keep, then open the menu (MENU button) and choose "
        "“Recruit” to recruit new soldiers.",
    ),
    (
        "Tip: Press “u” if you ever want to undo a move. Attacks "
        "cannot be undone.",
        "Tip: Press the D button if you ever want to undo a move. Attacks "
        "cannot be undone.",
    ),
    (
        "You may toggle tips on or off at any time during the campaign by "
        "right-clicking anywhere and selecting “Tips: On/Off” in "
        "the menu.",
        "You may toggle tips on or off at any time during the campaign by "
        "opening the menu (MENU button) and selecting “Tips: On/Off”.",
    ),
    (
        "Rain or snow have no impact on gameplay. If weather is distracting "
        "or reduces performance, you can disable it by opening the "
        "Preferences menu (Ctrl-P), by selecting “Display” and "
        "unchecking “Animate Map.”",
        "Rain or snow have no impact on gameplay.",
    ),
    (
        "I will need to recruit units if I want to weather the undead "
        "assault! I should first move to a keep, then press Ctrl+R to "
        "recruit or Alt+R to recall.",
        "I will need to recruit units if I want to weather the undead "
        "assault! I should first move to a keep, then open the menu (MENU "
        "button) to recruit or recall units.",
    ),
    (
        "If you have high-XP survivors from previous battles, remember to "
        "press Alt+R and recall them so they can continue gaining "
        "experience and be promoted to higher levels.",
        "If you have high-XP survivors from previous battles, remember to "
        "open the menu (MENU button) and recall them so they can continue "
        "gaining experience and be promoted to higher levels.",
    ),
    (
        "You are running out of turns to complete this scenario! See your "
        "objectives by pressing Ctrl+J, and remember that the turn limit "
        "can be seen in the top-left-hand corner.",
        "You are running out of turns to complete this scenario! See your "
        "objectives from the menu (MENU button), and remember that the "
        "turn limit can be seen in the top-left-hand corner.",
    ),
    (
        "To see what damage resistances and vulnerabilities a unit has, "
        "right-click it and select “Unit Type Description.”",
        "Every unit’s damage resistances and vulnerabilities are shown "
        "in the info panel at the bottom of the screen.",
    ),
    (
        "For a full description of what one of your units can do, "
        "right-click it and select “Unit Type Description.”",
        "Every unit’s full description is shown in the info panel at "
        "the bottom of the screen.",
    ),
    ("Use “Ctrl+R” to recruit a soldier first!",
     "Open the menu (MENU button) and recruit a soldier first!"),
    ("Press Ctrl+R to recruit new soldiers.",
     "Open the menu (MENU button) and choose “Recruit” to recruit "
     "new soldiers."),
]


def walk(obj, counter, stripped):
    if isinstance(obj, dict):
        for k, v in list(obj.items()):
            if k in TEXT_KEYS and isinstance(v, str):
                cleaned = strip_markup(v)
                if cleaned != v:
                    stripped[0] += 1
                for old, repl in REPLACEMENTS:
                    if old in cleaned:
                        cleaned = cleaned.replace(old, repl)
                        counter[old] = counter.get(old, 0) + 1
                obj[k] = cleaned
            else:
                walk(v, counter, stripped)
    elif isinstance(obj, list):
        for v in obj:
            walk(v, counter, stripped)


def process(path, out_path, counter, stripped):
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    walk(data, counter, stripped)
    dest = out_path or path
    if dest == path:
        shutil.copyfile(path, path + ".bak")
    with open(dest, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("json_paths", nargs="+")
    ap.add_argument("-o", "--output", default=None)
    args = ap.parse_args()
    if args.output and len(args.json_paths) > 1:
        sys.exit("-o n'est utilisable qu'avec un seul fichier en entree")

    counter, stripped = {}, [0]
    for p in args.json_paths:
        process(p, args.output, counter, stripped)

    print(f"{stripped[0]} valeur(s) avec balises retirees ; "
          f"{sum(counter.values())} remplacement(s) de raccourci applique(s) "
          f"sur {len(counter)} motif(s) distinct(s):")
    for old, n in counter.items():
        print(f"  [{n}x] {old[:70]}...")


if __name__ == "__main__":
    sys.exit(main())
