// Integration coverage for the model import module (modules/model-import): loading through Assimp and
// the bridge from imported geometry to one of the mesh module's vertex formats.
// Uses the bundled DamagedHelmet glTF asset. Runs on the headless device since the mesh uploads
// need a GPU; skips when no device is available.
//
// Image loading lives in the image module now, and is covered by test_image_module.cpp.

#include "gpu_fixture.h"

#include <cstdint>
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
    return kor::assetPath("DamagedHelmet/DamagedHelmet.gltf");
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

} // namespace
