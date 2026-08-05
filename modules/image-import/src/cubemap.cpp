//
// Created by radue on 29.07.2026.
//

// The two ways into a cubemap: six face files assembled into one six-layer image, and an
// equirectangular panorama projected onto one by a compute dispatch.
//
// Both produce the same thing — a cube-compatible image whose layers are in the standard order
// (+X, -X, +Y, -Y, +Z, -Z) — so a project builds the same kind of ImageView over either.

#include <koralImageImport.h>

#include <array>
#include <format>
#include <vector>

#include <buffer.h>
#include <commandBuffer.h>
#include <computePipeline.h>
#include <context.h>
#include <descriptor.h>
#include <descriptorSet.h>
#include <file.h>
#include <imageView.h>
#include <log.h>
#include <sampler.h>
#include <shader.h>

#include "decode.h"

namespace kimg
{
    namespace
    {
        constexpr glm::u32 kFaceCount = 6;

        kor::Resource<kor::Image> failure(std::string what)
        {
            kor::log::error("[image] {}", what);
            return kor::Resource<kor::Image>::failed(
                kor::Error{ .code = kor::ErrorCode::eInvalidArgument, .message = std::move(what) },
                "Image");
        }

        /**
         * @brief Says once, out loud, that cube *sampling* is a gap in the OpenGL backend.
         *
         * The loading below works on either backend — six layers of an ordinary 2D image — but the
         * GL backend binds a view's underlying image rather than the view object, so a cube view of
         * those layers is bound as the 2D array it is stored as. Better said than silently sampled
         * wrong. Vulkan is unaffected.
         */
        void warnAboutOpenGlOnce()
        {
            static bool said = false;
            if (said || kor::Context::activeAPI() != kor::API::eOpenGL) return;
            said = true;
            kor::log::warn("[image] cubemaps load on the OpenGL backend but cannot yet be sampled as "
                           "cubes there: an image view is bound as its underlying image, so a cube "
                           "view reads as a 2D array. Vulkan is unaffected.");
        }

        /** @brief Creates the six-layer image the faces are uploaded into. */
        kor::Resource<kor::Image> makeCubeImage(const glm::uvec2 faceExtent, const kor::Image::Format format,
                                                const bool generateMipmaps, const bool storage)
        {
            // Six 2D layers is what makes an image cube-compatible; the engine sets the Vulkan
            // create flag on exactly that shape. @see kor::ImageView::Type::eCube
            warnAboutOpenGlOnce();

            auto builder = kor::Image::Builder()
                .setType(kor::Image::Type::e2D)
                .setExtent({ faceExtent.x, faceExtent.y, 1 })
                .setArrayLayers(kFaceCount)
                .setMipLevels(generateMipmaps ? 0 : 1)
                .setFormat(format)
                .addUsage(kor::Image::Usage::eTransferSrc)
                .addUsage(kor::Image::Usage::eTransferDst);
            if (storage) builder.addUsage(kor::Image::Usage::eStorage);
            return builder.build();
        }

        /** @brief Uploads one decoded face into its layer. */
        void uploadFace(const kor::ResourceRef<const kor::Image>& cube, const CpuImage& face,
                        const glm::u32 layer)
        {
            detail::uploadSlice(cube, face.pixels, { face.extent.x, face.extent.y, 1 }, layer, 0);
        }

        /**
         * @brief Checks that six decoded faces can be the six faces of one cube.
         *
         * A cube whose faces disagree about size or format is not a cube, and finding that out here
         * — naming the face that differs — beats finding out from a copy that writes the wrong
         * number of bytes into a layer.
         */
        std::optional<std::string> facesDisagree(const std::array<CpuImage, kFaceCount>& faces,
                                                 const std::array<std::filesystem::path, kFaceCount>& paths)
        {
            for (glm::u32 face = 1; face < kFaceCount; ++face) {
                if (faces[face].extent != faces[0].extent) {
                    return std::format("cubemap face '{}' is {}x{} but '{}' is {}x{}; every face must match",
                                       paths[face].string(), faces[face].extent.x, faces[face].extent.y,
                                       paths[0].string(), faces[0].extent.x, faces[0].extent.y);
                }
                if (faces[face].format != faces[0].format) {
                    return std::format("cubemap face '{}' has a different pixel format from '{}'; "
                                       "every face must match",
                                       paths[face].string(), paths[0].string());
                }
            }
            if (faces[0].extent.x != faces[0].extent.y) {
                return std::format("cubemap face '{}' is {}x{}; faces must be square",
                                   paths[0].string(), faces[0].extent.x, faces[0].extent.y);
            }
            return std::nullopt;
        }
    }

