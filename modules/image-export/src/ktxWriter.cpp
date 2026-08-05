//
// Created by radue on 30.07.2026.
//

// Writing a whole image — every mip of every layer, compressed or not — as a KTX2. The only container
// here that can hold all of it, which is what makes it the way a texture leaves the engine and comes
// back in through kimg::LoadImage unchanged.

#include "writer.h"

#include <algorithm>
#include <format>

#include <ktx.h>
#include <vulkan/vulkan_core.h>

#include <commandBuffer.h>
#include <context.h>
#include <log.h>

namespace kimg
{
    namespace detail
    {
        namespace
        {
            /**
             * @brief The VkFormat a kor::Image::Format is, which is how a KTX2 names its format.
             *
             * A second table, deliberately: the engine's own conversion lives inside its Vulkan
             * backend and is not part of its interface, and a file format is not a backend concern —
             * a KTX2 written here is read by anything, on any API. Keep it in step with
             * kor::Image::Format; a format missing from it is refused by name rather than mis-written.
             */
            std::uint32_t vkFormatFor(const kor::Image::Format format)
            {
                using F = kor::Image::Format;
                switch (format) {
                case F::eR8_UNORM: return VK_FORMAT_R8_UNORM;
                case F::eR8_SNORM: return VK_FORMAT_R8_SNORM;
                case F::eR8_UINT: return VK_FORMAT_R8_UINT;
                case F::eR8_SINT: return VK_FORMAT_R8_SINT;
                case F::eRG8_UNORM: return VK_FORMAT_R8G8_UNORM;
                case F::eRG8_SNORM: return VK_FORMAT_R8G8_SNORM;
                case F::eRG8_UINT: return VK_FORMAT_R8G8_UINT;
                case F::eRG8_SINT: return VK_FORMAT_R8G8_SINT;
                case F::eRGB8_UNORM: return VK_FORMAT_R8G8B8_UNORM;
                case F::eRGB8_SRGB: return VK_FORMAT_R8G8B8_SRGB;
                case F::eRGBA8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
                case F::eRGBA8_SNORM: return VK_FORMAT_R8G8B8A8_SNORM;
                case F::eRGBA8_UINT: return VK_FORMAT_R8G8B8A8_UINT;
                case F::eRGBA8_SINT: return VK_FORMAT_R8G8B8A8_SINT;
                case F::eRGBA8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
                case F::eBGRA8_UNORM: return VK_FORMAT_B8G8R8A8_UNORM;
                case F::eBGRA8_SRGB: return VK_FORMAT_B8G8R8A8_SRGB;

                case F::eR16_UNORM: return VK_FORMAT_R16_UNORM;
                case F::eR16_SFLOAT: return VK_FORMAT_R16_SFLOAT;
                case F::eRG16_UNORM: return VK_FORMAT_R16G16_UNORM;
                case F::eRG16_SFLOAT: return VK_FORMAT_R16G16_SFLOAT;
                case F::eRGBA16_UNORM: return VK_FORMAT_R16G16B16A16_UNORM;
                case F::eRGBA16_SFLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;

                case F::eR32_UINT: return VK_FORMAT_R32_UINT;
                case F::eR32_SINT: return VK_FORMAT_R32_SINT;
                case F::eR32_SFLOAT: return VK_FORMAT_R32_SFLOAT;
                case F::eRG32_SFLOAT: return VK_FORMAT_R32G32_SFLOAT;
                case F::eRGBA32_UINT: return VK_FORMAT_R32G32B32A32_UINT;
                case F::eRGBA32_SINT: return VK_FORMAT_R32G32B32A32_SINT;
                case F::eRGBA32_SFLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;

                case F::eD16_UNORM: return VK_FORMAT_D16_UNORM;
                case F::eD32_SFLOAT: return VK_FORMAT_D32_SFLOAT;

                case F::eBC1_RGB_UNORM: return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
                case F::eBC1_RGB_SRGB: return VK_FORMAT_BC1_RGB_SRGB_BLOCK;
                case F::eBC1_RGBA_UNORM: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
                case F::eBC1_RGBA_SRGB: return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
                case F::eBC2_UNORM: return VK_FORMAT_BC2_UNORM_BLOCK;
                case F::eBC2_SRGB: return VK_FORMAT_BC2_SRGB_BLOCK;
                case F::eBC3_UNORM: return VK_FORMAT_BC3_UNORM_BLOCK;
                case F::eBC3_SRGB: return VK_FORMAT_BC3_SRGB_BLOCK;
                case F::eBC4_UNORM: return VK_FORMAT_BC4_UNORM_BLOCK;
                case F::eBC4_SNORM: return VK_FORMAT_BC4_SNORM_BLOCK;
                case F::eBC5_UNORM: return VK_FORMAT_BC5_UNORM_BLOCK;
                case F::eBC5_SNORM: return VK_FORMAT_BC5_SNORM_BLOCK;
                case F::eBC6H_UFLOAT: return VK_FORMAT_BC6H_UFLOAT_BLOCK;
                case F::eBC6H_SFLOAT: return VK_FORMAT_BC6H_SFLOAT_BLOCK;
                case F::eBC7_UNORM: return VK_FORMAT_BC7_UNORM_BLOCK;
                case F::eBC7_SRGB: return VK_FORMAT_BC7_SRGB_BLOCK;

                case F::eASTC_4x4_UNORM: return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
                case F::eASTC_4x4_SRGB: return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
                case F::eASTC_6x6_UNORM: return VK_FORMAT_ASTC_6x6_UNORM_BLOCK;
                case F::eASTC_6x6_SRGB: return VK_FORMAT_ASTC_6x6_SRGB_BLOCK;
                case F::eASTC_8x8_UNORM: return VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
                case F::eASTC_8x8_SRGB: return VK_FORMAT_ASTC_8x8_SRGB_BLOCK;

                case F::eETC2_RGB8_UNORM: return VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
                case F::eETC2_RGB8_SRGB: return VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK;
                case F::eETC2_RGBA8_UNORM: return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
                case F::eETC2_RGBA8_SRGB: return VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK;
                case F::eEAC_R11_UNORM: return VK_FORMAT_EAC_R11_UNORM_BLOCK;
                case F::eEAC_R11_SNORM: return VK_FORMAT_EAC_R11_SNORM_BLOCK;
                case F::eEAC_RG11_UNORM: return VK_FORMAT_EAC_R11G11_UNORM_BLOCK;
                case F::eEAC_RG11_SNORM: return VK_FORMAT_EAC_R11G11_SNORM_BLOCK;

                default: return VK_FORMAT_UNDEFINED;
                }
            }

