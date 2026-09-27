#!/usr/bin/env python3
"""generate_audio.py — synthèse vocale (TTS) hors-ligne pour les écrans de
récit de la campagne (campaign_bilingual.json), en anglais ET en français.

- Anglais : flite (voix "kal16/rms/slt/kal/awb" selon le personnage,
  cf. VOICE_MAP ci-dessous — c'est la table utilisée depuis le début du
  projet).
- Français : espeak-ng, avec les voix mbrola "mb-fr1" (masculin) et
  "mb-fr4" (féminin) quand elles sont installées (bien plus naturelles que
  la voix espeak-ng "fr-fr" de base), sinon repli automatique sur "fr-fr".

Synthétise les écrans de récit (story_screen/dialogue de
campaign_bilingual.json) ET, si --battle-scenarios-dir est fourni, les
messages d'ouverture joués PENDANT une bataille (intro_messages des
scenarios/*.json -- désormais doublés, cf. events.cpp::EventEngine::load()
et battle_scene.cpp, qui recalculent le même nom de fichier sans passer par
ce manifest). Les messages déclenchés dynamiquement en jeu (die/moveto/...)
restent muets (texte seul) : pas de séquence stable à indexer.

Usage :
    python3 generate_audio.py \
        --campaign /home/claude/w/gen/campaign_bilingual.json \
        --out-dir /home/claude/w/gen/sd_update/WESNOTH_SG/audio \
        --manifest /home/claude/w/gen/sd_update/WESNOTH_SG/manifest.json \
        --battle-scenarios-dir /home/claude/w/gen/data/scenarios

Par défaut, ne régénère pas un fichier .wav déjà présent (rapide en cas de
ré-exécution après un simple changement de texte ailleurs) ; --force
régénère tout.

Convention de nommage (chemins relatifs, stockés tels quels dans
manifest.json / lus par campaign_loader.cpp) :
    <scenario_id>/<beat_index:03d>_<speaker_slug>.wav       (anglais)
    <scenario_id>_fr/<beat_index:03d>_<speaker_slug>.wav    (français)
"""
import argparse
import json
import re
import shutil
import subprocess
import sys
import unicodedata
from pathlib import Path

# --- Table des voix anglaises (flite), par personnage. Reprise telle quelle
# du manifest.json déjà en place -- ne pas réordonner au hasard, ça changerait
# la voix de personnages déjà "connus" du joueur. ---
VOICE_MAP_EN = {
    "narrator": "kal16",
    "Deoran": "rms",
    "Mari": "slt",
    "unit": "slt",
    "second_unit": "slt",
    "Urza Mathin": "kal",
    "unknown": "kal16",
    "Zombie Mathin": "kal",
    "Urza Fastik": "kal",
    "Sir Gerrick": "awb",
    "Minister Hylas": "kal16",
    "Galdrad": "awb",
    "Ethiliel": "slt",
    "Vaenuith": "slt",
    "Talchar": "rms",
    "Ithelden": "rms",
    "Urza Nalmath": "kal",
    "Afalas": "kal",
    "Mebrin": "awb",
}
DEFAULT_VOICE_EN = "kal16"

# --- Correspondance voix anglaise -> voix française, pour garder un même
# "genre" de voix par personnage d'une langue à l'autre. mb-fr1 (masculin) et
# mb-fr4 (féminin) sont les voix mbrola installées via `apt-get install
# mbrola-fr1 mbrola-fr4` (bien meilleures que la voix espeak-ng "fr-fr" de
# base) ; fr-fr sert de repli si mbrola n'est pas disponible sur la machine
# qui exécute ce script. ---
EN_TO_FR_VOICE = {
    "kal16": "fr-fr",       # narrateur : voix neutre espeak-ng
    "rms": "mb-fr1",        # masculin
    "kal": "mb-fr1",        # masculin
    "awb": "mb-fr1",        # masculin
    "slt": "mb-fr4",        # féminin
}


