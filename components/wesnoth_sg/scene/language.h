// scene/language.h — langue d'affichage/narration, partagée entre StoryScene
// (récit) et BattleScene (dialogues joués pendant la bataille). Un seul enum
// pour éviter deux définitions qui divergeraient.
#pragma once

namespace wesnoth_sg {

enum class Language { French, English };

}  // namespace wesnoth_sg
