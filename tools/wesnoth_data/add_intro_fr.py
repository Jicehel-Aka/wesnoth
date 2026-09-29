#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""add_intro_fr.py -- ajoute le champ "message_fr" a chaque message affiche
en bataille : les "intro_messages" (sequence jouee a l'ouverture d'un
scenario) ET, recursivement, les actions {"t": "message", ...} enfouies
dans l'arbre "events" (dialogues dynamiques : morts, tutoriel, avancees de
niveau, etc. -- voir EventEngine::queue_message() dans events.cpp, qui lit
"message_fr" exactement comme pour intro_messages).

La traduction vient EN PRIORITE de la traduction francaise officielle de
Wesnoth (po/wesnoth-tsg/fr.po pour South Guard, po/wesnoth-tb/fr.po pour
Two Brothers) : on matche le texte anglais "message" tel quel contre les
msgid du .po et on recupere le msgstr correspondant -- c'est la fraicheur
et la fidelite aux donnees reelles de Wesnoth qui priment, pas une
traduction maison. Le reste (repliques adaptees/inventees pour le
tutoriel Gamebuino : bouton D, bouton MENU, etc., absentes du jeu
original donc pas dans le .po) est couvert par MANUAL_FR ci-dessous,
traduit a la main en gardant le ton du personnage.

Corrige aussi au passage une corruption de donnees issue de la conversion
WML -> JSON d'origine : {TUTOR_COLOR} (macro Wesnoth definissant une
couleur d'astuce, #D563D7) a ete mal extraite et a laisse le texte brut du
preprocesseur WML "<<#D563D7>>" tronque en "    <<" dans l'attribut
color= des balises <span> -- voir _initial.cfg:370 du jeu original. Sans
gravite pour l'affichage actuel (strip_markup() avale toute la balise, y
compris le "<<" errant, donc rien ne fuite a l'ecran), mais on corrige
quand meme pour rester fidele aux donnees reelles.

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

