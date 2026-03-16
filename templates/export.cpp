#include "@NAME@.h"

#if defined(_WIN32)
    #define SCENE_EXPORT extern "C" __declspec(dllexport)
#else
    #define SCENE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

SCENE_EXPORT gfx::Scene* CreateScene()
{
    return new @NAME@();
}