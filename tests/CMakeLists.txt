# ---------------------------------------------------------------------------
# GFX unit tests
#
# These are pure-logic, GPU-independent tests: they exercise the CPU-side
# algorithms and value types of the framework (the allocator, flag bitsets,
# the functional error model, vertex layout math, resource ownership) without
# needing a Vulkan device, window or swap chain.
#
# They link against the Koral shared library for the handful of symbols
# that are defined out-of-line (e.g. kor::describe / kor::Error::toString), and
# pull the public headers + generated api.h through its INTERFACE include dirs.
# ---------------------------------------------------------------------------
find_package(GTest CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

add_executable(koral_tests
        test_mesh_tlsf_allocator.cpp
        test_flags.cpp
        test_error.cpp
        test_log_repeat.cpp
        test_log_history.cpp
        test_structs.cpp
        test_image_format.cpp
        test_mesh_layout.cpp
        test_vertex_layout.cpp
        test_resource.cpp
        test_builder_recoverable.cpp
        test_project_config.cpp
        test_device_features.cpp
        test_module.cpp
        test_token.cpp
        test_task.cpp
        test_frame_graph_compiler.cpp
        test_reflect.cpp
        test_input_bindings.cpp
        test_debug_draw.cpp
        test_math.cpp
)

target_link_libraries(koral_tests PRIVATE
        Koral
        # The vertex-format layer lives in the mesh module now, so testing it means linking the
        # module exactly as a project would.
        koral-mesh
        GTest::gtest
        GTest::gtest_main
        magic_enum::magic_enum
        # Public GFX headers (mesh.h, buffer.h, context.h, log.h, ...) expose these
        # dependencies' headers directly, so consumers — including this test target —
        # need their include directories, exactly like Koral_Runtime does.
        spdlog::spdlog
        Vulkan::Vulkan
        # libKoral already carries GLFW and exports it. ELF folds a second static copy into that one, but
        # macOS's two-level namespace keeps both, and this binary's glfw* calls would reach a GLFW that
        # never initialised and owns none of the windows. So there it takes only the headers.
        $<$<NOT:$<PLATFORM_ID:Darwin>>:glfw>
)

# The public headers reach into a few private headers via relative paths
# (e.g. resource.h -> ../src/core/fileWatcher.h), so expose the source tree too.
target_include_directories(koral_tests PRIVATE
        $<$<PLATFORM_ID:Darwin>:$<TARGET_PROPERTY:glfw,INTERFACE_INCLUDE_DIRECTORIES>>
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include
        ${CMAKE_SOURCE_DIR})

include(GoogleTest)
gtest_discover_tests(koral_tests DISCOVERY_MODE PRE_TEST)

# ---------------------------------------------------------------------------
# Integration tests: these boot a real Vulkan device (through a window) and
# exercise GPU code paths. They need a GPU + display, so they live in a separate
# binary and skip gracefully at runtime when a device cannot be created.
# ---------------------------------------------------------------------------
option(KORAL_BUILD_INTEGRATION_TESTS "Build the GFX GPU integration tests" ON)
if(KORAL_BUILD_INTEGRATION_TESTS)
    add_subdirectory(integration)
endif()

# Windowed tests boot a real window + swap chain (needs a display). Separate opt-in
# so headless CI can build the rest without pulling in the presentation stack.
option(KORAL_BUILD_WINDOW_TESTS "Build the GFX windowed integration tests" ON)
if(KORAL_BUILD_WINDOW_TESTS)
    add_subdirectory(window)
endif()

# The C# bindings, end to end: scenes, passes, input, state, and scripts reloaded while they run. One
# process per case (Koral.Tests runs each in its own); no display needed.
if(KORAL_BUILD_DOTNET)
    add_custom_target(koral_dotnet_tests ALL
            COMMAND "${KORAL_DOTNET_EXECUTABLE}" build "${KORAL_DOTNET_DIR}/tests/Koral.Tests" -c Release --nologo
                    -o "${KORAL_DOTNET_OUT}/tests"
            WORKING_DIRECTORY "${KORAL_DOTNET_DIR}"
            USES_TERMINAL
            COMMENT "Building the C# tests")
    add_dependencies(koral_dotnet_tests koral_dotnet)
    add_test(NAME CSharp.Bindings COMMAND "${KORAL_DOTNET_EXECUTABLE}" "${KORAL_DOTNET_OUT}/tests/Koral.Tests.dll")
    set_tests_properties(CSharp.Bindings PROPERTIES
            ENVIRONMENT "KORAL_LIBRARY=$<TARGET_FILE:Koral>"
            TIMEOUT 600)

    # The C# enumerations and P/Invokes are generated from the C++ headers and koral_c.h: these fail when
    # one of those changed and the bindings were not generated again.
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME CSharp.EnumsMatchTheHeaders
                COMMAND "${Python3_EXECUTABLE}" "${KORAL_DOTNET_DIR}/tools/generate_enums.py" --check)
        add_test(NAME CSharp.InteropMatchesTheCInterface
                COMMAND "${Python3_EXECUTABLE}" "${KORAL_DOTNET_DIR}/tools/generate_native.py" --check)
    endif()
