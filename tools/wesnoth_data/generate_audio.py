#!/usr/bin/env python3
"""generate_audio.py — synthèse vocale (TTS) hors-ligne pour les écrans de
récit de la campagne (campaign_bilingual.json), en anglais ET en français.

- Anglais : flite (voix "kal16/rms/slt/kal/awb" selon le personnage,
  cf. VOICE_MAP ci-dessous — c'est la table utilisée depuis le début du
  projet).
- Français : Piper (TTS neuronal, voix "fr_FR-tom-medium"/"fr_FR-siwis-medium"
  -- BEAUCOUP plus naturel que l'ancien espeak-ng/mbrola, cf. PIPER_VOICES_EN
  ci-dessous) quand un modèle Piper est présent dans --piper-voices-dir ;
  repli automatique sur espeak-ng avec les voix mbrola "mb-fr1" (masculin) et
  "mb-fr4" (féminin) si elles sont installées (meilleures que la voix
  espeak-ng "fr-fr" de base, mais nettement plus robotiques que Piper), et en
  dernier recours sur "fr-fr" (la plus robotique des trois).

  Piper ne peut PAS être utilisé tel quel dans ce bac à sable cloud : ses
  modèles de voix (.onnx, ~60 Mo chacun) sont hébergés sur huggingface.co,
  qui n'est pas joignable depuis cet environnement (seuls pypi/npm/github
  sont autorisés). Il faut donc générer le doublage français sur une machine
  avec accès internet complet (ex. le PC Windows du projet) -- voir
  "Activer Piper" plus bas dans ce docstring.

Activer Piper (une fois, sur la machine qui génère l'audio -- PAS ce bac à
sable). Dans une console ESP-IDF Windows (idf.py export), l'interpréteur
s'appelle "python" (pas "python3") :
    python -m pip install piper-tts
    python -m piper.download_voices fr_FR-tom-medium      # voix masculine
    python -m piper.download_voices fr_FR-siwis-medium    # voix féminine
  Si download_voices échoue (proxy, pare-feu...), téléchargement direct à la
  place (mêmes fichiers, sur huggingface.co) :
    https://huggingface.co/rhasspy/piper-voices/resolve/main/fr/fr_FR/tom/medium/fr_FR-tom-medium.onnx
    https://huggingface.co/rhasspy/piper-voices/resolve/main/fr/fr_FR/tom/medium/fr_FR-tom-medium.onnx.json
    https://huggingface.co/rhasspy/piper-voices/resolve/main/fr/fr_FR/siwis/medium/fr_FR-siwis-medium.onnx
    https://huggingface.co/rhasspy/piper-voices/resolve/main/fr/fr_FR/siwis/medium/fr_FR-siwis-medium.onnx.json
  Les fichiers .onnx/.onnx.json téléchargés (dans le répertoire courant, ou
  ~/.local/share/piper-voices selon la version) doivent être copiés/déplacés
  dans --piper-voices-dir (par défaut : tools/wesnoth_data/voices/ à côté de
  ce script). Le script détecte automatiquement leur présence et bascule sur
  Piper pour le français ; si les fichiers sont absents, il retombe sur
  espeak-ng/mbrola comme avant (aucune régression si Piper n'est pas dispo).
  --fr-tts force explicitement piper|mbrola|auto (auto = piper si trouvé).

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
import os
import re
import shutil
import subprocess
import sys
import time
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

# --- Correspondance voix anglaise (flite) -> nom de voix Piper, même
# principe qu'EN_TO_FR_VOICE mais pour le backend neuronal (voir docstring
# "Activer Piper" en tête de fichier). "tom" = masculin, "siwis" = féminin ;
# ce sont les deux voix fr_FR de meilleure qualité disponibles dans
# piper-voices au moment d'écrire ce script. ---
EN_TO_PIPER_VOICE = {
    "kal16": "fr_FR-siwis-medium",   # narrateur : voix neutre/féminine, claire
    "rms": "fr_FR-tom-medium",       # masculin
    "kal": "fr_FR-tom-medium",       # masculin
    "awb": "fr_FR-tom-medium",       # masculin
    "slt": "fr_FR-siwis-medium",     # féminin
}


def find_piper_voices(voices_dir: Path) -> dict:
    """Repère les modèles Piper présents dans --piper-voices-dir (une paire
    <nom>.onnx + <nom>.onnx.json par voix, tels que produits par
    `python -m piper.download_voices <nom>` (Windows/ESP-IDF) ou `python3 ...` (Linux)). Retourne {nom: chemin .onnx}
    pour les seules voix complètes (les deux fichiers présents)."""
    found = {}
    if not voices_dir.is_dir():
        return found
    for onnx in voices_dir.glob("*.onnx"):
        cfg = onnx.with_suffix(".onnx.json")
        if cfg.exists():
            found[onnx.stem] = onnx
    return found


def has_speakable_content(text: str) -> bool:
    """False pour un texte reduit a de la ponctuation/des points de
    suspension ("...", "!?", etc.) -- ce genre de repartie (grognement de
    douleur, silence dramatique...) fait planter Piper (son phonemizeur
    interne produit zero phoneme et sa boucle lines_to_wav() indexe alors
    dans du vide) sans que ce soit un vrai probleme de traduction : on saute
    juste la generation audio pour cette ligne (silence, ce qui est de toute
    facon approprie pour ce type de repartie)."""
    return any(c.isalpha() for c in text)


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


def synth_fr_piper(text: str, model_path: Path, out_path: Path) -> bool:
    """Synthèse française via Piper (TTS neuronal) -- voir docstring
    "Activer Piper" en tête de fichier. `model_path` pointe le .onnx ; le
    .onnx.json associé est trouvé automatiquement par piper à côté de lui.
    Sort déjà en 16kHz mono (config des voix fr_FR de piper-voices), donc pas
    de passage par sox contrairement à synth_fr()."""
    out_path.parent.mkdir(parents=True, exist_ok=True)
    # Retente 3 fois : la plupart des echecs observes sont transitoires
    # (verrouillage de fichier passager -- p.ex. si le dossier de sortie est
    # synchronise par OneDrive, qui peut verrouiller un .wav fraichement
    # ecrit le temps de l'indexer/l'uploader -- ou une contention CPU/E-S
    # passagere pendant un gros lot). Un court delai avant de reessayer
    # suffit en general.
    last_err = None
    for attempt in range(3):
        try:
            # sys.executable (pas "python3" en dur) : sous la console ESP-IDF de
            # Windows, l'interpreteur s'appelle "python.exe" et est lance via
            # "python", pas "python3" -- on reutilise donc le meme interpreteur
            # que celui qui execute ce script, quel que soit son nom/chemin.
            # encoding="utf-8" explicite : sous Windows, text=True sans encodage
            # utilise le codepage de la console (cp1252 en general), qui ne sait
            # pas encoder certains caracteres typographiques francais (espace
            # fine insecable U+202F, tirets cadratins, guillemets courbes...) et
            # fait planter l'ecriture sur stdin avec UnicodeEncodeError.
            #
            # Mais encoding="utf-8" ici ne regle que l'ENVOI (les octets qu'on
            # ecrit sur son entree standard sont du vrai UTF-8) -- pas la
            # LECTURE : le sous-processus Piper est lui-meme un interpreteur
            # Python, et sur Windows son stdin se decode par defaut avec le
            # codepage de la console (cp1252), pas UTF-8, sauf si on le lui dit
            # explicitement. Resultat : "e" accentue (2 octets UTF-8, dont le
            # premier vaut 0xC3) se fait redecoder en cp1252 en "A tilde" (Ã),
            # d'ou "a tilde" prononce a voix haute, et "©" pour d'autres
            # sequences mal redecodees -- symptome classique du mojibake
            # UTF-8/cp1252. On force donc PYTHONUTF8/PYTHONIOENCODING dans
            # l'environnement du sous-processus pour que SA lecture soit elle
            # aussi en UTF-8.
            env = dict(os.environ)
            env["PYTHONUTF8"] = "1"
            env["PYTHONIOENCODING"] = "utf-8"
            subprocess.run(
                [sys.executable, "-m", "piper", "-m", str(model_path), "-f", str(out_path),
                 "--sentence-silence", "0.2"],
                input=text, check=True, capture_output=True, text=True, timeout=120,
                encoding="utf-8", env=env,
            )
            if out_path.exists() and out_path.stat().st_size > 0:
                return True
            last_err = "fichier de sortie vide ou absent"
        except subprocess.CalledProcessError as e:
            # La ligne utile (nom de l'exception) est en fin de traceback, pas
            # au debut (qui n'est que le boilerplate "frozen runpy") -- on
            # affiche donc les 300 derniers caracteres, pas les 200 premiers.
            last_err = e.stderr.strip()[-300:]
        except subprocess.TimeoutExpired:
            last_err = f"timeout ({attempt+1}/3)"
        if attempt < 2:
            time.sleep(1.5)
    print(f"  ! piper a echoue apres 3 essais ({model_path.name}): {last_err}", file=sys.stderr)
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


def resolve_fr_voice_label(voice_en: str, piper_voices: dict, fr_tts_mode: str, mbrola_ok: dict) -> str:
    """Comme synth_fr_any() mais sans synthétiser -- juste le libellé de voix
    qui SERAIT utilisé, pour renseigner le manifest sur un fichier déjà
    présent (non régénéré faute de --force)."""
    piper_name = EN_TO_PIPER_VOICE.get(voice_en, "fr_FR-siwis-medium")
    if fr_tts_mode in ("auto", "piper") and piper_name in piper_voices:
        return piper_name
    return resolve_fr_voice(voice_en, mbrola_ok)


def synth_fr_any(text_fr: str, voice_en: str, out_path: Path, piper_voices: dict,
                  fr_tts_mode: str, mbrola_ok: dict):
    """Point d'entrée unique pour le doublage français, qui choisit entre
    Piper (neuronal, prioritaire) et l'ancien espeak-ng/mbrola selon ce qui
    est disponible et --fr-tts. Retourne (succès, libellé_voix_pour_manifest)."""
    piper_name = EN_TO_PIPER_VOICE.get(voice_en, "fr_FR-siwis-medium")
    use_piper = fr_tts_mode in ("auto", "piper") and piper_name in piper_voices
    if fr_tts_mode == "piper" and piper_name not in piper_voices:
        print(f"  ! --fr-tts=piper mais la voix {piper_name} est introuvable dans "
              f"--piper-voices-dir -- voir docstring \"Activer Piper\"", file=sys.stderr)
    if use_piper:
        ok = synth_fr_piper(text_fr, piper_voices[piper_name], out_path)
        return ok, piper_name
    voice_fr = resolve_fr_voice(voice_en, mbrola_ok)
    ok = synth_fr(text_fr, voice_fr, out_path)
    return ok, voice_fr


def process_battle_intros(scenarios_dir, out_dir, mbrola_ok, piper_voices, args):
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

            if args.lang in ("fr", "both") and text_fr and has_speakable_content(text_fr):
                rel_fr = f"{sc_id}_fr/intro_{idx:03d}_{slug}.wav"
                out_path_fr = out_dir / rel_fr
                if args.force or not out_path_fr.exists():
                    ok, voice_fr = synth_fr_any(text_fr, voice_en, out_path_fr, piper_voices,
                                                 args.fr_tts, mbrola_ok)
                    if ok:
                        n_gen_fr += 1
                        print(f"  [fr] {rel_fr} (voix={voice_fr})")
                    else:
                        continue
                else:
                    voice_fr = resolve_fr_voice_label(voice_en, piper_voices, args.fr_tts, mbrola_ok)
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
    ap.add_argument("--piper-voices-dir",
                     default=str(Path(__file__).resolve().parent / "voices"),
                     help="Dossier contenant les modeles Piper (.onnx + .onnx.json) telecharges "
                          "via `python -m piper.download_voices fr_FR-...` -- voir docstring "
                          "\"Activer Piper\" en tete de fichier. Par defaut : voices/ a cote de "
                          "ce script.")
    ap.add_argument("--fr-tts", choices=["auto", "piper", "mbrola"], default="auto",
                     help="Backend de synthese francaise : auto (Piper si un modele est trouve, "
                          "sinon espeak-ng/mbrola -- par defaut), piper (force Piper, erreur si "
                          "le modele manque), mbrola (force l'ancien backend espeak-ng/mbrola "
                          "meme si Piper est disponible).")
    args = ap.parse_args()

    # flite (anglais) et espeak-ng/mbrola (repli francais) ne sont requis que
    # pour la langue effectivement demandee -- --lang fr avec Piper installe
    # n'a besoin ni de l'un ni de l'autre (bug corrige : avant, les deux
    # etaient exiges inconditionnellement meme en --lang fr).
    if args.lang in ("en", "both") and not shutil.which("flite"):
        print("ERREUR : flite introuvable (apt-get install flite)", file=sys.stderr)
        sys.exit(1)

    piper_voices = find_piper_voices(Path(args.piper_voices_dir))

    need_mbrola_fallback = args.lang in ("fr", "both") and (
        args.fr_tts == "mbrola" or (args.fr_tts == "auto" and not piper_voices)
    )
    if need_mbrola_fallback and not shutil.which("espeak-ng"):
        print("ERREUR : espeak-ng introuvable (apt-get install espeak-ng)", file=sys.stderr)
        sys.exit(1)

    # Calcule toujours mbrola_ok (verification sans effet de bord, cf.
    # has_mbrola_voice -- renvoie juste des False si espeak-ng est absent) :
    # utilise par resolve_fr_voice*() plus bas, meme si --lang fr + Piper
    # n'en aura pas besoin en pratique.
    mbrola_ok = {"mb-fr1": has_mbrola_voice("mb-fr1"), "mb-fr4": has_mbrola_voice("mb-fr4")}
    if need_mbrola_fallback:
        for name, ok in mbrola_ok.items():
            state = "disponible" if ok else "absente -> repli sur fr-fr (apt-get install mbrola-fr1 mbrola-fr4 pour l'activer)"
            print(f"Voix mbrola {name} : {state}")

    if piper_voices:
        print(f"Voix Piper trouvees dans {args.piper_voices_dir} : {', '.join(sorted(piper_voices))}")
    else:
        print(f"Aucune voix Piper dans {args.piper_voices_dir} -> repli sur espeak-ng/mbrola pour le "
              f"francais (voir docstring \"Activer Piper\" en tete de fichier pour une bien meilleure "
              f"qualite).")

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
            if args.lang in ("fr", "both") and text_fr.strip() and has_speakable_content(text_fr):
                rel_fr = f"{sc_id}_fr/{idx:03d}_{slug}.wav"
                out_path_fr = out_dir / rel_fr
                if args.force or not out_path_fr.exists():
                    ok, voice_fr = synth_fr_any(text_fr, voice_en, out_path_fr, piper_voices,
                                                 args.fr_tts, mbrola_ok)
                    if ok:
                        n_gen_fr += 1
                        print(f"  [fr] {rel_fr} (voix={voice_fr})")
                    else:
                        continue
                else:
                    voice_fr = resolve_fr_voice_label(voice_en, piper_voices, args.fr_tts, mbrola_ok)
                    n_skip += 1
                if out_path_fr.exists():
                    lines.append({
                        "scenario": sc_id, "beat_index": idx, "speaker": speaker,
                        "lang": "fr", "voice": voice_fr, "text": text_fr,
                        "file": rel_fr, "bytes": out_path_fr.stat().st_size,
                    })

    if args.battle_scenarios_dir:
        bl, be, bf, bs = process_battle_intros(Path(args.battle_scenarios_dir), out_dir, mbrola_ok, piper_voices, args)
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
        "fr_voice_map_used": {k: resolve_fr_voice_label(k, piper_voices, args.fr_tts, mbrola_ok)
                               for k in set(VOICE_MAP_EN.values())},
        "lines": merged_lines,
    }
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")

    print(f"\nTermine : {n_gen_en} fichier(s) anglais generes, {n_gen_fr} fichier(s) francais generes, "
          f"{n_skip} deja presents (non regeneres, utiliser --force sinon).")
    print(f"Manifest ecrit : {manifest_path} ({len(merged_lines)} entrees au total, fusionne avec l'existant)")


if __name__ == "__main__":
    main()
