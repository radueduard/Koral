#pragma once

#include <filesystem>
#include <vector>

#include <kmath/vector.h>

namespace kmdl::detail
{
    /** @brief One light of a glTF file's KHR_lights_punctual list, as the file wrote it. */
    struct GltfLight {
        kor::Vec3 color { 1.f };
        float intensity = 1.f;  ///< Candela for point and spot lights, lux for directional ones.
        float range = 0.f;      ///< 0: the file set no range.
    };

    /**
     * @brief The lights a .gltf or .glb declares, in the order it declares them.
     *
     * Assimp hands lights over with the colour already multiplied by the intensity, which loses
     * both when the intensity is 0 and the intensity always. It keeps the file's order, so these
     * line up with aiScene::mLights by index. Empty for any other format, or a file with no lights.
     */
    std::vector<GltfLight> ReadGltfLights(const std::filesystem::path& path);

    /** @brief One camera of a glTF file's `cameras`, as the file wrote it. */
    struct GltfCamera {
        bool orthographic = false;
        float yfov = 0.f;           ///< Perspective: top to bottom, in radians.
        float ymag = 0.f;           ///< Orthographic: half of what it sees, top to bottom.
        float aspect = 0.f;         ///< 0: the file set none.
        float znear = 0.f;
        float zfar = 0.f;           ///< 0: none — an infinite perspective.
    };

    /**
     * @brief The cameras a .gltf or .glb declares, in the order it declares them.
     *
     * Assimp turns a glTF camera's vertical field of view into a horizontal one by an aspect ratio the file need not
     * give. It keeps the file's order, so these line up with aiScene::mCameras by index. Empty for any other format,
     * or a file with no cameras.
     */
    std::vector<GltfCamera> ReadGltfCameras(const std::filesystem::path& path);
}
