#pragma once
namespace wesnoth_sg {
enum class SceneId { STORY, BATTLE, QUIT };
class SceneManager;
struct Scene { virtual void enter(){} virtual void update(SceneManager&)=0; virtual void render()=0; virtual ~Scene(){} };
class SceneManager {
public:
  void set(SceneId id);
  void update(){ if(s_) s_->update(*this); }
  void render(){ if(s_) s_->render(); }
  SceneId current() const { return cur_; }
private:
  SceneId cur_=SceneId::STORY; Scene* s_=nullptr;
};
}
