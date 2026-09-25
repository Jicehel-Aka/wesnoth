#!/usr/bin/env python3
"""gen_assets.py — convertit les VRAIES images de Wesnoth pour la console.

Sorties (dans --out = /sdcard/WESNOTH_SG/gfx) :
  terrain/<code>.aki   une tuile par code de terrain présent dans les cartes
                       de la campagne : image de base + surcouche (forêt,
                       village...) composées, détourées avec le masque
                       hexagonal officiel (terrain/alphamask.png), réduites
                       à la taille d'hexagone du jeu (42x44).
  units/<type>__<couleur>.aki
                       sprite de chaque type d'unité, recoloré dans la
                       couleur d'équipe (palette « magenta » de Wesnoth),
                       réduit à 40x40.
  ../images/portraits/types/<type>.bmp
                       portrait 96x96 de chaque type (pour les unités sans
                       portrait de personnage).

Format .aki : 'AKI1', largeur u16, hauteur u16, puis largeur*hauteur pixels
u16 BGR565 petit-boutiste (couleur de l'écran AKA). 0xF81F = transparent.
Noms de fichiers : '^' -> '-', espaces -> '_', ':' -> '+'.
"""
import argparse
import glob
import json
import os
import re
import struct

from PIL import Image

KEY = 0xF81F
TILE_W, TILE_H = 42, 44
SPRITE = 40
PORTRAIT = 96

# Couleurs d'équipe (valeur « moyenne » de chaque couleur Wesnoth)
TEAM = {
    "red": (255, 0, 0), "blue": (46, 65, 155), "green": (98, 182, 100),
    "purple": (147, 0, 157), "black": (90, 90, 90), "brown": (148, 80, 39),
    "orange": (255, 126, 0), "white": (225, 225, 225), "teal": (48, 203, 192),
    "yellow": (230, 230, 30),
}
SIDE_DEFAULT = {1: "red", 2: "blue", 3: "green", 4: "purple", 5: "black",
                6: "brown", 7: "orange", 8: "white", 9: "teal"}
TOD_IMG = ["tod-clouds-dawn", "tod-clouds-day", "tod-clouds-day", "tod-clouds-dusk",
           "tod-clouds-night", "tod-clouds-night"]


def safe(name):
    return name.replace("^", "-").replace(" ", "_").replace(":", "+")


def bgr565(r, g, b):
    v = ((b >> 3) << 11) | ((g >> 2) << 5) | (r >> 3)
    return v ^ 0x0020 if v == KEY else v


def write_aki(path, im):
    im = im.convert("RGBA")
    w, h = im.size
    out = bytearray(b"AKI1" + struct.pack("<HH", w, h))
    for r, g, b, a in (im.get_flattened_data() if hasattr(im, 'get_flattened_data') else im.getdata()):
        out += struct.pack("<H", bgr565(r, g, b) if a >= 128 else KEY)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(out)


def is_tc(r, g, b):
    """Pixel de la palette « magenta » de Wesnoth (à recolorer)."""
    return r > b and g < 0.6 * min(r, b) + 1 and r - g > 30


def team_color(im, color):
    tr, tg, tb = TEAM[color]
    im = im.convert("RGBA")
    px = im.load()
    key_lum = (236 + 0 + 140) / 3.0
    for y in range(im.size[1]):
        for x in range(im.size[0]):
            r, g, b, a = px[x, y]
            if a == 0 or not is_tc(r, g, b):
                continue
            lum = (r + g + b) / 3.0
            if lum <= key_lum:
                k = lum / key_lum
                px[x, y] = (int(tr * k), int(tg * k), int(tb * k), a)
            else:
                k = (lum - key_lum) / (255 - key_lum)
                px[x, y] = (int(tr + (255 - tr) * k), int(tg + (255 - tg) * k), int(tb + (255 - tb) * k), a)
    return im


def find_image(rel, roots):
    rel = rel.strip().strip('"')
    for r in roots:
        for cand in (os.path.join(r, rel), os.path.join(r, "images", rel)):
            for ext in ("", ".png", ".webp"):
                if os.path.exists(cand + ext):
                    return cand + ext
    return None


