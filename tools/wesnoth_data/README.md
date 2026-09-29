# Outils de données Wesnoth → AKA

Tout ce que le jeu lit sur la SD (`/sdcard/WESNOTH_SG/data` et `gfx`, ou
`/sdcard/WESNOTH_TB/...` pour A Tale of Two Brothers -- même pipeline, voir
`scene/campaign_root.h` côté firmware pour la liste des campagnes) est
généré depuis les vrais fichiers de Battle for Wesnoth (branche master).
Tous les scripts ci-dessous sont génériques (`--campaign` prend
`The_South_Guard` ou `Two_Brothers` indifféremment) ; les exemples utilisent
South Guard mais s'appliquent tels quels à Two Brothers en changeant ce nom
de dossier (et `WESNOTH_SG` → `WESNOTH_TB` côté carte SD).

Arborescence d'entrée attendue (extraits du dépôt wesnoth) :
- `data/core/units.cfg`, `data/core/units/`, `data/core/terrain.cfg`
- `data/core/macros/` (traits, horaires, macros d'unités et d'événements)
- `data/campaigns/The_South_Guard/` (ou `Two_Brothers/`)
- `data/core/images/` : `terrain/`, `units/`, `portraits/`
- `po/wesnoth/`, `po/wesnoth-units/`, `po/wesnoth-tsg/` ou `po/wesnoth-tb/`
  (traductions françaises officielles -- voir section « Traduction française »)

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

## Traduction française

Tout texte affiché en jeu est bilingue : un champ anglais (`message`,
`text`, `name`...) toujours présent, et un champ `..._fr` frère, lu par le
moteur quand `wsg::campaign_state().lang_fr` est vrai (menu système en
français) -- absent, il se replie silencieusement sur l'anglais. Les
scripts ci-dessous remplissent ces champs `_fr`, **après** `gen_data.py`/
`extract_scenario.py`/`fix_story_hotkeys.py` (ils modifient les JSON déjà
générés, en place) et **avant** `generate_audio.py --lang fr` (le doublage
audio lit `message_fr` pour savoir quoi synthétiser).

Source de vérité : les traductions officielles de Wesnoth, extraites du
dépôt `wesnoth/wesnoth` (`git show HEAD:po/<domaine>/fr.po`) et bundlées
dans `po/` :
- `po/wesnoth-base_fr.po` (domaine générique `wesnoth`, ex. « Turns run
  out ») et `po/wesnoth-units_fr.po` (noms de types d'unités) : communs aux
  deux campagnes.
- `po/wesnoth-tsg_fr.po` (South Guard) / `po/wesnoth-tb_fr.po` (Two
  Brothers) : texte propre à chaque campagne, prioritaire en cas de
  collision (toujours passé en dernier `--po`).

Rien ne dépend de `polib` (chaque script embarque son propre parseur `.po`,
`load_po_dict()` dans `add_intro_fr.py`, réutilisé par les deux autres).
Le contenu propre à ce portage (tutoriel Gamebuino, boutons AKA...), absent
des `.po` officiels, est traduit à la main dans un dict `MANUAL_FR` en tête
de chaque script -- un manque signalé en fin d'exécution (« introuvable »)
veut dire soit une entrée à ajouter à `MANUAL_FR`, soit un texte modifié en
amont qu'il faut re-matcher.

- `add_intro_fr.py --scenarios-dir data/scenarios --po po/wesnoth-tsg_fr.po`
  (ou `wesnoth-tb_fr.po`) : traite tous les scénarios d'un coup. Remplit
  `message_fr` sur les répliques d'ouverture (`intro_messages`) ET sur tous
  les messages dynamiques de l'arbre `events` (die/moveto/victoire...),
  ainsi que `message_fr`/`label_fr` sur les choix de dialogue (`[option]`).
  Corrige aussi au passage une corruption de données historique
  (`{TUTOR_COLOR}` mal converti) si elle est encore présente.
- `add_unit_names_fr.py --units-json data/units.json --po po/wesnoth-units_fr.po --po po/wesnoth-tsg_fr.po` :
  remplit `name_fr` sur chaque type d'unité (`data/units.json`), lu par
  `UnitTypeDef::name_fr` (moteur) pour le nom affiché en bataille et dans
  le journal de combat.
- `add_objectives_fr.py --scenario data/scenarios/<fichier>.json --po po/wesnoth-base_fr.po --po po/wesnoth-units_fr.po --po po/wesnoth-tsg_fr.po` :
  **un scénario à la fois** (contrairement aux deux scripts ci-dessus).
  Remplit `text_fr` sur les objectifs statiques (`objectives[]`) et
  `description_fr` sur les objectifs dynamiques (`[objectives][objective]`
  dans `events`), lus par le menu Objectifs (Ctrl+J) en bataille.

Tous acceptent `--force` (retraduire même les champs `_fr` déjà présents --
utile après une mise à jour des `.po`) et `--dry-run` (n'écrit rien, montre
juste la couverture). Sans `--force`, un champ `_fr` déjà rempli n'est
jamais touché : relancer un script après une régénération partielle ne
retraduit que ce qui manque.

`generate_audio.py --lang fr` (ou `both`) synthétise ensuite l'audio à
partir de ces champs `_fr` (silencieusement ignoré si absent ou réduit à de
la ponctuation, cf. `has_speakable_content()`). Un `.wav` déjà présent mais
vide (0 octet -- reliquat d'une exécution interrompue) est automatiquement
régénéré même sans `--force` (`needs_regen()`), sans retoucher le reste.
