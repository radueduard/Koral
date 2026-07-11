// Integration coverage for the asset importer: model loading through Assimp
// (importer.cpp + assimpImporter/) and image decoding/upload (Importer::LoadImage).
// Uses the bundled DamagedHelmet glTF asset. Runs on the headless device since
// the mesh/image uploads need a GPU; skips when no device is available.

#include "gpu_fixture.h"

#include <cstdint>
#include <string>
#include <vector>

#include "buffer.h"
#include "context.h"
#include "image.h"
#include "importer.h"
#include "mesh.h"
#include "meshLayout.h"

using gfx::Importer;
using gfx::ResourceRef;

namespace {

std::filesystem::path helmetPath() {
    return gfx::assetPath("DamagedHelmet/DamagedHelmet.gltf");
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

// Upload a scene mesh into GPU vertex/index buffers via Importer::LoadMesh,
// driving importer_detail::buildVertices/uploadVertexBuffer for a multi-attribute
// vertex (position + normal + uv), all of which the helmet provides.
TEST_F(GpuTest, ImporterUploadsMeshToGpu) {
    using Vertex = gfx::ParamVertex<gfx::Position, gfx::Normal, gfx::UV>;
    using Mesh = gfx::ParamMesh<Vertex>;

    auto importer = Importer::Load(helmetPath());
    ASSERT_NE(importer, nullptr);
    Importer::Scene scene = importer->LoadScene();
    ASSERT_FALSE(scene.meshes.empty());

    auto gpuMesh = importer->LoadMesh<Mesh>(scene.meshes.front());
    ASSERT_TRUE(static_cast<bool>(gpuMesh));
}

// Decode one of the helmet's textures off disk and upload it, with and without
// a generated mip chain — exercises Importer::LoadImage end to end.
TEST_F(GpuTest, ImporterLoadsImageFromDisk) {
    const auto path = gfx::assetPath("DamagedHelmet/Default_albedo.jpg");

    auto image = Importer::LoadImage(path);
    ASSERT_TRUE(static_cast<bool>(image));
    const glm::uvec3 extent = image->getExtent();
    EXPECT_GT(extent.x, 0u);
    EXPECT_GT(extent.y, 0u);

    auto mipped = Importer::LoadImage(path, /*generateMipmaps=*/true);
    ASSERT_TRUE(static_cast<bool>(mipped));
    EXPECT_EQ(mipped->getExtent(), extent);
}

// Save a GPU image to disk and read it back, verifying the pixels survive the
// round-trip. Drives Importer::SaveImage (CopyImageToBuffer + OIIO encode) and
// the decode path again on a file this test produced.
TEST_F(GpuTest, ImporterSaveImageRoundTrips) {
    using gfx::Buffer;
    using gfx::CommandBuffer;
    using gfx::Image;

    constexpr std::uint32_t kSize = 8;
    auto image = Image::Builder{}
                     .setType(Image::Type::e2D)
                     .setFormat(Image::Format::eRGBA8_UNORM)
                     .setExtent(glm::uvec2{kSize, kSize})
                     .addUsage(Image::Usage::eTransferDst)
                     .addUsage(Image::Usage::eTransferSrc)
                     .addUsage(Image::Usage::eSampled)
                     .build();
    // 0.25/0.5/0.75 -> 64/128/191 in UNORM8.
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.ClearColorImage(ResourceRef<const Image>(image), glm::vec4{0.25f, 0.5f, 0.75f, 1.f});
    }, CommandBuffer::Usage::eGraphics);

    const auto outPath = std::filesystem::temp_directory_path() / "gfx_importer_roundtrip.png";
    std::error_code ec;
    std::filesystem::remove(outPath, ec);
    Importer::SaveImage(outPath, "roundtrip", gfx::FileFormat::ePNG, ResourceRef<const Image>(image));
    ASSERT_TRUE(std::filesystem::exists(outPath));

    auto reloaded = Importer::LoadImage(outPath);
    ASSERT_TRUE(static_cast<bool>(reloaded));
    EXPECT_EQ(reloaded->getExtent(), glm::uvec3(kSize, kSize, 1));

    // Read the reloaded texels back and confirm the color survived the PNG round-trip.
    Buffer::RawBuilder rb;
    rb.setRawSize(static_cast<glm::i64>(kSize) * kSize * 4)
      .addUsage(Buffer::Usage::eTransferDst)
      .setType(Buffer::Type::eReadback);
    auto readback = rb.build();
    CommandBuffer::SingleTimeCommand([&](CommandBuffer& cb) {
        cb.CopyImageToBuffer(ResourceRef<const Image>(reloaded), ResourceRef<const Buffer>(readback));
    }, CommandBuffer::Usage::eTransfer);
    const auto texels = readback->Read<glm::u8vec4>();
    ASSERT_EQ(texels.size(), static_cast<std::size_t>(kSize) * kSize);
    for (const auto& t : texels) {
        EXPECT_NEAR(t.r, 64, 2);
        EXPECT_NEAR(t.g, 128, 2);
        EXPECT_NEAR(t.b, 191, 2);
    }
    std::filesystem::remove(outPath, ec);
}

} // namespace
