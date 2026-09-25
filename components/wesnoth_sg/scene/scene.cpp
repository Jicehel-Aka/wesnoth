#include "scene/scene.h"
#include "scene/story_scene.h"
#include "scene/battle_scene.h"

namespace wesnoth_sg {

void SceneManager::set(SceneId id) {
    cur_ = id;
    switch (id) {
        case SceneId::STORY:
            s_ = &story_scene();
            s_->enter();
            break;
        case SceneId::BATTLE:
            s_ = &battle_scene();
            s_->enter();
            break;
        case SceneId::QUIT:
            s_ = nullptr;
            break;
    }
}

}  // namespace wesnoth_sg
