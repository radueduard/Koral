//
// The interface between the runtime and a library of scenes.
//

#pragma once

#include <cstdint>
#include <type_traits>

#include "api.h"

namespace kor
{
    class Scene;
    class SceneArgs;
}

/**
 * @file sceneLibrary.h
 * @brief How a shared library tells the runtime which scenes it has.
 *
 * A library exports one C function, `KoralGetScenes`, returning a table of named scenes, each with a
 * function that makes one and one that destroys it. C, so that nothing about it depends on the
 * compiler that built either side — and so that a library written in another language can provide
 * the same table. The macros below write it for a C++ library:
 *
 * @code
 * #include <sceneLibrary.h>
 *
 * KORAL_SCENES(
 *     KORAL_SCENE("Menu",  MenuScene),
 *     KORAL_SCENE("Level", LevelScene),   // LevelScene(const kor::SceneArgs&) gets the arguments
 * )
 * @endcode
 *
 * The runtime loads the library, registers every scene under its name (App::LoadLibrary), opens
 * the one it was asked to, and destroys each with the library's own function — so a scene is freed
 * by the allocator that made it. A library can be unloaded, or reloaded, once none of its scenes is
 * open (App::UnloadLibrary, App::ReloadLibrary).
 *
 * A library exporting the older `CreateScene` instead is one scene, named after the library file.
 */
extern "C" {
    /** @brief Bumped whenever the table below — or kor::Scene, which a library derives from — changes shape. A library built against another is refused. */
    #define KORAL_SCENE_ABI_VERSION 3u

    /** @brief One scene a library offers. */
    struct KoralSceneEntry {
        const char* name;                                        ///< What it is opened by. Unique within the application.
        kor::Scene* (*create)(const kor::SceneArgs* arguments);  ///< Makes one; never null arguments.
        void (*destroy)(kor::Scene* scene);                      ///< Frees one this library made.
    };

    /** @brief Every scene a library offers. */
    struct KoralSceneTable {
        std::uint32_t abiVersion;       ///< KORAL_SCENE_ABI_VERSION as the library saw it.
        std::uint32_t count;
        const KoralSceneEntry* entries;
    };

    /** @brief The one function a scene library exports. */
    typedef const KoralSceneTable* (*KoralGetScenesFn)();
}

namespace kor::detail
{
    template<typename S>
    kor::Scene* CreateSceneOf(const kor::SceneArgs* arguments) {
        if constexpr (std::is_constructible_v<S, const kor::SceneArgs&>) return new S(*arguments);
        else return new S();
    }

    template<typename S>
    void DestroySceneOf(kor::Scene* scene) { delete static_cast<S*>(scene); }
}

#if defined(_WIN32)
    #define KORAL_SCENE_EXPORT_FN __declspec(dllexport)
#else
    #define KORAL_SCENE_EXPORT_FN __attribute__((visibility("default")))
#endif
#define KORAL_SCENE_EXPORT extern "C" KORAL_SCENE_EXPORT_FN

/** @brief One scene of a KORAL_SCENES table: its name, and the class that is it. */
#define KORAL_SCENE(name, Type) \
    KoralSceneEntry { name, &::kor::detail::CreateSceneOf<Type>, &::kor::detail::DestroySceneOf<Type> }

/** @brief Exports the library's scenes. Once per library, at namespace scope. */
#define KORAL_SCENES(...)                                                                        \
    KORAL_SCENE_EXPORT const KoralSceneTable* KoralGetScenes() {                                 \
        static const KoralSceneEntry entries[] = { __VA_ARGS__ };                                \
        static const KoralSceneTable table {                                                     \
            KORAL_SCENE_ABI_VERSION,                                                             \
            static_cast<std::uint32_t>(sizeof(entries) / sizeof(entries[0])),                   \
            entries };                                                                           \
        return &table;                                                                           \
    }