            struct KtxTextureDeleter {
                void operator()(ktxTexture2* t) const noexcept { if (t) ktxTexture_Destroy(ktxTexture(t)); }
            };
        }

        std::expected<void, kor::Error> writeKtx2(const std::filesystem::path& target,
                                                 const kor::Image& image,
                                                 const std::vector<ReadBack>& slices)
        {
            const auto vkFormat = vkFormatFor(image.getFormat());
            if (vkFormat == VK_FORMAT_UNDEFINED) {
                return std::unexpected(fileError(target, std::format(
                    "there is no KTX2 format for this image's format ({})",
                    static_cast<int>(image.getFormat()))));
            }

            const bool wholeImage = slices.size() > 1
                || static_cast<glm::u32>(slices.size()) == image.getMipLevels() * image.getArrayLayers();
            const glm::u32 levels = wholeImage ? image.getMipLevels() : 1;
            const glm::u32 layers = wholeImage ? image.getArrayLayers() : 1;

            // Six 2D layers is a cube map as far as KTX is concerned, and saying so is what lets the
            // file be loaded back as one rather than as an array that happens to have six entries.
            const bool isCube = layers == 6 && image.getExtent().z == 1;

            ktxTextureCreateInfo info {};
            info.vkFormat = vkFormat;
            info.baseWidth = slices.empty() ? image.getExtent().x : slices.front().extent.x;
            info.baseHeight = slices.empty() ? image.getExtent().y : slices.front().extent.y;
            info.baseDepth = slices.empty() ? image.getExtent().z : slices.front().extent.z;
            info.numDimensions = image.getExtent().z > 1 ? 3 : (info.baseHeight > 1 ? 2 : 1);
            info.numLevels = levels;
            info.numLayers = isCube ? 1 : layers;
            info.numFaces = isCube ? 6 : 1;
            info.isArray = !isCube && layers > 1 ? KTX_TRUE : KTX_FALSE;
            info.generateMipmaps = KTX_FALSE;   // the levels are supplied, not computed

            ktxTexture2* raw = nullptr;
            if (const auto created = ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &raw);
                created != KTX_SUCCESS || !raw) {
                return std::unexpected(fileError(target,
                    std::format("could not be created ({})", ktxErrorString(created))));
            }
            const std::unique_ptr<ktxTexture2, KtxTextureDeleter> texture(raw);

