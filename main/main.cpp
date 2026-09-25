// main.cpp — Point d'entrée device. Patron identique à main.cpp d'Asteria.
#if defined(ESP_PLATFORM)
#include "gb_core.h"
#include "gb_graphics.h"
#include "gb_audio_player.h"
#include "core/input.h"
#include "aka_runtime/aka_runtime.h"
#include "wesnoth_app.h"
#include "platform/gb_port.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Instances globales uniques, partagées avec aka_runtime, input et
// platform/gb_port_aka.cpp / audio/story_audio.cpp.
gb_core         g_core;
gb_graphics     gfx;
gb_audio_player g_audio_player;

static void audio_mix_task(void*) {
    for (;;) {
        g_audio_player.pool();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

extern "C" void app_main(void) {
    g_core.init();
    gfx.set_backlight_percent(80);
    gfx.set_refresh_rate(40);   // narratif, pas besoin du taux max
    input_init();

    // Identifiant du jeu -- doit correspondre EXACTEMENT (SD sensible à la
    // casse) partout où /sdcard/WESNOTH_SG/... est référencé (voir
    // scene/story_scene.cpp).
    akaRuntime.begin("WESNOTH_SG");

    static auto applyVolume = [](uint8_t musicVol, uint8_t /*sfxVol*/) {
        g_audio_player.set_master_volume((uint8_t)((uint16_t)musicVol * 200 / 100));
    };
    akaRuntime.setVolumeChangedCallback(applyVolume);

    static const char* const kControls[] = {
        "CTRL_ADVANCE",   // A : avancer le dialogue/l'écran narratif
        "CTRL_MENU_SHORT",
        "CTRL_QUIT",
        nullptr
    };
    akaRuntime.setControlsKeys(kControls);
    akaRuntime.setCredits("The South Guard (portage)", "Wesnoth Project / portage AKA",
                           "GPLv2 (moteur/scenario), CC-BY-SA (art)",
                           "https://www.wesnoth.org");

    g_audio_player.set_master_volume((uint8_t)((uint16_t)akaRuntime.getMusicVolume() * 200 / 100));
    xTaskCreatePinnedToCore(audio_mix_task, "AudioMixTask", 4096, nullptr, 5, nullptr, 1);

    wesnoth_sg::run();

    gb::return_to_loader();
    while (true) {
        input_poll(g_keys);
        akaRuntime.update(g_keys);
        vTaskDelay(pdMS_TO_TICKS(16));
    }
}
#endif
