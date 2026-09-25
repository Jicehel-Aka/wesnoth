#include "wesnoth_app.h"
#include "scene/scene.h"
#include "platform/gb_port.h"

namespace wesnoth_sg {
void run() {
    SceneManager mgr;
    mgr.set(SceneId::STORY);
    while (gb::running()) {
        gb::frame_begin();
        mgr.update();
        if (mgr.current() == SceneId::QUIT) break;
        mgr.render();
        gb::frame_end();
    }
}
}  // namespace wesnoth_sg
