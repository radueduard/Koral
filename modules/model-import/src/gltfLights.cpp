#include "gltfLights.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

#include <rfl.hpp>
#include <rfl/json.hpp>

namespace kmdl::detail
{
    // Not in the anonymous namespace: reflect-cpp needs these types to have linkage, which Clang enforces
    // and GCC/MSVC let slide.
    namespace gltfLightsJson
    {
        // Only the part of a glTF document this reads; everything else is ignored.
        struct PunctualLight {
            std::optional<std::array<float, 3>> color;
            std::optional<float> intensity;
            std::optional<float> range;
        };
        struct PunctualLights {
            std::vector<PunctualLight> lights;
        };
        struct Extensions {
            rfl::Rename<"KHR_lights_punctual", std::optional<PunctualLights>> punctual;
        };
        // Every field optional: a camera written otherwise than this expects costs the cameras, not the lights.
        struct Perspective {
            std::optional<float> aspectRatio;
            std::optional<float> yfov;
            std::optional<float> zfar;
            std::optional<float> znear;
        };
        struct Orthographic {
            std::optional<float> ymag;
            std::optional<float> zfar;
            std::optional<float> znear;
        };
        struct Camera {
            std::optional<std::string> type;
            std::optional<Perspective> perspective;
            std::optional<Orthographic> orthographic;
        };
        struct Document {
            std::optional<Extensions> extensions;
        };
        struct CameraDocument {
            std::optional<std::vector<Camera>> cameras;
        };
    }

    namespace
    {
        using namespace gltfLightsJson;

        /** @brief The JSON text of a .gltf, or the JSON chunk of a .glb. */
        std::optional<std::string> documentOf(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file) return std::nullopt;
            std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

            if (path.extension() == ".gltf") return bytes;

            // GLB: a 12-byte header (magic "glTF", version, length), then chunks of
            // (length, type, data); the first is the JSON one.
            const auto u32 = [&](const std::size_t at) {
                std::uint32_t v = 0;
                std::memcpy(&v, bytes.data() + at, sizeof(v));
                return v;
            };
            if (bytes.size() < 20 || bytes.compare(0, 4, "glTF") != 0) return std::nullopt;
            const std::uint32_t length = u32(12);
            if (u32(16) != 0x4E4F534Au /* "JSON" */ || 20ull + length > bytes.size()) return std::nullopt;
            return bytes.substr(20, length);
        }
    }

    std::vector<GltfLight> ReadGltfLights(const std::filesystem::path& path)
    {
        const auto extension = path.extension();
        if (extension != ".gltf" && extension != ".glb") return {};

        const auto json = documentOf(path);
        if (!json) return {};
        const auto document = rfl::json::read<Document>(*json);
        if (!document || !document->extensions || !document->extensions->punctual.value()) return {};

        std::vector<GltfLight> lights;
        for (const auto& light : document->extensions->punctual.value()->lights) {
            GltfLight out;
            if (light.color) out.color = kor::Vec3((*light.color)[0], (*light.color)[1], (*light.color)[2]);
            // The spec's defaults: intensity 1, and no range at all.
            out.intensity = light.intensity.value_or(1.f);
            out.range = light.range.value_or(0.f);
            lights.push_back(out);
        }
        return lights;
    }

    std::vector<GltfCamera> ReadGltfCameras(const std::filesystem::path& path)
    {
        const auto extension = path.extension();
        if (extension != ".gltf" && extension != ".glb") return {};

        const auto json = documentOf(path);
        if (!json) return {};
        const auto document = rfl::json::read<CameraDocument>(*json);
        if (!document || !document->cameras) return {};

        std::vector<GltfCamera> cameras;
        for (const auto& camera : *document->cameras) {
            GltfCamera out;
            out.orthographic = camera.type.value_or("perspective") == "orthographic";
            if (out.orthographic && camera.orthographic) {
                out.ymag = camera.orthographic->ymag.value_or(1.f);
                out.znear = camera.orthographic->znear.value_or(0.f);
                out.zfar = camera.orthographic->zfar.value_or(0.f);
            } else if (camera.perspective) {
                out.yfov = camera.perspective->yfov.value_or(0.f);
                out.aspect = camera.perspective->aspectRatio.value_or(0.f);
                out.znear = camera.perspective->znear.value_or(0.f);
                out.zfar = camera.perspective->zfar.value_or(0.f);     // the spec's: none, an infinite projection
            }
            cameras.push_back(out);
        }
        return cameras;
    }
}
