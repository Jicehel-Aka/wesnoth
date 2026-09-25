// main_battle_host.cpp — joue le scénario 1 dans le banc hôte (sans écran),
// avec un script de touches : recrutement, sélection, déplacement, fin de
// tour, puis tours de l'IA. Les images sont exportées en PPM.
#include "scene/scene.h"
#include "platform/gb_port.h"
#include <cstdio>
#include <cstdlib>
#include <functional>

void host_set_budget(int n);
void host_set_dump_dir(const char* dir);
void host_set_input_script(std::function<uint32_t(int)> fn);

int main(int argc, char** argv) {
    int budget = argc > 1 ? atoi(argv[1]) : 200;
    host_set_budget(budget);
    host_set_dump_dir(argc > 2 ? argv[2] : ".");
    host_set_input_script([](int f) -> uint32_t {
        switch (f) {
            case 2: return gb::BTN_MENU;              // menu
            case 4: return gb::BTN_DOWN;              // -> Recruter
            case 6: return gb::BTN_A;
            case 8: return gb::BTN_A;                 // Spearman
            case 10: return gb::BTN_MENU;
            case 12: return gb::BTN_DOWN;
            case 14: return gb::BTN_A;
            case 16: return gb::BTN_DOWN;             // Bowman
            case 18: return gb::BTN_A;
            case 20: return gb::BTN_R1;               // unité suivante (Deoran)
            case 22: case 24: case 26: case 28: return gb::BTN_DOWN;
            case 30: return gb::BTN_A;                // déplacement
            case 34: return gb::BTN_MENU;
            case 36: return gb::BTN_A;                // fin de tour
            default: {
                // ensuite : fin de tour dès que c'est au joueur (MENU puis A)
                if (f > 40 && f % 40 == 0) return gb::BTN_MENU;
                if (f > 40 && f % 40 == 2) return gb::BTN_A;
                return 0u;
            }
        }
    });
    wesnoth_sg::SceneManager mgr;
    mgr.set(wesnoth_sg::SceneId::BATTLE);
    while (gb::running()) {
        gb::frame_begin();
        mgr.update();
        if (mgr.current() == wesnoth_sg::SceneId::QUIT) break;
        mgr.render();
        gb::frame_end();
    }
    printf("Test bataille termine\n");
    return 0;
}
