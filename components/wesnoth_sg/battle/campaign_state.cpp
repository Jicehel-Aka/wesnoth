// battle/campaign_state.cpp — voir campaign_state.h ; ne fait que fournir
// l'unique instance (singleton à la première utilisation, cf. Meyers) de
// CampaignState partagée par StoryScene, BattleScene et le moteur d'action
// (events.cpp/ai.cpp) pour tout ce qui doit survivre entre deux scénarios
// ou être lu sans référence directe à une scène (ex. wsg::campaign_state().lang_fr).
#include "battle/campaign_state.h"

namespace wsg {

CampaignState& campaign_state() {
    static CampaignState s;
    return s;
}

}  // namespace wsg
