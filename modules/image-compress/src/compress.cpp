//
// Created by radue on 30.07.2026.
//

// Encoding an image into a compressed KTX2, through libktx's Basis and ASTC encoders.
//
// Nothing here touches the GPU: it decodes a file (through the import module), builds a mip chain on
// the CPU, hands the levels to an encoder, and writes the container. That is what makes it usable as a
// build step as well as at runtime.

#include <koralImageCompress.h>

#include <algorithm>
#include <format>
#include <memory>
#include <vector>

#include <ktx.h>
#include <vulkan/vulkan_core.h>

#include <context.h>
#include <log.h>

namespace kimg
{
    namespace
    {
        kor::Error compressError(const std::filesystem::path& path, std::string what)
        {
            return kor::Error{ .code = kor::ErrorCode::eInvalidArgument,
                               .message = std::format("{}: {}", path.string(), std::move(what)) };
        }

        struct KtxTextureDeleter {
            void operator()(ktxTexture2* t) const noexcept { if (t) ktxTexture_Destroy(ktxTexture(t)); }
        };

        /** @brief How many mip levels an image of this size has, counting the full-size one. */
        glm::u32 mipLevelsFor(const glm::uvec3 extent)
        {
            glm::u32 levels = 1;
            for (glm::u32 size = std::max(extent.x, extent.y); size > 1; size /= 2) ++levels;
            return levels;
        }

        /**
         * @brief Halves an 8-bit image, averaging each 2x2 group of texels.
         *
         * A box filter, which is what a mip chain for a compressed texture is normally built with:
         * the encoders quantise hard enough that a better kernel does not survive the trip, and this
         * one cannot ring or overshoot. Odd sizes take the texels that exist.
         */
        CpuImage halve(const CpuImage& source, const glm::u32 channels)
        {
            CpuImage next;
            next.format = source.format;
            next.extent = { std::max(1u, source.extent.x / 2), std::max(1u, source.extent.y / 2), 1 };
            next.pixels.resize(static_cast<std::size_t>(next.extent.x) * next.extent.y * channels);

            for (glm::u32 y = 0; y < next.extent.y; ++y) {
                for (glm::u32 x = 0; x < next.extent.x; ++x) {
                    for (glm::u32 c = 0; c < channels; ++c) {
                        const auto at = [&](const glm::u32 sx, const glm::u32 sy) -> glm::u32 {
                            const glm::u32 cx = std::min(sx, source.extent.x - 1);
                            const glm::u32 cy = std::min(sy, source.extent.y - 1);
                            return source.pixels[(static_cast<std::size_t>(cy) * source.extent.x + cx) * channels + c];
                        };
                        const glm::u32 sum = at(x * 2, y * 2) + at(x * 2 + 1, y * 2)
                                           + at(x * 2, y * 2 + 1) + at(x * 2 + 1, y * 2 + 1);
                        next.pixels[(static_cast<std::size_t>(y) * next.extent.x + x) * channels + c] =
                            static_cast<unsigned char>((sum + 2) / 4);
                    }
                }
            }
            return next;
        }

        /** @brief The channel count of an 8-bit format, or nullopt when the format is not one. */
        std::optional<glm::u32> eightBitChannels(const kor::Image::Format format)
        {
            using F = kor::Image::Format;
            switch (format) {
            case F::eR8_UNORM: case F::eR8_SINT: case F::eR8_UINT: case F::eR8_SNORM:
                return 1;
            case F::eRG8_UNORM: case F::eRG8_SINT: case F::eRG8_UINT: case F::eRG8_SNORM:
                return 2;
            case F::eRGBA8_UNORM: case F::eRGBA8_SRGB: case F::eRGBA8_SINT:
            case F::eRGBA8_UINT: case F::eRGBA8_SNORM:
                return 4;
            default:
                return std::nullopt;
            }
        }

        /** @brief The uncompressed VkFormat to record before the encoder replaces it. */
        std::uint32_t sourceVkFormat(const glm::u32 channels, const bool srgb)
        {
            switch (channels) {
            case 1: return VK_FORMAT_R8_UNORM;
            case 2: return VK_FORMAT_R8G8_UNORM;
            default: return srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
            }
        }

