// scene/scene.h — machine à états minimale entre l'écran de récit
// (StoryScene, story_scene.h) et l'écran de bataille (BattleScene,
// battle_scene.h) ; les deux sont des singletons (story_scene()/battle_scene(),
// voir leurs .h respectifs), SceneManager ne fait que garder un pointeur
// courant et lui déléguer update()/render() -- pas de pile, un seul écran
// actif à la fois (QUIT = aucun, la boucle principale s'arrête).
#pragma once
namespace wesnoth_sg {
enum class SceneId { STORY, BATTLE, QUIT };
class SceneManager;
struct Scene { virtual void enter(){} virtual void update(SceneManager&)=0; virtual void render()=0; virtual ~Scene(){} };
class SceneManager {
public:
  void set(SceneId id);          // change l'écran actif (voir scene.cpp) et appelle enter()
  void update(){ if(s_) s_->update(*this); }
  void render(){ if(s_) s_->render(); }
  SceneId current() const { return cur_; }
private:
  SceneId cur_=SceneId::STORY; Scene* s_=nullptr;
};
}
