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
- `fix_story_hotkeys.py` : nettoie les balises Pango/WML (`<b>`, `<span>`...) et
  applique les **verrous texte** de `text_overrides.json` (raccourcis clavier PC
  sans sens sur l'AKA, reformulations manuelles...) sur tous les JSON de texte
  (campagne ET scénarios de bataille). À relancer après chaque régénération :
  `python3 fix_story_hotkeys.py /sdcard/WESNOTH_SG/campaign_bilingual.json /sdcard/WESNOTH_SG/data/scenarios/*.json`
  - Pour verrouiller un texte corrigé à la main (traduction, réécriture,
    suppression d'un passage) afin qu'il survive à une prochaine régénération
    complète des données : ajouter une entrée `{"old": "...", "new": "..."}`
    dans `text_overrides.json` plutôt que d'éditer le JSON généré directement.
    Le script l'appliquera automatiquement partout où `"old"` apparaît, quel
    que soit le scénario/fichier concerné.
  - Le script signale en fin d'exécution les verrous **obsolètes** (motif
    `"old"` introuvable dans les fichiers traités) : ça veut dire que le texte
    amont a changé (mise à jour de la campagne...) et qu'il faut relire/adapter
    l'entrée correspondante dans `text_overrides.json`.
