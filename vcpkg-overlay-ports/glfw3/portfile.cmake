if (VCPKG_TARGET_IS_EMSCRIPTEN)
    # emscripten has built-in glfw3 library
    set(VCPKG_BUILD_TYPE release)
    file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/glfw3Config.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/glfw3")
    set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
    return()
endif()

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO glfw/glfw
    REF ${VERSION}
    SHA512 39ad7a4521267fbebc35d2ff0c389a56236ead5fa4bdff33db113bd302f70f5f2869ff4e6db1979512e1542813292dff5a482e94dfce231750f0746c301ae9ed
    HEAD_REF master
    PATCHES
        # Koral overlay: on X11, GLFW chooses a visual with alpha only for a window that has an
        # OpenGL context. A Vulkan one (GLFW_NO_API) got the default visual whatever
        # GLFW_TRANSPARENT_FRAMEBUFFER said, so no Vulkan window could be seen through there.
        x11-transparent-without-client-api.patch
        # Koral overlay: on Wayland only the compositor moves or resizes a window, when asked with the
        # press that is still down, and GLFW asks only for its own decorations. A window that draws its
        # own title bar and edges has glfwKoralWaylandMoveResize to ask with.
        wayland-move-resize.patch
)

if(VCPKG_TARGET_IS_LINUX)
    message(
"GLFW3 currently requires the following libraries from the system package manager:
    xinerama
    xcursor
    xorg
    libglu1-mesa
    pkg-config

These can be installed on Ubuntu systems via sudo apt install libxinerama-dev libxcursor-dev xorg-dev libglu1-mesa-dev pkg-config

Alternatively, when targeting the Wayland display server, use the packages listed in the GLFW documentation here:

https://www.glfw.org/docs/3.3/compile.html#compile_deps_wayland")
endif()

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
    wayland         GLFW_BUILD_WAYLAND
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DGLFW_BUILD_EXAMPLES=OFF
        -DGLFW_BUILD_TESTS=OFF
        -DGLFW_BUILD_DOCS=OFF
        ${FEATURE_OPTIONS}
    MAYBE_UNUSED_VARIABLES
        GLFW_USE_WAYLAND
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/glfw3)

vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.md")
