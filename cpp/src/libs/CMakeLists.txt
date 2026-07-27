# Two containers, and the split is the naming decision: module names come from the
# path under src/libs, so everything under cup/ is `cup.<name>` in `cup::<name>`,
# and everything under utils/ is `utils` or `utils.<name>` in `utils::`.
#
# What goes where is what the code names. cup/ knows about projects, templates,
# compilers and the terminal; utils/ holds the object-lifetime patterns and the
# error type, neither of which names a cup concept — a locator keyed on typeid and
# an E carrying a message are the same in any program.
#
# utils/ is first because the dependency runs one way: every library under cup/
# links utils.error, and nothing under utils/ links anything under cup/.
add_subdirectory(utils)
add_subdirectory(cup)
