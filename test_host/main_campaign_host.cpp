// main_campaign_host.cpp — enchaînement complet sans écran : récit du
// scénario 1 -> bataille (événements, dialogues) -> fin -> scénario suivant.
// WSG_AUTOPLAY=1 : l'IA joue aussi pour le joueur. Une image sur N exportée.
#include "scene/scene.h"
#include "platform/gb_port.h"
#include <cstdio>
#include <cstdlib>
#include <functional>

void host_set_budget(int n);
void host_set_dump_dir(const char* dir);
namespace gb { void host_set_dump_every(int n); }
void host_set_input_script(std::function<uint32_t(int)> fn);

int main(int argc, char** argv) {
    host_set_budget(argc > 1 ? atoi(argv[1]) : 3000);
    host_set_dump_dir(argc > 2 ? argv[2] : "");
    gb::host_set_dump_every(argc > 3 ? atoi(argv[3]) : 50);
    host_set_input_script([](int f) -> uint32_t { return (f % 4 == 0) ? gb::BTN_A : 0u; });
    wesnoth_sg::SceneManager mgr;
    mgr.set(wesnoth_sg::SceneId::STORY);
    wesnoth_sg::SceneId last = mgr.current();
    int f = 0;
    while (gb::running()) {
        gb::frame_begin();
        mgr.update();
        if (mgr.current() != last) {
            printf("[image %d] scène -> %s\n", f, mgr.current() == wesnoth_sg::SceneId::STORY ? "RÉCIT"
                   : mgr.current() == wesnoth_sg::SceneId::BATTLE ? "BATAILLE" : "FIN");
            last = mgr.current();
        }
        if (mgr.current() == wesnoth_sg::SceneId::QUIT) break;
        mgr.render();
        gb::frame_end();
        ++f;
    }
    printf("Fin après %d images\n", f);
    return 0;
}
