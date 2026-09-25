// Integration coverage for the model import module (modules/model-import): loading through Assimp and
// the bridge from imported geometry to one of the mesh module's vertex formats.
// Uses the bundled DamagedHelmet glTF asset. Runs on the headless device since the mesh uploads
// need a GPU; skips when no device is available.
//
// Image loading lives in the image module now, and is covered by test_image_module.cpp.

#include "gpu_fixture.h"

#include <cstdint>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

#include "buffer.h"
#include "context.h"
#include "image.h"
#include "mesh.h"

#include <koralMesh.h>
#include <koralModelImport.h>
#include <koralModelMesh.h>

using kmdl::Importer;
using kor::ResourceRef;

namespace {

std::filesystem::path helmetPath() {
    return kor::AssetPath("DamagedHelmet/DamagedHelmet.gltf");
}

// Load the glTF scene and walk the mesh/material/node metadata the importer
// exposes. Purely reads the parsed data — no GPU needed for this part, but the
// fixture keeps it beside the upload test below.
TEST_F(GpuTest, ImporterLoadsGltfSceneMetadata) {
    auto importer = Importer::Load(helmetPath());
    ASSERT_NE(importer, nullptr);

    const auto meshNames = importer->GetMeshNames();
    const auto materialNames = importer->GetMaterialNames();
    ASSERT_FALSE(meshNames.empty());

    Importer::Scene scene = importer->LoadScene();
    ASSERT_FALSE(scene.meshes.empty());
    ASSERT_FALSE(scene.nodes.empty());

    // The first mesh must carry geometry, and its indices (if any) must stay in
    // range of the position array.
    const Importer::Mesh& mesh = scene.meshes.front();
    EXPECT_FALSE(mesh.positions.empty());
    if (mesh.indices.has_value()) {
        for (const glm::u32 idx : *mesh.indices) {
            ASSERT_LT(idx, mesh.positions.size());
        }
    }

    // GetMesh / GetMaterial by name should return the same geometry the scene did.
    const Importer::Mesh byName = importer->GetMesh(meshNames.front());
    EXPECT_EQ(byName.positions.size(), mesh.positions.size());
    if (!materialNames.empty()) {
        const Importer::Material mat = importer->GetMaterial(materialNames.front());
        EXPECT_FALSE(mat.name.empty());
    }

    // The helmet is not skinned; the bone-matrix query should simply not crash.
    (void)importer->GetBoneTransformationMatrices();
}

// Upload a scene mesh into GPU vertex/index buffers via the mesh module's LoadMesh,
// driving importer_detail::buildVertices/uploadVertexBuffer for a multi-attribute
// vertex (position + normal + uv), all of which the helmet provides.
TEST_F(GpuTest, ImporterUploadsMeshToGpu) {
    using Vertex = kmesh::ParamVertex<kmesh::Position, kmesh::Normal, kmesh::UV>;
    using Mesh = kmesh::ParamMesh<Vertex>;

    auto importer = Importer::Load(helmetPath());
    ASSERT_NE(importer, nullptr);
    Importer::Scene scene = importer->LoadScene();
    ASSERT_FALSE(scene.meshes.empty());

    auto gpuMesh = kmdl::LoadMesh<Mesh>(scene.meshes.front());
    ASSERT_TRUE(static_cast<bool>(gpuMesh));
}

// Assimp multiplies a glTF light's colour by its intensity, so on its own a light the file switched
// off comes through black and every light's intensity is lost. The importer reads the file's own
// KHR_lights_punctual list to hand both back separately.
TEST_F(GpuTest, ImporterKeepsAGltfLightsColourAndIntensityApart) {
    const auto path = std::filesystem::temp_directory_path() / "koral_lights_test.gltf";
    {
        std::ofstream out(path);
        out << R"GLTF({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["KHR_lights_punctual"],
  "extensions": {"KHR_lights_punctual": {"lights": [
    {"name": "off", "type": "point", "color": [1.0, 0.5, 0.25], "intensity": 0.0},
    {"name": "sun", "type": "directional", "color": [0.2, 0.4, 1.0], "intensity": 5.0, "range": 12.0}
  ]}},
  "scene": 0,
  "scenes": [{"nodes": [0, 1, 2]}],
  "nodes": [
    {"mesh": 0},
    {"name": "lamp", "translation": [1.0, 2.0, 3.0], "extensions": {"KHR_lights_punctual": {"light": 0}}},
    {"name": "sunNode", "extensions": {"KHR_lights_punctual": {"light": 1}}}
  ],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}],
  "buffers": [{"byteLength": 36, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA"}],
  "bufferViews": [{"buffer": 0, "byteLength": 36}],
  "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0]}]
})GLTF";
    }
    auto importer = Importer::Load(path);
    ASSERT_NE(importer, nullptr);
    const auto scene = importer->LoadScene();
    ASSERT_EQ(scene.lights.size(), 2u);

    const auto find = [&](const std::string& name) {
        for (const auto& light : scene.lights) if (light.name == name) return light;
        ADD_FAILURE() << "no light named " << name;
        return Importer::Light{};
    };
    const auto lamp = find("lamp");
    EXPECT_EQ(lamp.type, Importer::Light::Type::ePoint);
    EXPECT_EQ(lamp.color, glm::vec3(1.f, 0.5f, 0.25f)) << "a switched-off light keeps its colour";
    EXPECT_EQ(lamp.intensity, 0.f);
    EXPECT_EQ(lamp.position, glm::vec3(1.f, 2.f, 3.f));

    const auto sun = find("sunNode");
    EXPECT_EQ(sun.type, Importer::Light::Type::eDirectional);
    EXPECT_EQ(sun.color, glm::vec3(0.2f, 0.4f, 1.f)) << "the colour, without the intensity multiplied in";
    EXPECT_EQ(sun.intensity, 5.f);
    EXPECT_EQ(sun.range, 12.f);

    std::filesystem::remove(path);
}

} // namespace
