# Container for cup's own libraries. It defines no target of its own — cup derives
# module names from the path under src/libs, so nesting the libraries here is what
# gives them their `cup.` prefix (src/libs/cup/ui -> module cup.ui, namespace
# cup::ui). `cup add lib` appends each child below.
#
# error is not here: it moved to src/libs/utils/error (module utils.error) with the
# rest of the code that names no cup concept. Every library below still links it.
add_subdirectory(platform)
add_subdirectory(ui)
add_subdirectory(tmpl)
add_subdirectory(project)
add_subdirectory(scaffold)
add_subdirectory(probe)