def slugify(name: str) -> str:
    if not name:
        return "unknown"
    n = unicodedata.normalize("NFKD", name).encode("ascii", "ignore").decode("ascii")
    n = re.sub(r"[^A-Za-z0-9]+", "", n)
    return n or "unknown"


def has_mbrola_voice(mb_name: str) -> bool:
    """mb-fr1/mb-fr4 ne sont listées par `espeak-ng --voices=mb` que si le
    paquet mbrola-frX correspondant est installé (sinon espeak-ng plante à
    l'exécution). On vérifie une fois au démarrage plutôt qu'à chaque ligne."""
    try:
        out = subprocess.run(["espeak-ng", "--voices=mb"], capture_output=True, text=True, timeout=10)
        return mb_name in out.stdout
    except Exception:
        return False


def resolve_fr_voice(en_voice: str, mbrola_ok: dict) -> str:
    mb = EN_TO_FR_VOICE.get(en_voice, "fr-fr")
    if mb.startswith("mb-") and not mbrola_ok.get(mb):
        return "fr-fr"
    return mb


def synth_en(text: str, voice: str, out_path: Path) -> bool:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    try:
        subprocess.run(["flite", "-voice", voice, "-o", str(out_path), text],
                        check=True, capture_output=True, text=True, timeout=60)
        return out_path.exists() and out_path.stat().st_size > 0
    except subprocess.CalledProcessError as e:
        print(f"  ! flite a echoue ({voice}): {e.stderr.strip()[:200]}", file=sys.stderr)
        return False


def synth_fr(text: str, voice: str, out_path: Path) -> bool:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    espeak_voice = voice if not voice.startswith("mb-") else f"mb/{voice}"
    raw_path = out_path.with_suffix(".raw.wav")
    try:
        subprocess.run(["espeak-ng", "-v", espeak_voice, "-w", str(raw_path), text],
                        check=True, capture_output=True, text=True, timeout=60)
        if not (raw_path.exists() and raw_path.stat().st_size > 0):
            return False
        # Ramene au meme format que les .wav flite (16kHz mono 16-bit) --
        # espeak-ng/mbrola sortent en 22050Hz, ce qui gonfle inutilement la
        # taille de la carte SD (memoire limitee cote AKA) sans ameliorer la
        # qualite perceptible sur ce type de synthese.
        if shutil.which("sox"):
            subprocess.run(["sox", str(raw_path), "-r", "16000", "-c", "1", "-b", "16", str(out_path)],
                            check=True, capture_output=True, text=True, timeout=60)
            raw_path.unlink(missing_ok=True)
        else:
            raw_path.rename(out_path)
        return out_path.exists() and out_path.stat().st_size > 0
    except subprocess.CalledProcessError as e:
        print(f"  ! espeak-ng/sox a echoue ({espeak_voice}): {e.stderr.strip()[:200]}", file=sys.stderr)
        return False