def terrain_symbols(terrain_cfg):
    t = open(terrain_cfg, encoding="utf-8").read()
    sym = {}
    for b in re.findall(r"\[terrain_type\](.*?)\[/terrain_type\]", t, re.S):
        s = re.search(r"^\s*string=(\S+)", b, re.M)
        im = re.search(r"^\s*symbol_image=(\S+)", b, re.M)
        if s and im:
            sym[s.group(1)] = im.group(1)
    return sym


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--images", required=True, help="dossier contenant terrain/ units/ portraits/ unit_env/")
    ap.add_argument("--campaign", required=True)
    ap.add_argument("--terrain-cfg", required=True)
    ap.add_argument("--data", required=True, help="dossier des JSON générés (units.json, terrain.json, scenarios/)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--portraits-out", required=True)
    a = ap.parse_args()
    roots = [a.images, os.path.join(a.campaign, "images")]

    # --- terrain ---------------------------------------------------------
    codes = json.load(open(os.path.join(a.data, "terrain.json"), encoding="utf-8"))["codes"]
    sym = terrain_symbols(a.terrain_cfg)
    mask = Image.open(os.path.join(a.images, "terrain", "alphamask.png")).convert("RGBA").split()[3] \
        if os.path.exists(os.path.join(a.images, "terrain", "alphamask.png")) else None
    n_t, missing = 0, set()
    for code in codes:
        base, _, over = code.partition("^")
        canvas = Image.new("RGBA", (72, 72), (0, 0, 0, 0))
        for part in [base] + (["^" + over] if over else []):
            s = sym.get(part)
            p = find_image("terrain/" + s, [a.images]) if s else None
            if not p:
                if part == base:
                    missing.add(part)
                continue
            tile = Image.open(p).convert("RGBA")
            if tile.size != (72, 72):
                tile = tile.resize((72, 72))
            canvas.alpha_composite(tile)
        if mask is not None:
            alpha = Image.new("L", (72, 72), 0)
            alpha.paste(canvas.split()[3], (0, 0))
            from PIL import ImageChops
            canvas.putalpha(ImageChops.multiply(alpha, mask))
        write_aki(os.path.join(a.out, "terrain", safe(code) + ".aki"),
                  canvas.resize((TILE_W, TILE_H), Image.LANCZOS))
        n_t += 1

    # --- unités -------------------------------------------------------------
    units = json.load(open(os.path.join(a.data, "units.json"), encoding="utf-8"))["types"]
    colors = set()
    for sf in glob.glob(os.path.join(a.data, "scenarios", "*.json")):
        for s in json.load(open(sf, encoding="utf-8"))["sides"]:
            colors.add(s.get("color") or SIDE_DEFAULT.get(int(s["side"]), "red"))
    colors = sorted(c for c in colors if c in TEAM)
    n_u, no_img = 0, []
    for tid, t in units.items():
        p = find_image(t.get("image", ""), roots) if t.get("image") else None
        if not p:
            no_img.append(tid)
            continue
        src = Image.open(p).convert("RGBA")
        # cadrage : le sprite Wesnoth fait 72x72 (parfois plus grand)
        if src.size != (72, 72):
            side = max(src.size)
            sq = Image.new("RGBA", (side, side), (0, 0, 0, 0))
            sq.paste(src, ((side - src.size[0]) // 2, (side - src.size[1]) // 2))
            src = sq
        for c in colors:
            # le personnage n'occupe que le centre des 72x72 : on recadre
            # (pieds en bas) pour qu'il soit lisible dans un hexagone de 42 px
            im = team_color(src, c).crop((8, 4, 64, 60)).resize((SPRITE, SPRITE), Image.LANCZOS)
            write_aki(os.path.join(a.out, "units", f"{safe(tid)}__{c}.aki"), im)
            n_u += 1
        prof = find_image(t.get("profile", ""), roots) if t.get("profile") else None
        if prof:
            pim = Image.open(prof).convert("RGBA")
            bg = Image.new("RGB", pim.size, (18, 20, 28))
            bg.paste(pim, mask=pim.split()[3])
            os.makedirs(a.portraits_out, exist_ok=True)
            bg.resize((PORTRAIT, PORTRAIT), Image.LANCZOS).save(
                os.path.join(a.portraits_out, safe(tid) + ".bmp"))


    print(f"terrain: {n_t} tuiles (bases sans image: {sorted(missing)}) ; "
          f"sprites: {n_u} ({len(colors)} couleurs: {colors}) ; types sans image: {no_img}")


if __name__ == "__main__":
    main()