# Repliques du tutoriel Gamebuino (intro_messages), inventees/adaptees pour
# ce portage et donc absentes du jeu Wesnoth original (absentes de
# po/wesnoth-tsg/fr.po) : traduites a la main. Cle = texte anglais exact tel
# qu'ecrit dans data/scenarios/*.json.
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

    # --- Repliques dynamiques (events) propres au tutoriel Gamebuino ---
    "Tip: Press the D button if you ever want to undo a move. Attacks "
    "cannot be undone.":
        "Astuce : appuie sur le bouton D à tout moment pour annuler un "
        "déplacement. Les attaques, elles, ne peuvent pas être annulées.",
    "(Bows) A pleasure to make your acquaintance, Captain.":
        "(s’incline) Un plaisir de faire votre connaissance, Capitaine.",
    "Wondering about those percentages that appear on the ground when you "
    "move a unit? That’s your soldiers’ terrain defense — their chance to "
    "evade enemy attacks.":
        "Tu te demandes ce que sont ces pourcentages qui apparaissent au "
        "sol quand tu déplaces une unité ? C’est la défense de terrain de "
        "tes soldats : leur chance d’esquiver les attaques ennemies.",
    "Nice try, but you already used up your chance to attack. Now it’s the "
    "quintain’s turn to act.":
        "Belle tentative, mais tu as déjà utilisé ta chance d’attaquer. "
        "C’est maintenant au tour de la quintaine d’agir.",
    "The dummy gets a turn?":
        "Le mannequin a droit à un tour ?",
    "To end your turn, open the menu (MENU button) and choose “End Turn”.":
        "Pour terminer ton tour, ouvre le menu (bouton MENU) et choisis "
        "« Fin du tour ».",
    "Starting your turn in a village will heal you for 8 hitpoints. End "
    "your turn now.":
        "Commencer ton tour dans un village te soigne de 8 points de vie. "
        "Termine ton tour maintenant.",
    "The golden crown above Deoran’s HP bar indicates that he’s a leader: "
    "he can recruit new units when standing on a castle’s keep hex.\n\nIn "
    "most scenarios, you will lose if your leader is killed. Use Deoran "
    "enough to build his experience, but not so aggressively that he’s at "
    "risk of dying!":
        "La couronne dorée au-dessus de la barre de vie de Deoran indique "
        "que c’est un chef : il peut recruter de nouvelles unités "
        "lorsqu’il se trouve sur le donjon d’un château.\n\nDans la "
        "plupart des scénarios, tu perds si ton chef est tué. Utilise "
        "Deoran pour qu’il gagne de l’expérience, mais pas trop "
        "imprudemment, sous peine de le voir mourir !",
    "Move onto your keep, then open the menu (MENU button) and choose "
    "“Recruit” to recruit new soldiers.":
        "Déplace-toi sur ton donjon, puis ouvre le menu (bouton MENU) et "
        "choisis « Recruter » pour recruter de nouveaux soldats.",
    "One of your units has reached max XP, and has advanced a level! It is "
    "now significantly more powerful, and has been fully healed!\n\nEvery "
    "unit’s full description is shown in the info panel at the bottom of "
    "the screen.":
        "Une de tes unités a atteint son XP maximum et a gagné un "
        "niveau ! Elle est maintenant bien plus puissante, et a été "
        "entièrement soignée !\n\nLa description complète de chaque unité "
        "est affichée dans le panneau d’information en bas de l’écran.",
    "Careful, Deoran. Unless you have overwhelming numbers, you may want "
    "to hunker down and hold a defensive position until dawn. Castles and "
    "especially villages (because they heal) make for good defensive "
    "positions.\n\n Capturing villages will also increase your income, "
    "which you can use to recruit more soldiers.":
        "Prudence, Deoran. À moins d’être en nette supériorité numérique, "
        "mieux vaut te retrancher et tenir une position défensive "
        "jusqu’à l’aube. Les châteaux, et surtout les villages (parce "
        "qu’ils soignent), sont de bonnes positions défensives.\n\n "
        "Capturer des villages augmente aussi tes revenus, que tu peux "
        "utiliser pour recruter plus de soldats.",
    "I just made a ranged attack! I had to stand right next to this "
    "Quintain (yes, this is different from how most games handle ranged "
    "attacks), but because the dummy has no ranged weapon of its own it "
    "was unable to retaliate.":
        "Je viens de faire une attaque à distance ! J’ai dû me tenir "
        "juste à côté de cette quintaine (oui, c’est différent de la "
        "plupart des jeux pour les attaques à distance), mais comme le "
        "mannequin n’a pas d’arme à distance, il n’a pas pu riposter.",
    "I just made a ranged attack! I had to stand right next to this "
    "$second_unit.language_name (yes, this is different from how most "
    "games handle ranged attacks), but because the $second_unit."
    "language_name has no ranged weapon of its own it was unable to "
    "retaliate.":
        "Je viens de faire une attaque à distance ! J’ai dû me tenir "
        "juste à côté de ce $second_unit.language_name (oui, c’est "
        "différent de la plupart des jeux pour les attaques à distance), "
        "mais comme le $second_unit.language_name n’a pas d’arme à "
        "distance, il n’a pas pu riposter.",
    "I just made a ranged attack! I had to stand right next to this "
    "$second_unit.language_name (yes, this is different from how most "
    "games handle ranged attacks), but because the $second_unit."
    "language_name has no ranged weapon of his own he was unable to "
    "retaliate.":
        "Je viens de faire une attaque à distance ! J’ai dû me tenir "
        "juste à côté de ce $second_unit.language_name (oui, c’est "
        "différent de la plupart des jeux pour les attaques à distance), "
        "mais comme le $second_unit.language_name n’a pas d’arme à "
        "distance à lui, il n’a pas pu riposter.",
    "Every unit’s damage resistances and vulnerabilities are shown in the "
    "info panel at the bottom of the screen.":
        "Les résistances et vulnérabilités aux dégâts de chaque unité "
        "sont affichées dans le panneau d’information en bas de l’écran.",
    "Oh, now you want my help? I thought you were too clever to listen to "
    "my tutorial.":
        "Oh, tu veux mon aide maintenant ? Je pensais que tu étais trop "
        "malin pour écouter mon tutoriel.",
    "You may toggle tips on or off at any time during the campaign by "
    "opening the menu (MENU button) and selecting “Tips: On/Off”.":
        "Tu peux activer ou désactiver les astuces à tout moment pendant "
        "la campagne en ouvrant le menu (bouton MENU) et en sélectionnant "
        "« Astuces : Activées/Désactivées ».",
    "At your service, Commander Deoran! You’ll need every spear you can "
    "get before we reach Westin.":
        "À votre service, Commandant Deoran ! Vous aurez besoin de toutes "
        "les lances possibles avant d’atteindre Westin.",
    "Move onto your keep, then open the menu (MENU button) to recruit "
    "soldiers whenever you have the gold for it.":
        "Déplace-toi sur ton donjon, puis ouvre le menu (bouton MENU) "
        "pour recruter des soldats dès que tu as l’or nécessaire.",
    "Hooray! The dummy is destroyed, and you’ve gained valuable combat "
    "experience. Not bad at all — now let’s see how you fare against "
    "something that can actually hit back.":
        "Hourra ! Le mannequin est détruit, et tu as gagné une précieuse "
        "expérience de combat. Pas mal du tout — voyons maintenant "
        "comment tu te débrouilles contre quelque chose qui peut "
        "vraiment riposter.",
    "Good, we’ve captured a village! The little flag waving from it will "
    "serve as a reminder that it’s under our control. It will remain that "
    "way until an enemy moves into it.\n\n Each village we control "
    "provides upkeep and generates gold, which I can use to recruit new "
    "soldiers.":
        "Bien, nous avons capturé un village ! Le petit drapeau qui y "
        "flotte nous rappellera qu’il est sous notre contrôle. Il le "
        "restera tant qu’aucun ennemi n’y pénètre.\n\n Chaque village que "
        "nous contrôlons couvre l’entretien et génère de l’or, que je "
        "peux utiliser pour recruter de nouveaux soldats.",

    # --- Astuces avec balise <span color='...'> corrompue (voir docstring) ---
    # Cles au format POST-correction (_fix_tutor_color a deja tourne sur
    # "message" avant la recherche dans ce dictionnaire), donc balises
    # <span color='#D563D7'>...</span> incluses des deux cotes.
    "<span color='#D563D7'>One of your high-experience units has died! "
    "Take care to protect your experienced soldiers, so that they can "
    "eventually advance to powerful Level 2 veterans.</span>":
        "<span color='#D563D7'>Une de tes unités expérimentées est "
        "morte ! Prends soin de protéger tes soldats expérimentés, afin "
        "qu’ils puissent un jour devenir de puissants vétérans de "
        "niveau 2.</span>",
    "<span color='#D563D7'>One of your high-level veterans has died! "
    "Take care to protect your promoted soldiers, so that you can recall "
    "them in future battles.</span>":
        "<span color='#D563D7'>Un de tes vétérans de haut niveau est "
        "mort ! Prends soin de protéger tes soldats promus, afin de "
        "pouvoir les rappeler lors de batailles futures.</span>",
    "<span color='#D563D7'>One of your units has reached max XP, and has "
    "advanced a level! It is now significantly more powerful, and has "
    "been fully healed!\n\nFor a full description of what one of your "
    "units can do, <b><i>right-click it and select “Unit Type "
    "Description.”</i></b></span>":
        "<span color='#D563D7'>Une de tes unités a atteint son XP "
        "maximum et a gagné un niveau ! Elle est maintenant bien plus "
        "puissante, et a été entièrement soignée !\n\nPour voir la "
        "description complète de ce qu’une de tes unités peut faire, "
        "<b><i>fais un clic droit dessus et sélectionne « Description "
        "du type d’unité ».</i></b></span>",
    "<span color='#D563D7'>To see what damage resistances and "
    "vulnerabilities a unit has, right-click it and select “Unit Type "
    "Description.”</span>":
        "<span color='#D563D7'>Pour voir les résistances et "
        "vulnérabilités aux dégâts d’une unité, fais un clic droit "
        "dessus et sélectionne « Description du type d’unité ».</span>",
    "<span color='#D563D7'>Mari has unusually high terrain defenses (her "
    "chance to evade enemy attacks). Her <i><b>skirmisher</b></i> "
    "ability lets her ignore enemies’ Zone of Control — she can move "
    "freely around enemy units without being stopped.</span>":
        "<span color='#D563D7'>Mari a une défense de terrain "
        "inhabituellement élevée (sa chance d’esquiver les attaques "
        "ennemies). Sa capacité <i><b>tirailleur</b></i> lui permet "
        "d’ignorer la zone de contrôle des ennemis — elle peut se "
        "déplacer librement autour des unités ennemies sans être "
        "arrêtée.</span>",
    "<span color='#D563D7'>Slowed enemies move at <i><b>half speed</b>"
    "</i> and deal <i><b>half damage</b></i>.</span>":
        "<span color='#D563D7'>Les ennemis ralentis se déplacent à "
        "<i><b>vitesse réduite de moitié</b></i> et infligent <i><b>"
        "deux fois moins de dégâts</b></i>.</span>",
    "<span color='#D563D7'>If you have high-XP survivors from previous "
    "battles, remember to press <b>Alt+R</b> and recall them so they can "
    "continue gaining experience and be promoted to higher levels.</span>":
        "<span color='#D563D7'>Si tu as des survivants avec beaucoup "
        "d’XP de batailles précédentes, pense à appuyer sur <b>Alt+R</b> "
        "pour les rappeler, afin qu’ils continuent à gagner de "
        "l’expérience et à être promus à des niveaux supérieurs.</span>",
    "<span color='#D563D7'><i>Rain or snow have no impact on gameplay. "
    "If weather is distracting or reduces performance, you can disable "
    "it by opening the Preferences menu (Ctrl-P), by selecting "
    "“Display” and unchecking “Animate Map.”</i></span>":
        "<span color='#D563D7'><i>La pluie ou la neige n’ont aucun "
        "impact sur le jeu. Si la météo te distrait ou réduit les "
        "performances, tu peux la désactiver en ouvrant le menu "
        "Préférences (Ctrl-P), en sélectionnant « Affichage » et en "
        "décochant « Animer la carte ».</i></span>",
    "<span color='#D563D7'>You are running out of turns to complete "
    "this scenario! See your objectives by pressing Ctrl+J, and "
    "remember that the turn limit can be seen in the top-left-hand "
    "corner.</span>":
        "<span color='#D563D7'>Il te reste peu de tours pour terminer "
        "ce scénario ! Consulte tes objectifs en appuyant sur Ctrl+J, "
        "et n’oublie pas que la limite de tours est affichée dans le "
        "coin supérieur gauche.</span>",
    "<span color='#D563D7'>You have run out of turns to complete the "
    "scenario, and have been defeated!</span>":
        "<span color='#D563D7'>Tu n’as plus de tours pour terminer le "
        "scénario, et tu as été vaincu !</span>",
}

