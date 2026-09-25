#pragma once

#include <filesystem>
#include <vector>

#include <glm/vec3.hpp>

namespace kmdl::detail
{
    /** @brief One light of a glTF file's KHR_lights_punctual list, as the file wrote it. */
    struct GltfLight {
        glm::vec3 color { 1.f };
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
}
