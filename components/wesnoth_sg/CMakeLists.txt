# Composant ESP-IDF wesnoth_sg (portage "The South Guard" sur AKA).
# platform/gb_port_host.cpp est volontairement ABSENT de cette liste : c'est
# le mock de test hôte (PC), jamais compilé pour la cible ESP32.
idf_component_register(
  SRCS
    "wesnoth_app.cpp"
    "scene/scene.cpp"
    "scene/story_scene.cpp"
    "scene/battle_scene.cpp"
    "battle/wdata.cpp"
    "battle/game.cpp"
    "battle/ai.cpp"
    "battle/events.cpp"
    "battle/campaign_state.cpp"
    "audio/story_audio.cpp"
    "story/campaign_loader.cpp"
    "platform/gb_port_common.cpp"
    "platform/gb_port_aka.cpp"
  INCLUDE_DIRS "."
  REQUIRES gamebuino aka_runtime aka_font json
)

# Meme liste de tolerances que les autres jeux AKA de ce studio (cf.
# components/asteria/CMakeLists.txt) -- necessaire pour ne pas transformer
# des warnings de style en erreurs bloquantes de build.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=reorder -Wno-error=switch -Wno-error=unused-variable
    -Wno-error=type-limits -Wno-error=narrowing -Wno-error=parentheses
    -Wno-error=maybe-uninitialized -Wno-error=unused-function -Wno-error=missing-field-initializers
    -Wno-error=unused-value -Wno-error=char-subscripts -Wno-error=unused-parameter
    -Wno-error=sign-compare -Wno-error=misleading-indentation -Wno-error=return-type
    -Wno-error=ignored-qualifiers)