# Couleur reelle de {TUTOR_COLOR} (po/.../_initial.cfg:370 du jeu original :
# "#define TUTOR_COLOR \n <<#D563D7>> #enddef" -- "<<" ">>" sont la syntaxe
# WML de citation litterale du preprocesseur, pas une couleur). Notre
# convertisseur WML->JSON a laisse passer le texte brut tronque au lieu
# d'extraire #D563D7.
_BROKEN_TUTOR_COLOR = "color='    <<'"
_FIXED_TUTOR_COLOR = "color='#D563D7'"


def _fix_tutor_color(text: str) -> str:
    return text.replace(_BROKEN_TUTOR_COLOR, _FIXED_TUTOR_COLOR)


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


def _translate_message(a: dict, fr_dict: dict, manual: dict, force: bool,
                        stats: dict, missing_out: list, location: str):
    """Traite une action {"t": "message", "a": {...}} : corrige la couleur
    TUTOR_COLOR corrompue dans "message" (langue source), et remplit
    "message_fr" depuis fr_dict/manual si absent (ou si force). Retourne
    True si `a` a ete modifie."""
    changed = False
    text_en = (a.get("message") or "")
    if _BROKEN_TUTOR_COLOR in text_en:
        a["message"] = _fix_tutor_color(text_en)
        text_en = a["message"]
        changed = True
    text_en = text_en.strip()
    if not text_en:
        return changed
    if a.get("message_fr") and not force:
        stats["skip"] += 1
        return changed
    fr = fr_dict.get(text_en) or manual.get(text_en)
    if fr:
        a["message_fr"] = _fix_tutor_color(fr)
        stats["done"] += 1
        if text_en in manual and text_en not in fr_dict:
            stats["manual"] += 1
        changed = True
    else:
        stats["missing"] += 1
        missing_out.append((location, a.get("speaker"), text_en))
    return changed


