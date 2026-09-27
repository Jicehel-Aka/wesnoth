# Outils de données Wesnoth → AKA

Tout ce que le jeu lit sur la SD (`/sdcard/WESNOTH_SG/data` et `gfx`) est
généré depuis les vrais fichiers de Battle for Wesnoth (branche master).

Arborescence d'entrée attendue (extraits du dépôt wesnoth) :
- `data/core/units.cfg`, `data/core/units/`, `data/core/terrain.cfg`
- `data/core/macros/` (traits, horaires, macros d'unités et d'événements)
- `data/campaigns/The_South_Guard/`
- `data/core/images/` : `terrain/`, `units/`, `portraits/`

```sh
python3 gen_data.py --units-cfg units.cfg --units-dir units --terrain-cfg terrain.cfg \
    --campaign The_South_Guard --macros macros --out data
python3 extract_scenario.py --campaign The_South_Guard --macros macros \
    --scenario The_South_Guard/scenarios/01_Born_to_the_Banner.cfg \
    --terrain-json data/terrain.json --out data/scenarios/01_Born_to_the_Banner.json
python3 gen_assets.py --images images --campaign The_South_Guard --terrain-cfg terrain.cfg \
    --data data --out gfx --portraits-out images/portraits/types
```
Copier ensuite `data/`, `gfx/`, les cartes (`The_South_Guard/maps/*.map` → `maps/`)
et `images/portraits/types/` dans `/sdcard/WESNOTH_SG/`.

- `wml.py` : préprocesseur + lecteur WML (macros, #ifdef, [+tag], clés multiples).
- `gen_data.py` : types d'unités (avec lignées), movetypes, races, traits réels, terrains.
- `extract_scenario.py` : rejoue les événements de mise en place d'un scénario
  (difficulté `--difficulty`, choix de message `--choose`, par défaut on passe le tutoriel).
- `gen_assets.py` : tuiles hexagonales, sprites en couleurs d'équipe, portraits de type.
- `fix_story_hotkeys.py` : corrige dans `campaign_bilingual.json` les instructions de
  raccourcis clavier PC copiées telles quelles du WML d'origine (Ctrl+R, Ctrl+Space,
  Ctrl+J, Alt+R, touche "u", clic droit...) qui n'ont pas de sens sur l'AKA, en les
  remplaçant par les vraies actions manette (menu, boutons). À relancer après chaque
  régénération de `campaign_bilingual.json` :
  `python3 fix_story_hotkeys.py /sdcard/WESNOTH_SG/campaign_bilingual.json`