    kor::Resource<kor::Image> LoadCubemap(const CubeFaces& faces, const bool generateMipmaps)
    {
        std::array<std::filesystem::path, kFaceCount> paths{};
        const auto named = faces.inLayerOrder();
        for (glm::u32 face = 0; face < kFaceCount; ++face) paths[face] = kor::assetPath(named[face]);

        std::array<CpuImage, kFaceCount> decoded{};
        for (glm::u32 face = 0; face < kFaceCount; ++face) {
            auto result = detail::decodeFile(paths[face]);
            if (!result) return failure(std::string(result.error().message));
            decoded[face] = std::move(*result);
        }

        if (const auto complaint = facesDisagree(decoded, paths)) return failure(*complaint);

        auto cube = makeCubeImage({ decoded[0].extent.x, decoded[0].extent.y },
                                  decoded[0].format, generateMipmaps, false);
        for (glm::u32 face = 0; face < kFaceCount; ++face) uploadFace(cube, decoded[face], face);
        detail::finishUpload(cube, generateMipmaps);
        return cube;
    }

    kor::Task<kor::Resource<kor::Image>> LoadCubemapAsync(CubeFaces faces, const bool generateMipmaps)
    {
        // Resolved on the main thread, before anything is handed to a background one: the search
        // roots are read-mostly global state the main thread owns.
        std::array<std::filesystem::path, kFaceCount> paths{};
        const auto named = faces.inLayerOrder();
        for (glm::u32 face = 0; face < kFaceCount; ++face) paths[face] = kor::assetPath(named[face]);

        co_await kor::Context::SwitchToBackgroundThread();

        std::array<CpuImage, kFaceCount> decoded{};
        for (glm::u32 face = 0; face < kFaceCount; ++face) {
            auto result = detail::decodeFile(paths[face]);
            if (!result) {
                auto message = std::string(result.error().message);
                co_await kor::Context::SwitchToMainThread();
                co_return failure(std::move(message));
            }
            decoded[face] = std::move(*result);
        }

        if (auto complaint = facesDisagree(decoded, paths)) {
            co_await kor::Context::SwitchToMainThread();
            co_return failure(std::move(*complaint));
        }

        co_await kor::Context::SwitchToMainThread();
        auto cube = makeCubeImage({ decoded[0].extent.x, decoded[0].extent.y },
                                  decoded[0].format, generateMipmaps, false);
        kor::ResourceRef cubeRef = cube;

        // One face per short main-thread visit, so a frame can be drawn between them.
        for (glm::u32 face = 0; face < kFaceCount; ++face) {
            uploadFace(cubeRef, decoded[face], face);
            co_await kor::Context::SwitchToBackgroundThread();
            co_await kor::Context::SwitchToMainThread();
        }

        detail::finishUpload(cubeRef, generateMipmaps);
        co_return std::move(cube);
    }

    // ---- Equirectangular projection -------------------------------------------------------------

    namespace
    {
        // The pipeline and the sampler the projection runs with, built once and reused. They are
        // GPU resources held by the module rather than by a caller, which is the whole reason
        // MeshModule's counterpart here has a Shutdown: they must be released before the device is.
        // @see kimg::detail::releaseGpuCache
        kor::Resource<kor::ComputePipeline> g_pipeline;
        kor::Resource<kor::Sampler> g_sampler;

        // Returns the owning resource rather than a ref: a ResourceRef of a *derived* type cannot
        // be widened to one of its base, while a Resource can, and the descriptor set needs the
        // pipeline as a kor::Pipeline.
        kor::Resource<kor::ComputePipeline>& projectionPipeline()
        {
            if (!g_pipeline) {
                const auto shader = kor::Shader::Builder{}
                    .setLang<kor::Shader::Lang::eSlang>()
                    .setEntryPoint("equirectToCube", "main")
                    .getOrBuild("koral.image.equirectToCube");
                g_pipeline = kor::ComputePipeline::Builder{}.setComputeShader(shader).build();
            }
            return g_pipeline;
        }

        kor::ResourceRef<const kor::Sampler> projectionSampler()
        {
            if (!g_sampler) {
                g_sampler = kor::Sampler::Builder{}
                    // Longitude wraps: the texel left of u=0 is the one at u=1, which is what keeps
                    // the seam behind the viewer from showing. Latitude does not — clamping stops
                    // the pole row bleeding round to the other pole.
                    .setAddressModeU(kor::Sampler::AddressMode::eRepeat)
                    .setAddressModeV(kor::Sampler::AddressMode::eClampToEdge)
                    .setAddressModeW(kor::Sampler::AddressMode::eClampToEdge)
                    .setMinFilter(kor::Filter::eLinear)
                    .setMagFilter(kor::Filter::eLinear)
                    .build();
            }
            return kor::ResourceRef<const kor::Sampler>(g_sampler);
        }

