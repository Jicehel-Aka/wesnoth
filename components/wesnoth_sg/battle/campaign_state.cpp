#include "battle/campaign_state.h"

namespace wsg {

CampaignState& campaign_state() {
    static CampaignState s;
    return s;
}

}  // namespace wsg
