# ---------------------------------------------------------------------------
# The interface module: koral.ui (namespace kui)
#
# A retained-mode UI in three layers — element shaders, a canvas recording
# pictures, and declarative widgets — drawn by a frame-graph pass. Linked by
# whoever uses it, like every module; a project that does not link it pays
# nothing for it.
# ---------------------------------------------------------------------------

add_library(koral-ui SHARED
        src/export.cpp
        src/canvas.cpp
        src/path.cpp
        src/clean.cpp
        src/renderer.cpp
        src/text.cpp
        src/rendering.cpp
        src/elements.cpp
        src/boxes.cpp
        src/modifiers.cpp
        src/controls.cpp
        src/more.cpp
        src/compose.cpp
        src/ui.cpp
        src/list.cpp
        src/dock.cpp
        src/capi.cpp
)

target_include_directories(koral-ui PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/modules/koral-ui>)

target_compile_definitions(koral-ui PRIVATE KORAL_UI_EXPORTS)

target_link_libraries(koral-ui PUBLIC Koral)

# Clipper2 makes paths clean before they are tessellated: overlapping contours united under their fill
# rule, strokes offset into outlines with real joins and caps. Static and private: nothing of it shows.
find_package(Clipper2 CONFIG REQUIRED)
target_link_libraries(koral-ui PRIVATE Clipper2::Clipper2)

# glm appears in the public headers but reaches consumers through Koral, as for the other modules.
# stb_truetype is header-only and compiled into text.cpp.
target_link_libraries(koral-ui PRIVATE glm::glm spdlog::spdlog)

set_target_properties(koral-ui PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules)

if(MSVC)
    # No manifest of its own. With one, Windows resolves what the module imports by the module's manifest alone;
    # without, by whoever loads it — which is how C# has it bind to the engine and not to its own Koral.dll, the
    # managed assembly of the same name that .NET has loaded first (see NativeLibraryResolver.LoadBesideKoral).
    target_link_options(koral-ui PRIVATE /MANIFEST:NO)
endif()

if(APPLE)
    set_target_properties(koral-ui PROPERTIES INSTALL_RPATH "@loader_path/..")
elseif(UNIX)
    set_target_properties(koral-ui PROPERTIES INSTALL_RPATH "$ORIGIN/..")
endif()

# The UI's shaders, and the headers element shaders include, must be findable at runtime: staged
# beside libKoral for a dev build (its shaders/ is a search root), installed for the SDK. Staged by a
# target of their own that depends on the files, so editing only a shader still stages it.
file(GLOB KORAL_UI_SHADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*)
set(KORAL_UI_SHADER_STAMP ${CMAKE_CURRENT_BINARY_DIR}/shaders.stamp)
add_custom_command(OUTPUT ${KORAL_UI_SHADER_STAMP}
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:Koral>/shaders
        COMMAND ${CMAKE_COMMAND} -E touch ${KORAL_UI_SHADER_STAMP}
        DEPENDS ${KORAL_UI_SHADERS}
        COMMENT "Staging the UI module's shaders where the shader search roots look")
add_custom_target(koral-ui-shaders DEPENDS ${KORAL_UI_SHADER_STAMP})
add_dependencies(koral-ui koral-ui-shaders)

install(TARGETS koral-ui
        EXPORT KoralTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/modules
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/modules
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/modules/koral-ui
        FILES_MATCHING PATTERN "*.h")
install(DIRECTORY shaders/ DESTINATION ${CMAKE_INSTALL_DATADIR}/Koral/shaders)