def process_battle_intros(scenarios_dir, out_dir, mbrola_ok, args):
    """Doublage des messages d'ouverture de bataille (intro_messages des
    scenarios/*.json), désormais joués par battle_scene.cpp -- voir
    events.cpp::EventEngine::load(), qui calcule EXACTEMENT le même nom de
    fichier (scenario_id/intro_<idx:03d>_<slug>.wav) : ne pas changer l'un
    sans l'autre. Le texte français vient du champ "message_fr" déjà écrit
    dans le JSON (tools/wesnoth_data/add_intro_fr.py) ; un message sans
    "message_fr" n'est doublé qu'en anglais."""
    lines = []
    n_gen_en = n_gen_fr = n_skip = 0
    for path in sorted(Path(scenarios_dir).glob("*.json")):
        sc_id = path.stem
        if args.scenario and sc_id != args.scenario:
            continue
        d = json.loads(path.read_text(encoding="utf-8"))
        for idx, msg in enumerate(d.get("intro_messages", [])):
            text_en = (msg.get("message") or "").strip()
            if not text_en:
                continue
            text_fr = (msg.get("message_fr") or "").strip()
            speaker = msg.get("speaker") or "narrator"
            voice_en = VOICE_MAP_EN.get(speaker, DEFAULT_VOICE_EN)
            slug = slugify(speaker)

            if args.lang in ("en", "both"):
                rel = f"{sc_id}/intro_{idx:03d}_{slug}.wav"
                out_path = out_dir / rel
                if args.force or not out_path.exists():
                    if synth_en(text_en, voice_en, out_path):
                        n_gen_en += 1
                        print(f"  [en] {rel}")
                    else:
                        continue
                else:
                    n_skip += 1
                if out_path.exists():
                    lines.append({"scenario": sc_id, "message_index": idx, "speaker": speaker,
                                  "lang": "en", "voice": voice_en, "text": text_en,
                                  "file": rel, "bytes": out_path.stat().st_size, "kind": "battle_intro"})

            if args.lang in ("fr", "both") and text_fr:
                voice_fr = resolve_fr_voice(voice_en, mbrola_ok)
                rel_fr = f"{sc_id}_fr/intro_{idx:03d}_{slug}.wav"
                out_path_fr = out_dir / rel_fr
                if args.force or not out_path_fr.exists():
                    if synth_fr(text_fr, voice_fr, out_path_fr):
                        n_gen_fr += 1
                        print(f"  [fr] {rel_fr} (voix={voice_fr})")
                    else:
                        continue
                else:
                    n_skip += 1
                if out_path_fr.exists():
                    lines.append({"scenario": sc_id, "message_index": idx, "speaker": speaker,
                                  "lang": "fr", "voice": voice_fr, "text": text_fr,
                                  "file": rel_fr, "bytes": out_path_fr.stat().st_size, "kind": "battle_intro"})
    return lines, n_gen_en, n_gen_fr, n_skip


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--campaign", default="/home/claude/w/gen/campaign_bilingual.json")
    ap.add_argument("--out-dir", default="/home/claude/w/gen/sd_update/WESNOTH_SG/audio",
                     help="Dossier de sortie des .wav (= <sdroot>/WESNOTH_SG/audio)")
    ap.add_argument("--manifest", default="/home/claude/w/gen/sd_update/WESNOTH_SG/manifest.json")
    ap.add_argument("--force", action="store_true", help="Regenere meme si le .wav existe deja")
    ap.add_argument("--lang", choices=["en", "fr", "both"], default="both",
                     help="Ne (re)generer qu'une langue (par defaut : les deux)")
    ap.add_argument("--scenario", default=None, help="Ne traiter qu'un seul scenario (id)")
    ap.add_argument("--battle-scenarios-dir", default=None,
                     help="Si fourni, double aussi les intro_messages des scenarios/*.json "
                          "de ce dossier (bataille) en plus des beats de --campaign (recit).")
    args = ap.parse_args()

    if not shutil.which("flite"):
        print("ERREUR : flite introuvable (apt-get install flite)", file=sys.stderr)
        sys.exit(1)
    if not shutil.which("espeak-ng"):
        print("ERREUR : espeak-ng introuvable (apt-get install espeak-ng)", file=sys.stderr)
        sys.exit(1)

    mbrola_ok = {"mb-fr1": has_mbrola_voice("mb-fr1"), "mb-fr4": has_mbrola_voice("mb-fr4")}
    for name, ok in mbrola_ok.items():
        state = "disponible" if ok else "absente -> repli sur fr-fr (apt-get install mbrola-fr1 mbrola-fr4 pour l'activer)"
        print(f"Voix mbrola {name} : {state}")

    campaign = json.loads(Path(args.campaign).read_text(encoding="utf-8"))
    scenarios = campaign["scenarios"] if isinstance(campaign, dict) and "scenarios" in campaign else [campaign]

    out_dir = Path(args.out_dir)
    lines = []
    n_gen_en = n_gen_fr = n_skip = 0

    for sc in scenarios:
        sc_id = sc["id"]
        if args.scenario and sc_id != args.scenario:
            continue
        for idx, beat in enumerate(sc.get("beats", [])):
            text_en = beat.get("text") or beat.get("text_en") or ""
            text_fr = beat.get("text_fr") or ""
            if not text_en.strip():
                continue
            speaker = beat.get("speaker") or "narrator"
            voice_en = VOICE_MAP_EN.get(speaker, DEFAULT_VOICE_EN)
            slug = slugify(speaker)

            # --- Anglais ---
            if args.lang in ("en", "both"):
                rel = f"{sc_id}/{idx:03d}_{slug}.wav"
                out_path = out_dir / rel
                if args.force or not out_path.exists():
                    ok = synth_en(text_en, voice_en, out_path)
                    if ok:
                        n_gen_en += 1
                        print(f"  [en] {rel}")
                    else:
                        continue
                else:
                    n_skip += 1
                if out_path.exists():
                    lines.append({
                        "scenario": sc_id, "beat_index": idx, "speaker": speaker,
                        "lang": "en", "voice": voice_en, "text": text_en,
                        "file": rel, "bytes": out_path.stat().st_size,
                    })

            # --- Français (seulement si une traduction existe pour ce beat) ---
            if args.lang in ("fr", "both") and text_fr.strip():
                voice_fr = resolve_fr_voice(voice_en, mbrola_ok)
                rel_fr = f"{sc_id}_fr/{idx:03d}_{slug}.wav"
                out_path_fr = out_dir / rel_fr
                if args.force or not out_path_fr.exists():
                    ok = synth_fr(text_fr, voice_fr, out_path_fr)
                    if ok:
                        n_gen_fr += 1
                        print(f"  [fr] {rel_fr} (voix={voice_fr})")
                    else:
                        continue
                else:
                    n_skip += 1
                if out_path_fr.exists():
                    lines.append({
                        "scenario": sc_id, "beat_index": idx, "speaker": speaker,
                        "lang": "fr", "voice": voice_fr, "text": text_fr,
                        "file": rel_fr, "bytes": out_path_fr.stat().st_size,
                    })

    if args.battle_scenarios_dir:
        bl, be, bf, bs = process_battle_intros(Path(args.battle_scenarios_dir), out_dir, mbrola_ok, args)
        lines += bl
        n_gen_en += be
        n_gen_fr += bf
        n_skip += bs

    # Fusion avec un manifest.json existant (clé = fichier .wav produit) :
    # une exécution partielle (--lang/--scenario) ne doit JAMAIS effacer les
    # entrées déjà enregistrées par une exécution précédente qui ne portait
    # pas sur le même sous-ensemble (bug déjà rencontré une fois -- voir
    # l'historique du projet).
    existing = {}
    manifest_path = Path(args.manifest)
    if manifest_path.exists():
        try:
            old = json.loads(manifest_path.read_text(encoding="utf-8"))
            for ln in old.get("lines", []):
                existing[ln.get("file")] = ln
        except Exception:
            pass
    for ln in lines:
        existing[ln["file"]] = ln
    merged_lines = list(existing.values())

    manifest = {
        "voice_map_used": VOICE_MAP_EN,
        "fr_voice_map_used": {k: resolve_fr_voice(k, mbrola_ok) for k in set(VOICE_MAP_EN.values())},
        "lines": merged_lines,
    }
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")

    print(f"\nTermine : {n_gen_en} fichier(s) anglais generes, {n_gen_fr} fichier(s) francais generes, "
          f"{n_skip} deja presents (non regeneres, utiliser --force sinon).")
    print(f"Manifest ecrit : {manifest_path} ({len(merged_lines)} entrees au total, fusionne avec l'existant)")


if __name__ == "__main__":
    main()
