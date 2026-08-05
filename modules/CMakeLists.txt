# ---------------------------------------------------------------------------
# Koral's own modules: optional engine features, each a shared library loaded
# at runtime when a project's koral.json asks for it (see include/module.h).
#
# Every module lands in <build>/modules — which is <libKoral's dir>/modules,
# the first place the loader looks — so a dev build finds them with no
# configuration at all.
# ---------------------------------------------------------------------------

add_subdirectory(camera)
add_subdirectory(gui-extras)
add_subdirectory(mesh)
# Image IO is three modules, not one: reading files, writing them, and the compression step that
# turns an ordinary image into a GPU-ready KTX2. A project links whichever of them it uses.
add_subdirectory(image-import)
add_subdirectory(image-export)
add_subdirectory(image-compress)
add_subdirectory(model-import)
