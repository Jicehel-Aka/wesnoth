#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""add_intro_fr.py -- ajoute le champ "message_fr" a chaque entree de
"intro_messages" dans les data/scenarios/*.json (messages d'ouverture de
bataille, doubles par generate_audio.py --battle-scenarios-dir).

La traduction vient EN PRIORITE de la traduction francaise officielle de
Wesnoth (po/wesnoth-tsg/fr.po pour South Guard, po/wesnoth-tb/fr.po pour
Two Brothers) : on matche le texte anglais "message" tel quel contre les
msgid du .po et on recupere le msgstr correspondant -- c'est la fraicheur
et la fidelite aux donnees reelles de Wesnoth qui priment, pas une
traduction maison. Le reste (repliques adaptees/inventees pour le tutoriel
Gamebuino : bouton D, bouton MENU, etc., absentes du jeu original donc pas
dans le .po) est couvert par MANUAL_FR ci-dessous, traduit a la main en
gardant le ton du personnage (ex: Mari, directe et familiere, telle que
rendue dans le .po officiel).

Usage :
    python3 add_intro_fr.py --scenarios-dir <dir> --po <fichier .po> [--dry-run]

Exemple (depuis ce dossier) :
    python3 add_intro_fr.py \\
        --scenarios-dir /chemin/vers/SD_files/WESNOTH_SG/data/scenarios \\
        --po po/wesnoth-tsg_fr.po
    python3 add_intro_fr.py \\
        --scenarios-dir /chemin/vers/SD_files/WESNOTH_TB/data/scenarios \\
        --po po/wesnoth-tb_fr.po

Idempotent : une entree qui a deja un "message_fr" non vide n'est pas
retouchee (sauf --force), donc on peut relancer sans rien casser.

Ne depend d'aucun paquet externe (pas de "pip install") : le format .po
utilise ici (fichiers officiels Wesnoth, sans pluriels) est assez simple
pour etre lu par un petit parseur maison, cf. load_po_dict() ci-dessous.
"""
import argparse
import json
import re
import sys
from pathlib import Path

# Repliques inventees/adaptees pour le tutoriel Gamebuino, absentes du jeu
# Wesnoth original (donc absentes de po/wesnoth-tsg/fr.po) : traduites a la
# main. Cle = texte anglais exact tel qu'ecrit dans data/scenarios/*.json.
MANUAL_FR = {
    "Well, orders are orders. Come on over here, Deoran — let’s get a closer "
    "look at you. On my way south I picked up an old training quintain — "
    "let’s see what you’re made of before I trust you with real soldiers.":
        "Enfin bon, les ordres sont les ordres. Viens donc par là, Deoran, "
        "que je puisse te voir de plus près. En descendant vers le sud, "
        "j’ai récupéré une vieille quintaine d’entraînement — voyons un peu "
        "ce que tu as dans le ventre avant que je te confie de vrais "
        "soldats.",
    "Move Deoran next to the quintain and attack it.":
        "Déplace Deoran à côté de la quintaine et attaque-la.",
    "Watch the percentage shown on the ground — that’s your chance to hit.":
        "Regarde le pourcentage affiché au sol : c’est ta chance de "
        "toucher.",
    "Tip: press the D button any time if you want to undo a move; attacks "
    "themselves cannot be undone.":
        "Astuce : appuie sur le bouton D à tout moment pour annuler un "
        "déplacement ; les attaques, elles, ne peuvent pas être annulées.",
    "Careful — it’s a magical quintain, on loan from the wizards’ academy "
    "at Alduin. It strikes back twice per attack, so don’t be surprised if "
    "it stings.":
        "Attention : c’est une quintaine magique, prêtée par l’académie "
        "des mages d’Alduin. Elle riposte deux fois par attaque, alors ne "
        "sois pas surpris si ça pique.",
    "When you’re done acting, open the menu (MENU button) and choose "
    "“End Turn.”":
        "Une fois que tu as fini de jouer, ouvre le menu (bouton MENU) et "
        "choisis « Fin du tour ».",
    "Not bad at all for a first fight. Now let’s get you to Westin — we’re "
    "not far now.":
        "Pas mal du tout pour un premier combat. Direction Westin, "
        "maintenant — on n’est plus très loin.",
}


def _unescape_po_string(s: str) -> str:
    """Decode le contenu entre guillemets d'une ligne .po (echappements
    gettext standard : \\", \\\\, \\n, \\t)."""
    out = []
    i = 0
    while i < len(s):
        c = s[i]
        if c == "\\" and i + 1 < len(s):
            nxt = s[i + 1]
            if nxt == "n":
                out.append("\n")
            elif nxt == "t":
                out.append("\t")
            elif nxt in ('"', "\\"):
                out.append(nxt)
            else:
                out.append(nxt)
            i += 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


_PO_STRING_RE = re.compile(r'^\s*"(.*)"\s*$')


def load_po_dict(po_path: Path) -> dict:
    """Parseur .po minimal (pas de dependance externe) : associe chaque
    msgid a son msgstr. Gere les chaines multi-lignes et ignore les
    entrees obsoletes (prefixees "#~") et l'en-tete (msgid ""). Suffisant
    pour les fichiers .po officiels de Wesnoth utilises ici (pas de
    pluriels msgid_plural)."""
    text = po_path.read_text(encoding="utf-8")
    result = {}
    cur_id_lines = None   # None = pas dans un bloc msgid
    cur_str_lines = None
    in_id = in_str = False

    def flush():
        if cur_id_lines is not None and cur_str_lines is not None:
            msgid = "".join(cur_id_lines)
            msgstr = "".join(cur_str_lines)
            if msgid and msgstr:
                result[msgid] = msgstr

    for raw_line in text.split("\n"):
        line = raw_line.rstrip("\r")
        stripped = line.strip()
        if stripped.startswith("#~"):
            # entree obsolete : on clot le bloc courant et on ignore la ligne
            continue
        if stripped.startswith("msgid "):
            flush()
            cur_id_lines = [_unescape_po_string(_PO_STRING_RE.match(stripped[6:]).group(1))]
            cur_str_lines = None
            in_id, in_str = True, False
            continue
        if stripped.startswith("msgstr "):
            cur_str_lines = [_unescape_po_string(_PO_STRING_RE.match(stripped[7:]).group(1))]
            in_id, in_str = False, True
            continue
        m = _PO_STRING_RE.match(stripped)
        if m:
            piece = _unescape_po_string(m.group(1))
            if in_str and cur_str_lines is not None:
                cur_str_lines.append(piece)
            elif in_id and cur_id_lines is not None:
                cur_id_lines.append(piece)
            continue
        # ligne vide, commentaire (#), msgctxt, etc. : ferme le bloc courant
        if stripped == "" or stripped.startswith("#"):
            flush()
            cur_id_lines = cur_str_lines = None
            in_id = in_str = False

    flush()
    # l'en-tete (msgid "") produit une entree cle="" -> pas gênant, on la retire
    result.pop("", None)
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--scenarios-dir", required=True,
                     help="Dossier data/scenarios a completer (in-place)")
    ap.add_argument("--po", required=True,
                     help="Fichier .po francais officiel (wesnoth-tsg ou wesnoth-tb)")
    ap.add_argument("--force", action="store_true",
                     help="Ecrase aussi les message_fr deja presents")
    ap.add_argument("--dry-run", action="store_true",
                     help="N'ecrit rien, affiche juste ce qui serait fait")
    args = ap.parse_args()

    fr_dict = load_po_dict(Path(args.po))
    print(f"{len(fr_dict)} chaines chargees depuis {args.po}", file=sys.stderr)

    n_done = n_manual = n_skip = n_missing = 0
    missing_lines = []

    for path in sorted(Path(args.scenarios_dir).glob("*.json")):
        d = json.loads(path.read_text(encoding="utf-8"))
        msgs = d.get("intro_messages", [])
        if not msgs:
            continue
        changed = False
        for idx, msg in enumerate(msgs):
            text_en = (msg.get("message") or "").strip()
            if not text_en:
                continue
            if msg.get("message_fr") and not args.force:
                n_skip += 1
                continue
            fr = fr_dict.get(text_en) or MANUAL_FR.get(text_en)
            if fr:
                msg["message_fr"] = fr
                changed = True
                n_done += 1
                if text_en in MANUAL_FR and text_en not in fr_dict:
                    n_manual += 1
            else:
                n_missing += 1
                missing_lines.append((path.name, idx, msg.get("speaker"), text_en))
        if changed and not args.dry_run:
            path.write_text(json.dumps(d, ensure_ascii=False, indent=2) + "\n",
                             encoding="utf-8")
            print(f"  écrit : {path.name}")
        elif changed:
            print(f"  (dry-run) modifierait : {path.name}")

    print(f"\n{n_done} message_fr ajoutés ({n_manual} traductions manuelles, "
          f"{n_done - n_manual} depuis le .po officiel), {n_skip} déjà présents, "
          f"{n_missing} introuvables.", file=sys.stderr)
    if missing_lines:
        print("\nIntrouvables (à traduire à la main, ajouter à MANUAL_FR) :",
              file=sys.stderr)
        for fn, idx, sp, t in missing_lines:
            print(f"  {fn} [{idx}] {sp}: {t!r}", file=sys.stderr)


if __name__ == "__main__":
    main()