endif()

# The Kotlin bindings: Gradle runs their tests (each class in a JVM of its own) against this build's Koral,
# and the generated interop is checked against koral_c.h and koralUI_c.h like the C# one.
if(KORAL_BUILD_KOTLIN)
    set(KORAL_JBR "$ENV{KORAL_JBR}" CACHE PATH "A JetBrains Runtime 25: the Kotlin tests then cover structural hot reload too")
    add_test(NAME Kotlin.Bindings
            COMMAND "${KORAL_GRADLEW}" :tests:test --console=plain --rerun-tasks
            WORKING_DIRECTORY "${KORAL_KOTLIN_DIR}")
    set_tests_properties(Kotlin.Bindings PROPERTIES
            ENVIRONMENT "JAVA_HOME=${KORAL_JAVA_HOME};KORAL_JBR=${KORAL_JBR};KORAL_LIBRARY=$<TARGET_FILE:Koral>"
            TIMEOUT 900)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME Kotlin.InteropMatchesTheCInterface
                COMMAND "${Python3_EXECUTABLE}" "${KORAL_KOTLIN_DIR}/tools/generate_kotlin.py" --check)
        add_test(NAME Kotlin.IconsMatchTheSvgs
                COMMAND "${Python3_EXECUTABLE}" "${KORAL_KOTLIN_DIR}/tools/generate_icons.py" --check)
    endif()
endif()

# The UI module's tests live beside it, and need GTest from here.
add_subdirectory(${CMAKE_SOURCE_DIR}/modules/ui/tests ${CMAKE_BINARY_DIR}/modules/ui/tests)
add_subdirectory(${CMAKE_SOURCE_DIR}/modules/gui-extras/tests ${CMAKE_BINARY_DIR}/modules/gui-extras/tests)
add_subdirectory(${CMAKE_SOURCE_DIR}/modules/net/tests ${CMAKE_BINARY_DIR}/modules/net/tests)

# kmath's C interface, driven from C — which is also the check that koral_math_c.h is C99.
add_executable(koral_math_c_test test_math_c.c)
set_target_properties(koral_math_c_test PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE CXX)
target_compile_options(koral_math_c_test PRIVATE $<$<C_COMPILER_ID:GNU,Clang>:-Wall -Wextra -pedantic>)
target_link_libraries(koral_math_c_test PRIVATE Koral $<$<NOT:$<PLATFORM_ID:Windows>>:m>)
add_test(NAME CApi.MathFromC COMMAND koral_math_c_test)

# kmath's code for every language (C++ swizzles, the C exports, the C# and Kotlin types and functions, and their
# parity tests) comes from scripts/kmath: this fails when its description changed and it was not run again.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME Math.GeneratedCodeIsCurrent COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/kmath/generate.py" --check)
endif()