        /** @brief Runs the encoder the options ask for over an already-populated texture. */
        std::expected<void, kor::Error> encode(ktxTexture2* texture, const std::filesystem::path& target,
                                              const CompressOptions& options)
        {
            switch (options.codec) {
            case Codec::eASTC: {
                ktxAstcParams params {};
                params.structSize = sizeof(params);
                params.blockDimension = KTX_PACK_ASTC_BLOCK_DIMENSION_4x4;
                params.mode = KTX_PACK_ASTC_ENCODER_MODE_LDR;
                params.qualityLevel = std::clamp<glm::u32>(options.quality, 1, KTX_PACK_ASTC_QUALITY_LEVEL_MAX);
                params.normalMap = options.normalMap ? KTX_TRUE : KTX_FALSE;
                params.perceptual = options.normalMap ? KTX_FALSE : KTX_TRUE;
                params.threadCount = 0;   // libktx picks a sensible number

                if (const auto result = ktxTexture2_CompressAstcEx(texture, &params); result != KTX_SUCCESS)
                    return std::unexpected(compressError(target,
                        std::format("ASTC encoding failed ({})", ktxErrorString(result))));
                return {};
            }
            case Codec::eUASTC:
            case Codec::eETC1S:
            default: {
                ktxBasisParams params {};
                params.structSize = sizeof(params);
                params.uastc = options.codec == Codec::eUASTC ? KTX_TRUE : KTX_FALSE;
                params.threadCount = 0;
                params.normalMap = options.normalMap ? KTX_TRUE : KTX_FALSE;

                if (options.codec == Codec::eUASTC) {
                    // UASTC has five discrete levels rather than a 1..255 scale, so the option is
                    // mapped onto them: 128 — the default — lands on KTX_PACK_UASTC_LEVEL_DEFAULT.
                    const glm::u32 level = std::min<glm::u32>(
                        options.quality * (KTX_PACK_UASTC_MAX_LEVEL + 1) / 256, KTX_PACK_UASTC_MAX_LEVEL);
                    params.uastcFlags = level;
                } else {
                    params.qualityLevel = std::clamp<glm::u32>(options.quality, 1, 255);
                }

                if (const auto result = ktxTexture2_CompressBasisEx(texture, &params); result != KTX_SUCCESS)
                    return std::unexpected(compressError(target,
                        std::format("{} encoding failed ({})",
                                    options.codec == Codec::eUASTC ? "UASTC" : "ETC1S",
                                    ktxErrorString(result))));

                // ETC1S carries its own entropy coding; deflating it again buys nothing.
                if (options.supercompress && options.codec == Codec::eUASTC) {
                    if (const auto result = ktxTexture2_DeflateZstd(texture, 18); result != KTX_SUCCESS)
                        return std::unexpected(compressError(target,
                            std::format("Zstd supercompression failed ({})", ktxErrorString(result))));
                }
                return {};
            }
            }
        }
    }

    kor::Result<std::filesystem::path> CompressToKtx(const CpuImage& image,
                                                    const std::filesystem::path& outputDirectory,
                                                    const std::string& name,
                                                    const CompressOptions& options)
    {
        const auto target = outputDirectory / (name + ".ktx2");

        const auto channels = eightBitChannels(image.format);
        if (!channels) {
            // The encoders take 8-bit colour. An HDR source wants BC6H, which is a different
            // encoder entirely and not one libktx offers — better said than silently truncated.
            return std::unexpected(compressError(target,
                "these encoders take 8-bit images; convert the source, or keep it uncompressed"));
        }
        if (image.extent.x == 0 || image.extent.y == 0) {
            return std::unexpected(compressError(target, "the image is empty"));
        }

        // Every level, largest first — including the largest, which is the image itself.
        std::vector<CpuImage> levels;
        levels.push_back(image);
        if (options.generateMipmaps) {
            const auto wanted = mipLevelsFor(image.extent);
            while (levels.size() < wanted) levels.push_back(halve(levels.back(), *channels));
        }

        ktxTextureCreateInfo info {};
        info.vkFormat = sourceVkFormat(*channels, options.srgb);
        info.baseWidth = image.extent.x;
        info.baseHeight = image.extent.y;
        info.baseDepth = 1;
        info.numDimensions = 2;
        info.numLevels = static_cast<ktx_uint32_t>(levels.size());
        info.numLayers = 1;
        info.numFaces = 1;
        info.isArray = KTX_FALSE;
        info.generateMipmaps = KTX_FALSE;   // the levels are supplied above, not asked for later

        ktxTexture2* raw = nullptr;
        if (const auto created = ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &raw);
            created != KTX_SUCCESS || !raw) {
            return std::unexpected(compressError(target,
                std::format("could not be created ({})", ktxErrorString(created))));
        }
        const std::unique_ptr<ktxTexture2, KtxTextureDeleter> texture(raw);

        for (std::size_t level = 0; level < levels.size(); ++level) {
            const auto set = ktxTexture_SetImageFromMemory(
                ktxTexture(texture.get()), static_cast<ktx_uint32_t>(level), 0, 0,
                levels[level].pixels.data(), levels[level].pixels.size());
            if (set != KTX_SUCCESS) {
                return std::unexpected(compressError(target,
                    std::format("level {} could not be stored ({})", level, ktxErrorString(set))));
            }
        }

        if (auto encoded = encode(texture.get(), target, options); !encoded)
            return std::unexpected(encoded.error());

        std::error_code ec;
        std::filesystem::create_directories(outputDirectory, ec);

        if (const auto written = ktxTexture_WriteToNamedFile(ktxTexture(texture.get()), target.string().c_str());
            written != KTX_SUCCESS) {
            return std::unexpected(compressError(target,
                std::format("could not be written ({})", ktxErrorString(written))));
        }
        return target;
    }

    kor::Result<std::filesystem::path> CompressToKtx(const std::filesystem::path& source,
                                                    const std::filesystem::path& outputDirectory,
                                                    const CompressOptions& options)
    {
        auto decoded = DecodeImageFile(source);
        if (!decoded) return std::unexpected(decoded.error());

        return CompressToKtx(*decoded, outputDirectory, source.stem().string(), options);
    }

    kor::Task<kor::Result<std::filesystem::path>> CompressToKtxAsync(std::filesystem::path source,
                                                                    std::filesystem::path outputDirectory,
                                                                    CompressOptions options)
    {
        // All of it is CPU work, and the encoders are the expensive part by orders of magnitude — so
        // the whole thing goes to a background thread and the render thread never sees it.
        co_await kor::Context::SwitchToBackgroundThread();
        auto result = CompressToKtx(source, outputDirectory, options);
        co_await kor::Context::SwitchToMainThread();
        co_return std::move(result);
    }
}
