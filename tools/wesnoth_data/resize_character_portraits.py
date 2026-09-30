#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""resize_character_portraits.py -- redimensionne en 96x96 les portraits de
PERSONNAGES (images/portraits/<slug>.bmp -- Arvith, Baran, Deoran...), lus
par BattleScene::draw_dialog() (scene/battle_scene.cpp) pour la boîte de
dialogue en bataille et par StoryScene pour le récit.

Pourquoi ce script existe : gen_assets.py ne génère QUE les portraits de
TYPE d'unité (images/portraits/types/<type>.bmp, 96x96 -- pour les unités
sans portrait de personnage propre), à partir de units.json. Les portraits
de personnage nommé (Arvith, Baran, Mordak...), eux, sont copiés/convertis
à part depuis les .webp/.png sources de Wesnoth (voir la constante
`profile=` du WML, ex. "portraits/arvith.webp~RIGHT()") -- rien dans ce
dépôt d'outils ne les redimensionne au passage.

Repéré via un rapport utilisateur sur A Tale of Two Brothers : la boîte de
dialogue dessine le portrait avec gb::draw_image(path, 4, y+4), un blit
1:1 SANS mise à l'échelle (voir platform/gb_port_*.cpp, load_and_blit) --
un .bmp resté à sa taille d'origine (arvith.bmp = 500x500, baran.bmp =
400x400 mesurés dans ce dépôt) déborde donc massivement de la zone qui lui
est réservée (~100x96 px, avant le texte à tx=106) : seul un coin de
l'image s'affiche, ce qui peut aussi bien montrer un fond uni qu'un
fragment du visage n'importe où selon la composition de l'original --
d'où "on ne voit pas l'image"/"le portrait est à droite" dans le rapport.
Les portraits de South Guard (deoran.bmp etc.) sont déjà en 96x96 -- ils
ont dû passer par une étape de redimensionnement historique que Two
Brothers n'a jamais eue. Ce script comble cet écart pour toute campagne,
sans dépendre de units.json/WML (simple passe sur le dossier).

Usage :
    python3 resize_character_portraits.py --dir images/portraits
    (traite tous les .bmp directement dans --dir ; ignore le sous-dossier
    types/ -- déjà en 96x96 par construction via gen_assets.py -- et tout
    fichier déjà à la bonne taille, donc réexécutable sans risque après
    une régénération partielle.)
"""
import argparse
import sys
from pathlib import Path

from PIL import Image

SIZE = 96  # même constante que PORTRAIT dans gen_assets.py


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", required=True, help="dossier images/portraits (PAS .../types)")
    ap.add_argument("--size", type=int, default=SIZE)
    args = ap.parse_args()

    d = Path(args.dir)
    if d.name == "types":
        print("erreur : --dir pointe sur le sous-dossier 'types' (déjà en 96x96, "
              "généré par gen_assets.py) -- viser le dossier parent images/portraits.",
              file=sys.stderr)
        sys.exit(1)

    resized, skipped = 0, 0
    for p in sorted(d.glob("*.bmp")):
        im = Image.open(p)
        if im.size == (args.size, args.size):
            skipped += 1
            continue
        im.convert("RGB").resize((args.size, args.size), Image.LANCZOS).save(p)
        print(f"  redimensionné : {p.name} ({im.size[0]}x{im.size[1]} -> "
              f"{args.size}x{args.size})", file=sys.stderr)
        resized += 1

    print(f"\n{resized} portrait(s) redimensionné(s), {skipped} déjà à {args.size}x{args.size}.",
          file=sys.stderr)


if __name__ == "__main__":
    main()