        /**
         * @brief What the projection writes, whatever it read.
         *
         * The shader declares this format rather than inheriting the pipeline's, which is what lets
         * it write without the shaderStorageImageWriteWithoutFormat feature — and it is the right
         * choice anyway: a panorama is normally HDR, and 32-bit float is what keeps its range. Keep
         * in step with the [format(...)] attribute in equirectToCube.slang.
         */
        constexpr auto kProjectedFormat = kor::Image::Format::eRGBA32_SFLOAT;
    }

    namespace detail
    {
        // Called from the module's Shutdown. Not a static destructor: these hold GPU handles, and a
        // static destructor runs long after the device is gone.
        void releaseGpuCache()
        {
            g_pipeline.reset();
            g_sampler.reset();
        }
    }

    kor::Resource<kor::Image> EquirectangularToCubemap(kor::ResourceRef<const kor::Image> equirect,
                                                       glm::u32 faceSize, const bool generateMipmaps)
    {
        if (!equirect) {
            return failure("cannot project an equirectangular image that could not be loaded");
        }

        const auto sourceExtent = equirect->getExtent();
        if (faceSize == 0) {
            // A quarter of the panorama's width: the point at which a face's texels are about as
            // dense as the source's, so the projection neither invents nor discards detail.
            faceSize = std::max(1u, sourceExtent.x / 4);
        }

        auto& pipeline = projectionPipeline();
        if (!pipeline) {
            return failure("the equirectangular projection shader could not be compiled; "
                           "is the image module's shaders/ directory reachable?");
        }

        auto cube = makeCubeImage({ faceSize, faceSize }, kProjectedFormat, generateMipmaps, true);

        const auto equirectView = kor::ImageView::Builder(equirect).build();
        // The cube seen as what the shader writes: a six-layer 2D array, top mip only. The same
        // image is *read* later through a cube view — one image, two ways of looking at it.
        const auto cubeView = kor::ImageView::Builder(kor::ResourceRef<const kor::Image>(cube))
            .setViewType(kor::ImageView::Type::e2DArray)
            .setArrayLayerCount(kFaceCount)
            .setMipLevelCount(1)
            .build();

        const auto set = kor::DescriptorSet::Builder(kor::ResourceRef<const kor::Pipeline>(pipeline), 0)
            .write(0, kor::Descriptor(kor::ResourceRef<const kor::ImageView>(equirectView)))
            .write(1, kor::Descriptor(projectionSampler()))
            .write(2, kor::Descriptor(kor::ResourceRef<const kor::ImageView>(cubeView)))
            .build();
        if (!set) {
            return failure(std::format("the equirectangular projection could not be bound: {}",
                                       set.error() ? set.error()->message : "unknown reason"));
        }

        constexpr glm::u32 kLocalSize = 8;   // matches [numthreads(8, 8, 1)] in equirectToCube.slang
        const glm::u32 groups = (faceSize + kLocalSize - 1) / kLocalSize;

        kor::CommandBuffer::SingleTimeCommand([&](kor::CommandBuffer& commandBuffer) {
            commandBuffer.BindComputePipeline(kor::ResourceRef<const kor::ComputePipeline>(pipeline));
            commandBuffer.BindDescriptorSet(0, kor::ResourceRef<const kor::DescriptorSet>(set));
            commandBuffer.Dispatch(groups, groups, kFaceCount);
        }, kor::CommandBuffer::Usage::eCompute);

        detail::finishUpload(kor::ResourceRef<const kor::Image>(cube), generateMipmaps);
        return cube;
    }

    kor::Resource<kor::Image> LoadCubemapFromEquirectangular(const std::filesystem::path& relativePath,
                                                             const glm::u32 faceSize, const bool generateMipmaps)
    {
        auto panorama = LoadImage(relativePath);
        if (!panorama) return panorama;   // already poisoned, and already says why

        return EquirectangularToCubemap(kor::ResourceRef<const kor::Image>(panorama), faceSize, generateMipmaps);
    }

    kor::Task<kor::Resource<kor::Image>> LoadCubemapFromEquirectangularAsync(std::filesystem::path relativePath,
                                                                            const glm::u32 faceSize,
                                                                            const bool generateMipmaps)
    {
        // The file half is what there is to overlap — decoding a 8192x4096 panorama is most of the
        // cost. The projection itself is one dispatch, and runs on the main thread once it lands.
        auto panorama = co_await LoadImageAsync(relativePath);
        if (!panorama) co_return std::move(panorama);

        co_return EquirectangularToCubemap(kor::ResourceRef<const kor::Image>(panorama), faceSize, generateMipmaps);
    }
}