def _translate_option(a, fr_dict, manual, force, stats, missing_out, location):
    """Comme _translate_message, pour un noeud [option] : le texte du choix
    est dans "message" ou, plus souvent, "label" -- on ecrit la traduction
    dans le meme champ ("message_fr" ou "label_fr" selon celui utilise) ;
    lu par EventMessage::options_fr (events.cpp/battle_scene.cpp)."""
    changed = False
    key = "message" if a.get("message") else "label"
    text_en = (a.get(key) or "").strip()
    if not text_en:
        return changed
    fr_key = key + "_fr"
    if a.get(fr_key) and not force:
        stats["skip"] += 1
        return changed
    fr = fr_dict.get(text_en) or manual.get(text_en)
    if fr:
        a[fr_key] = fr
        stats["done"] += 1
        if text_en in manual and text_en not in fr_dict:
            stats["manual"] += 1
        changed = True
    else:
        stats["missing"] += 1
        missing_out.append((location, "[option]", text_en))
    return changed


def _walk_events(node, fr_dict, manual, force, stats, missing_out, location):
    """Parcourt recursivement l'arbre "events" (liste de noeuds
    {"t": ..., "a": {...}, "c": [...]}) et traite chaque action message,
    ainsi que les choix [option] qu'elle propose."""
    changed = False
    if isinstance(node, dict):
        if node.get("t") == "message":
            if _translate_message(node.get("a", {}), fr_dict, manual, force,
                                   stats, missing_out, location):
                changed = True
        elif node.get("t") == "option":
            if _translate_option(node.get("a", {}), fr_dict, manual, force,
                                  stats, missing_out, location):
                changed = True
        for v in node.values():
            if _walk_events(v, fr_dict, manual, force, stats, missing_out, location):
                changed = True
    elif isinstance(node, list):
        for v in node:
            if _walk_events(v, fr_dict, manual, force, stats, missing_out, location):
                changed = True
    return changed


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

    stats = {"done": 0, "manual": 0, "skip": 0, "missing": 0}
    missing_lines = []

    for path in sorted(Path(args.scenarios_dir).glob("*.json")):
        d = json.loads(path.read_text(encoding="utf-8"))
        changed = False

        for idx, msg in enumerate(d.get("intro_messages", [])):
            if _translate_message(msg, fr_dict, MANUAL_FR, args.force, stats,
                                   missing_lines, f"{path.name} intro_messages[{idx}]"):
                changed = True

        if _walk_events(d.get("events", []), fr_dict, MANUAL_FR, args.force,
                         stats, missing_lines, f"{path.name} events"):
            changed = True

        if changed and not args.dry_run:
            path.write_text(json.dumps(d, ensure_ascii=False, indent=2) + "\n",
                             encoding="utf-8")
            print(f"  écrit : {path.name}")
        elif changed:
            print(f"  (dry-run) modifierait : {path.name}")

    print(f"\n{stats['done']} message_fr ajoutés ({stats['manual']} traductions "
          f"manuelles, {stats['done'] - stats['manual']} depuis le .po officiel), "
          f"{stats['skip']} déjà présents, {stats['missing']} introuvables.",
          file=sys.stderr)
    if missing_lines:
        print("\nIntrouvables (à traduire à la main, ajouter à MANUAL_FR) :",
              file=sys.stderr)
        for loc, sp, t in missing_lines:
            print(f"  {loc} {sp}: {t!r}", file=sys.stderr)


if __name__ == "__main__":
    main()
