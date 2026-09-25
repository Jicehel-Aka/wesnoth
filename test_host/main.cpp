#include <cstdio>
#include "../main/story/campaign_loader.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s campaign.json manifest.json\n", argv[0]);
        return 1;
    }
    Campaign campaign;
    bool ok = campaign_load(argv[1], argv[2], &campaign);
    if (!ok) {
        fprintf(stderr, "ECHEC campaign_load\n");
        return 1;
    }
    int total_beats = 0;
    int with_audio = 0;
    for (auto& sc : campaign.scenarios) {
        printf("=== %s (%s) -> next=%s, %zu beats ===\n",
               sc.id.c_str(), sc.name.c_str(), sc.next_scenario.c_str(), sc.beats.size());
        for (auto& b : sc.beats) {
            total_beats++;
            if (!b.audio_path.empty()) with_audio++;
        }
    }
    printf("\nTotal: %d beats, %d avec audio associe\n", total_beats, with_audio);

    // Vérifie le premier et un beat au hasard pour inspection manuelle
    if (!campaign.scenarios.empty() && !campaign.scenarios[0].beats.empty()) {
        auto& b = campaign.scenarios[0].beats[0];
        printf("\nPremier beat: speaker='%s' en='%.60s' fr='%.60s' has_fr=%d audio='%s'\n",
               b.speaker.c_str(), b.text_en.c_str(), b.text_fr.c_str(), b.has_fr, b.audio_path.c_str());
    }
    return 0;
}
