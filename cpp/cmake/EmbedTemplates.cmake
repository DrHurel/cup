# Template embedding for cup — the one piece of build machinery cup cannot
# scaffold for itself.
#
# The Go implementation compiles its template corpus in with `//go:embed
# all:files`. C++ has no such facility, so this module generates an equivalent:
# a header + source pair exposing every file under a template directory as
# compile-time std::string_view constants with a lookup table.
#
#   include(cmake/EmbedTemplates.cmake)
#   cup_embed_templates(TARGET cup_templates DIR ${CMAKE_CURRENT_SOURCE_DIR}/templates)
#   target_link_libraries(cup_tmpl PRIVATE cup_templates)
#
# The corpus layout is identical to the Go implementation's internal/tmpl/files/,
# so the templates themselves port unchanged and the generated paths match the
# ones the Go code already uses.

include_guard(GLOBAL)

# cup_embed_templates(TARGET <name> DIR <template-root>)
#
# Defines a static library <name> carrying the embedded corpus, with its generated
# include directory exposed PUBLIC so consumers can #include <cup/embedded_templates.hpp>.
#
# Regeneration is wired both ways: the custom command re-runs when any template's
# *content* changes (DEPENDS), and CONFIGURE_DEPENDS re-globs at build time so
# adding or deleting a template is picked up without a manual re-configure.
function(cup_embed_templates)
  cmake_parse_arguments(PARSE_ARGV 0 ARG "" "TARGET;DIR" "")

  if(NOT ARG_TARGET)
    message(FATAL_ERROR "cup_embed_templates: TARGET is required")
  endif()
  if(NOT ARG_DIR)
    message(FATAL_ERROR "cup_embed_templates: DIR is required")
  endif()
  if(NOT IS_DIRECTORY "${ARG_DIR}")
    message(FATAL_ERROR "cup_embed_templates: DIR '${ARG_DIR}' is not a directory")
  endif()
  if(ARG_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "cup_embed_templates: unexpected arguments: ${ARG_UNPARSED_ARGUMENTS}")
  endif()

  file(GLOB_RECURSE _templates CONFIGURE_DEPENDS "${ARG_DIR}/*")
  if(NOT _templates)
    message(FATAL_ERROR "cup_embed_templates: no templates found under ${ARG_DIR}")
  endif()
  list(LENGTH _templates _count)

  set(_generator "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/GenerateEmbeddedTemplates.cmake")
  set(_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/${ARG_TARGET}")
  # The header sits under a cup/ prefix so consumers spell it
  # #include <cup/embedded_templates.hpp>; _header_include is that same spelling,
  # which the generated source uses to include its own header.
  set(_header_include "cup/embedded_templates.hpp")
  set(_header "${_gen_dir}/${_header_include}")
  set(_source "${_gen_dir}/embedded_templates.cpp")

  add_custom_command(
    OUTPUT "${_header}" "${_source}"
    COMMAND "${CMAKE_COMMAND}"
            -D "CUP_TEMPLATE_DIR=${ARG_DIR}"
            -D "CUP_HEADER_OUT=${_header}"
            -D "CUP_SOURCE_OUT=${_source}"
            -D "CUP_HEADER_INCLUDE=${_header_include}"
            -P "${_generator}"
    DEPENDS ${_templates} "${_generator}"
    COMMENT "cup: embedding ${_count} templates from ${ARG_DIR}"
    VERBATIM)

  add_library(${ARG_TARGET} STATIC "${_source}" "${_header}")
  target_include_directories(${ARG_TARGET} PUBLIC "${_gen_dir}")
  # The corpus is plain C++20 data; consumers may be built at a newer standard.
  target_compile_features(${ARG_TARGET} PUBLIC cxx_std_20)
endfunction()
