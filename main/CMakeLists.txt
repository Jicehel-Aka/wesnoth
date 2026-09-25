idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES gamebuino aka_runtime wesnoth_sg
    PRIV_REQUIRES app_update)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=reorder -Wno-error=switch -Wno-error=unused-variable
    -Wno-error=type-limits -Wno-error=narrowing -Wno-error=parentheses
    -Wno-error=maybe-uninitialized -Wno-error=unused-function -Wno-error=missing-field-initializers
    -Wno-error=unused-value -Wno-error=char-subscripts -Wno-error=unused-parameter
    -Wno-error=sign-compare -Wno-error=misleading-indentation)
