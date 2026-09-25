// main_host.cpp — lance le vrai moteur wesnoth_sg::run() sur PC, via le
// mock platform/gb_port_host.cpp. Dump des frames en PPM pour inspection.
#include "wesnoth_app.h"
#include "platform/gb_port.h"
#include <cstdio>
#include <functional>

void host_set_budget(int n);
void host_set_dump_dir(const char* dir);
void host_set_input_script(std::function<uint32_t(int)> fn);

int main(int argc, char** argv) {
    int budget = argc > 1 ? atoi(argv[1]) : 8;
    const char* dump_dir = argc > 2 ? argv[2] : ".";
    host_set_budget(budget);
    host_set_dump_dir(dump_dir);
    // Script : appuie sur A toutes les 2 frames pour avancer les beats.
    host_set_input_script([](int f) -> uint32_t {
        return (f % 2 == 0) ? gb::BTN_A : 0u;
    });

    wesnoth_sg::run();

    printf("Test hote termine : %d frames dumpees dans %s\n", budget, dump_dir);
    return 0;
}
