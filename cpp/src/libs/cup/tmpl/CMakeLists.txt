add_library(tmpl)
target_sources(tmpl
    PUBLIC
        FILE_SET CXX_MODULES FILES
            tmpl.cppm
            corpus.cppm
            resolve.cppm
)
target_compile_features(tmpl PUBLIC cxx_std_23)
# cup scaffolds targets but does not wire dependencies between them, so module
# imports are linked by hand. cup_templates is the generated corpus (see
# cmake/EmbedTemplates.cmake), linked PRIVATE because :corpus includes its header
# in a global module fragment and re-exports none of it; utils.error supplies the E
# of the std::expected results.
target_link_libraries(tmpl PUBLIC error PRIVATE cup_templates)
