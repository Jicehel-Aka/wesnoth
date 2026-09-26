// Point d'entrée natif : on fournit notre propre main(), sans passer par le
// SDL_main de libSDL2main (qui exige un point d'entrée nommé SDL_main --
// source d'un échec de lien sous Windows/MinGW : "undefined reference to
// SDL_main"). SDL_MAIN_HANDLED l'indique explicitement à SDL2, au cas où un
// fichier de ce binaire viendrait à inclure SDL.h directement : la macro qui
// renomme main en SDL_main ne doit jamais s'appliquer ici. Le lien évite par
// ailleurs libSDL2main via pkg-config plutôt que sdl2-config (voir
// build_pc.sh et .github/workflows/build-pc.yml).
#define SDL_MAIN_HANDLED
#include "wesnoth_app.h"
int main(){ wesnoth_sg::run(); return 0; }