            // The slices arrive in the order the caller read them: mip-major, layer-minor, which is
            // the order KTX stores them in too.
            std::size_t index = 0;
            for (glm::u32 level = 0; level < levels; ++level) {
                for (glm::u32 layer = 0; layer < layers; ++layer) {
                    if (index >= slices.size()) break;
                    const auto& slice = slices[index++];
                    const auto set = ktxTexture_SetImageFromMemory(
                        ktxTexture(texture.get()), level,
                        isCube ? 0 : layer, isCube ? layer : 0,
                        slice.bytes.data(), slice.bytes.size());
                    if (set != KTX_SUCCESS) {
                        return std::unexpected(fileError(target, std::format(
                            "level {} layer {} could not be stored ({})", level, layer, ktxErrorString(set))));
                    }
                }
            }

            if (const auto written = ktxTexture_WriteToNamedFile(ktxTexture(texture.get()), target.string().c_str());
                written != KTX_SUCCESS) {
                return std::unexpected(fileError(target,
                    std::format("could not be written ({})", ktxErrorString(written))));
            }
            return {};
        }
    }

    namespace
    {
        /** @brief Reads every mip of every layer back, in the order KTX stores them. */
        kor::Result<std::vector<detail::ReadBack>> readEverySlice(const kor::ResourceRef<const kor::Image>& image)
        {
            std::vector<detail::ReadBack> slices;
            slices.reserve(static_cast<std::size_t>(image->getMipLevels()) * image->getArrayLayers());

            for (glm::u32 level = 0; level < image->getMipLevels(); ++level) {
                for (glm::u32 layer = 0; layer < image->getArrayLayers(); ++layer) {
                    const auto extent = detail::mipExtent(*image, level);
                    auto data = detail::readBack(image, Subimage{
                        .mipLevel = level, .arrayLayer = layer, .offset = { 0, 0, 0 }, .extent = extent });
                    if (!data) return std::unexpected(data.error());
                    slices.push_back(std::move(*data));
                }
            }
            return slices;
        }

        kor::Result<std::filesystem::path> prepareSet(const std::filesystem::path& directory,
                                                     const std::string& name,
                                                     const kor::ResourceRef<const kor::Image>& image)
        {
            const auto target = directory / (name + std::string(extensionFor(FileFormat::eKTX2)));
            if (!image) return std::unexpected(detail::fileError(target, "cannot be written from an unusable image"));

            std::error_code ec;
            std::filesystem::create_directories(directory, ec);
            return target;
        }
    }

    kor::Result<std::filesystem::path> SaveImageSet(
        const std::filesystem::path& directory, const std::string& name,
        kor::ResourceRef<const kor::Image> image)
    {
        auto target = prepareSet(directory, name, image);
        if (!target) return std::unexpected(target.error());

        auto slices = readEverySlice(image);
        if (!slices) return std::unexpected(detail::fileError(*target, std::string(slices.error().message)));

        if (auto written = detail::writeKtx2(*target, *image, *slices); !written)
            return std::unexpected(written.error());
        return *target;
    }

    kor::Task<kor::Result<std::filesystem::path>> SaveImageSetAsync(
        std::filesystem::path directory, std::string name, kor::ResourceRef<const kor::Image> image)
    {
        auto target = prepareSet(directory, name, image);
        if (!target) co_return std::unexpected(target.error());

        // The read-backs are GPU work and stay on the main thread; assembling and writing the
        // container — which for a full mip chain is the expensive half — does not.
        auto slices = readEverySlice(image);
        if (!slices) co_return std::unexpected(detail::fileError(*target, std::string(slices.error().message)));

        co_await kor::Context::SwitchToBackgroundThread();
        auto written = detail::writeKtx2(*target, *image, *slices);
        co_await kor::Context::SwitchToMainThread();

        if (!written) co_return std::unexpected(written.error());
        co_return *target;
    }
}
