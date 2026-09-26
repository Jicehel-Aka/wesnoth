#!/bin/bash
# build_pc.sh — Compile la version PC/Unix jouable (SDL2, temps réel).
#
# Prérequis sur la machine de build (PAS présents dans le bac à sable où ce
# projet a été écrit -- seul le mock hôte a pu y être testé) :
#   - libsdl2-dev      (Debian/Ubuntu: apt install libsdl2-dev)
#   - libcjson-dev      (apt install libcjson-dev)
#   - le fichier pkg-config sdl2.pc disponible (fourni par libsdl2-dev)
#
# Usage:
#   ./build_pc.sh
#   ./wesnoth_sg_pc
#
# Contrôles : flèches/ZQSD = déplacement (inutilisé pour l'instant, mode
# narratif pur) -- Espace/Entrée/X = avancer le dialogue -- Échap = quitter.
set -e
cd "$(dirname "$0")"

g++ -std=c++17 -Wno-narrowing -O2 -I pc/include \
    $(pkg-config --cflags libcjson 2>/dev/null || echo -I/usr/include/cjson) \
    -I components/wesnoth_sg -I components/aka_font/include \
    components/wesnoth_sg/wesnoth_app.cpp \
    components/wesnoth_sg/scene/scene.cpp \
    components/wesnoth_sg/scene/story_scene.cpp \
    components/wesnoth_sg/scene/battle_scene.cpp \
    components/wesnoth_sg/battle/wdata.cpp \
    components/wesnoth_sg/battle/game.cpp \
    components/wesnoth_sg/battle/ai.cpp \
    components/wesnoth_sg/battle/events.cpp \
    components/wesnoth_sg/battle/campaign_state.cpp \
    components/wesnoth_sg/audio/story_audio_sdl.cpp \
    components/wesnoth_sg/platform/gb_port_sdl.cpp \
    components/wesnoth_sg/platform/gb_port_common.cpp \
    components/wesnoth_sg/story/campaign_loader.cpp \
    pc/main_sdl.cpp \
    $(pkg-config --cflags --libs sdl2 2>/dev/null || echo -lSDL2) \
    -lcjson \
    -o wesnoth_sg_pc

echo "Build OK -> ./wesnoth_sg_pc"
echo "Assure-toi que ./sdcard_files/WESNOTH_SG/ contient campaign_bilingual.json, manifest.json et audio/ (voir SD_files_WESNOTH_SG.zip)."
