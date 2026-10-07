//
// Created by radue on 29.07.2026.
//

#include "decode.h"

#include <algorithm>
#include <cstring>
#include <format>

#include <OpenImageIO/imageio.h>
#include <ktx.h>
#include <vulkan/vulkan_core.h>

#include <buffer.h>
#include <commandBuffer.h>
#include <context.h>
#include <log.h>

namespace kimg::detail
{
    kor::Error fileError(const std::filesystem::path& path, std::string what)
    {
        return kor::Error{ .code = kor::ErrorCode::eInvalidArgument,
                           .message = std::format("{}: {}", path.string(), std::move(what)) };
    }

    kor::Resource<kor::Image> poisoned(const std::filesystem::path& path, std::string what)
    {
        auto error = fileError(path, std::move(what));
        kor::log::Error("[image] {}", error.message);
        return kor::Resource<kor::Image>::Failed(std::move(error), "Image");
    }

    std::expected<kor::Image::Format, kor::Error> formatFrom(const OIIO::TypeDesc& type, const int channels)
    {
        // No GPU format has three channels, so a three-channel source is widened to four before it
        // gets here; anything else is the file saying something we have no texture format for.
        const auto unsupported = [&]() -> std::expected<kor::Image::Format, kor::Error> {
            return std::unexpected(kor::Error{
                .code = kor::ErrorCode::eInvalidArgument,
                .message = std::format("no image format for {} channels of {}",
                                       channels, type.c_str()) });
        };

        switch (type.basetype) {
        case OIIO::TypeDesc::UINT8:
            switch (channels) {
            case 1: return kor::Image::Format::eR8_UNORM;
            case 2: return kor::Image::Format::eRG8_UNORM;
            case 4: return kor::Image::Format::eRGBA8_UNORM;
            default: return unsupported();
            }
        case OIIO::TypeDesc::INT8:
            switch (channels) {
            case 1: return kor::Image::Format::eR8_SINT;
            case 2: return kor::Image::Format::eRG8_SINT;
            case 4: return kor::Image::Format::eRGBA8_SINT;
            default: return unsupported();
            }
        case OIIO::TypeDesc::UINT16:
            switch (channels) {
            case 1: return kor::Image::Format::eR16_UNORM;
            case 2: return kor::Image::Format::eRG16_UNORM;
            case 4: return kor::Image::Format::eRGBA16_UNORM;
            default: return unsupported();
            }
        case OIIO::TypeDesc::INT16:
            switch (channels) {
            case 1: return kor::Image::Format::eR16_SINT;
            case 2: return kor::Image::Format::eRG16_SINT;
            case 4: return kor::Image::Format::eRGBA16_SINT;
            default: return unsupported();
            }
        case OIIO::TypeDesc::INT32:
            switch (channels) {
            case 1: return kor::Image::Format::eR32_SINT;
            case 2: return kor::Image::Format::eRG32_SINT;
            case 4: return kor::Image::Format::eRGBA32_SINT;
            default: return unsupported();
            }
        case OIIO::TypeDesc::UINT32:
            switch (channels) {
            case 1: return kor::Image::Format::eR32_UINT;
            case 2: return kor::Image::Format::eRG32_UINT;
            case 4: return kor::Image::Format::eRGBA32_UINT;
            default: return unsupported();
            }
        case OIIO::TypeDesc::HALF:
            // Half floats are what an EXR or an .hdr decoded at half precision arrives as. Read as
            // 16-bit floats rather than promoted, so a panorama keeps its range without doubling.
            switch (channels) {
            case 1: return kor::Image::Format::eR16_SFLOAT;
            case 2: return kor::Image::Format::eRG16_SFLOAT;
            case 4: return kor::Image::Format::eRGBA16_SFLOAT;
            default: return unsupported();
            }
        case OIIO::TypeDesc::FLOAT:
            switch (channels) {
            case 1: return kor::Image::Format::eR32_SFLOAT;
            case 2: return kor::Image::Format::eRG32_SFLOAT;
            case 4: return kor::Image::Format::eRGBA32_SFLOAT;
            default: return unsupported();
            }
        default:
            return unsupported();
        }
    }

    std::expected<CpuImage, kor::Error> decodeFile(const std::filesystem::path& path)
    {
        const auto input = OIIO::ImageInput::open(path.string());
        if (!input) {
            return std::unexpected(fileError(path, "could not be opened (" + OIIO::geterror() + ")"));
        }

        const auto spec = input->spec();

        const int srcChannels = spec.nchannels;
        const int dstChannels = (srcChannels == 3) ? 4 : srcChannels;
        const auto bytesPerChannel = static_cast<std::size_t>(spec.format.size());
        const auto texelCount = static_cast<std::size_t>(spec.width) * spec.height * spec.depth;

        auto format = formatFrom(spec.format, dstChannels);
        if (!format) {
            input->close();
            return std::unexpected(fileError(path, std::string(format.error().message)));
        }

        CpuImage decoded;
        decoded.extent = { spec.width, spec.height, spec.depth };
        decoded.format = *format;
        decoded.pixels.resize(texelCount * dstChannels * bytesPerChannel);

        if (srcChannels == dstChannels) {
            if (!input->read_image(0, 0, 0, srcChannels, spec.format, decoded.pixels.data())) {
                auto why = input->geterror();
                input->close();
                return std::unexpected(fileError(path, "could not be decoded (" + why + ")"));
            }
        } else {
            // Three channels in, four out: read the source and interleave it into the wider
            // destination, filling the alpha the GPU format insists on with "opaque".
            std::vector<unsigned char> source(texelCount * srcChannels * bytesPerChannel);
            if (!input->read_image(0, 0, 0, srcChannels, spec.format, source.data())) {
                auto why = input->geterror();
                input->close();
                return std::unexpected(fileError(path, "could not be decoded (" + why + ")"));
            }

            const unsigned char alphaFill = (bytesPerChannel == 1) ? 0xFF : 0x00;
            for (std::size_t t = 0; t < texelCount; ++t) {
                unsigned char* destination = decoded.pixels.data() + t * dstChannels * bytesPerChannel;
                const unsigned char* texel = source.data() + t * srcChannels * bytesPerChannel;
                std::memcpy(destination, texel, static_cast<std::size_t>(srcChannels) * bytesPerChannel);
                for (int c = srcChannels; c < dstChannels; ++c) {
                    std::memset(destination + static_cast<std::size_t>(c) * bytesPerChannel,
                                (c == 3) ? alphaFill : 0x00, bytesPerChannel);
                }
            }

            // A float source has no "0xFF is opaque" — write a real 1.0 into the alpha instead.
            if (bytesPerChannel == 4 && spec.format.basetype == OIIO::TypeDesc::FLOAT) {
                for (std::size_t t = 0; t < texelCount; ++t) {
                    auto* alpha = reinterpret_cast<float*>(
                        decoded.pixels.data() + t * dstChannels * bytesPerChannel) + 3;
                    *alpha = 1.f;
                }
            }
        }

        input->close();
        return decoded;
    }

    // ---- upload ---------------------------------------------------------------------------------

    void uploadSlice(const kor::ResourceRef<const kor::Image>& image, const std::span<const unsigned char> bytes,
                     const kor::UVec3 extent, const kor::u32 layer, const kor::u32 mip)
    {
        const auto staging = kor::Buffer::Builder<unsigned char>()
            .SetDataView(bytes)
            .SetUsage(kor::Buffer::Usage::eTransferSrc)
            .SetType(kor::Buffer::Type::eStaging)
            .Build();

        // Not waited for: the GPU orders what reads the image after it (CommandBuffer::Upload).
        (void)kor::CommandBuffer::Upload([&](kor::CommandBuffer& commandBuffer) {
            commandBuffer.CopyBufferToImage(staging, image, kor::Copy {
                .imageOffset = { 0, 0, 0 },
                .imageExtent = kor::IVec3(extent),
                .imageBaseArrayLayer = layer,
                .imageLayerCount = 1,
                .imageMipLevel = mip,
            });
        }, kor::CommandBuffer::Usage::eGraphics);
    }

    void finishUpload(const kor::ResourceRef<const kor::Image>& image, const bool generateMipmaps)
    {
        // A compressed image cannot be blitted into, and mips are made by blitting: it carries the
        // chain it was encoded with or it has none. The engine refuses this anyway — skipping it here
        // is what keeps the refusal out of the log for a caller who simply passed `true`.
        const bool mips = generateMipmaps && !kor::Image::IsBlockCompressed(image->PixelFormat());

        (void)kor::CommandBuffer::Upload([&](kor::CommandBuffer& commandBuffer) {
            if (mips) commandBuffer.GenerateMipmaps(image);
            // Ready for the first way a shader can read it: the read-only layout is invalid for an
            // image that isn't sampled, so a storage one goes to the general layout, and one no
            // shader reads stays where the copy left it.
            const auto usage = image->UsageFlags();
            if (usage & kor::Image::Usage::eSampled)
                commandBuffer.Barrier({}, {{ image, kor::ResourceAccess::eAllShaderRead }});
            else if (usage & kor::Image::Usage::eStorage)
                commandBuffer.Barrier({}, {{ image, kor::ResourceAccess::eAllShaderReadWrite }});
        }, kor::CommandBuffer::Usage::eGraphics);
    }

    // ---- KTX ------------------------------------------------------------------------------------

    namespace
    {
        struct KtxTextureDeleter {
            void operator()(ktxTexture* t) const noexcept {
                if (t) ktxTexture_Destroy(t);   // the macro is fine when invoked
            }
        };

        // The GL internal formats a KTX1 file names its format with — Khronos-assigned values, so
        // written out here rather than taken from a GL header this module would otherwise need.
        namespace gl {
            constexpr ktx_uint32_t COMPRESSED_R11_EAC = 0x9270;
            constexpr ktx_uint32_t COMPRESSED_RED_RGTC1 = 0x8DBB;
            constexpr ktx_uint32_t COMPRESSED_RG11_EAC = 0x9272;
            constexpr ktx_uint32_t COMPRESSED_RGB8_ETC2 = 0x9274;
            constexpr ktx_uint32_t COMPRESSED_RGBA8_ETC2_EAC = 0x9278;
            constexpr ktx_uint32_t COMPRESSED_RGBA_ASTC_4x4_KHR = 0x93B0;
            constexpr ktx_uint32_t COMPRESSED_RGBA_ASTC_6x6_KHR = 0x93B4;
            constexpr ktx_uint32_t COMPRESSED_RGBA_ASTC_8x8_KHR = 0x93B7;
            constexpr ktx_uint32_t COMPRESSED_RGBA_BPTC_UNORM_ARB = 0x8E8C;
            constexpr ktx_uint32_t COMPRESSED_RGBA_S3TC_DXT1_EXT = 0x83F1;
            constexpr ktx_uint32_t COMPRESSED_RGBA_S3TC_DXT3_EXT = 0x83F2;
            constexpr ktx_uint32_t COMPRESSED_RGBA_S3TC_DXT5_EXT = 0x83F3;
            constexpr ktx_uint32_t COMPRESSED_RGB_BPTC_SIGNED_FLOAT_ARB = 0x8E8E;
            constexpr ktx_uint32_t COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_ARB = 0x8E8F;
            constexpr ktx_uint32_t COMPRESSED_RGB_S3TC_DXT1_EXT = 0x83F0;
            constexpr ktx_uint32_t COMPRESSED_RG_RGTC2 = 0x8DBD;
            constexpr ktx_uint32_t COMPRESSED_SIGNED_R11_EAC = 0x9271;
            constexpr ktx_uint32_t COMPRESSED_SIGNED_RED_RGTC1 = 0x8DBC;
            constexpr ktx_uint32_t COMPRESSED_SIGNED_RG11_EAC = 0x9273;
            constexpr ktx_uint32_t COMPRESSED_SIGNED_RG_RGTC2 = 0x8DBE;
            constexpr ktx_uint32_t COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR = 0x93D0;
            constexpr ktx_uint32_t COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR = 0x93D4;
            constexpr ktx_uint32_t COMPRESSED_SRGB8_ALPHA8_ASTC_8x8_KHR = 0x93D7;
            constexpr ktx_uint32_t COMPRESSED_SRGB8_ALPHA8_ETC2_EAC = 0x9279;
            constexpr ktx_uint32_t COMPRESSED_SRGB8_ETC2 = 0x9275;
            constexpr ktx_uint32_t COMPRESSED_SRGB_ALPHA_BPTC_UNORM_ARB = 0x8E8D;
            constexpr ktx_uint32_t COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT = 0x8C4D;
            constexpr ktx_uint32_t COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT = 0x8C4E;
            constexpr ktx_uint32_t COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT = 0x8C4F;
            constexpr ktx_uint32_t COMPRESSED_SRGB_S3TC_DXT1_EXT = 0x8C4C;
            constexpr ktx_uint32_t R16 = 0x822A;
            constexpr ktx_uint32_t R32F = 0x822E;
            constexpr ktx_uint32_t R8 = 0x8229;
            constexpr ktx_uint32_t RG16 = 0x822C;
            constexpr ktx_uint32_t RG32F = 0x8230;
            constexpr ktx_uint32_t RG8 = 0x822B;
            constexpr ktx_uint32_t RGBA16 = 0x805B;
            constexpr ktx_uint32_t RGBA16F = 0x881A;
            constexpr ktx_uint32_t RGBA32F = 0x8814;
            constexpr ktx_uint32_t RGBA8 = 0x8058;
            constexpr ktx_uint32_t SRGB8_ALPHA8 = 0x8C43;
        }

        std::expected<kor::Image::Format, kor::Error> formatFromGl(const ktx_uint32_t glInternalFormat) {
            switch (glInternalFormat) {
                case gl::R8: return kor::Image::Format::eR8_UNORM;
                case gl::RG8: return kor::Image::Format::eRG8_UNORM;
                case gl::RGBA8: return kor::Image::Format::eRGBA8_UNORM;
                case gl::SRGB8_ALPHA8: return kor::Image::Format::eRGBA8_SRGB;

                case gl::R16: return kor::Image::Format::eR16_UNORM;
                case gl::RG16: return kor::Image::Format::eRG16_UNORM;
                case gl::RGBA16: return kor::Image::Format::eRGBA16_UNORM;
                case gl::RGBA16F: return kor::Image::Format::eRGBA16_SFLOAT;

                case gl::R32F: return kor::Image::Format::eR32_SFLOAT;
                case gl::RG32F: return kor::Image::Format::eRG32_SFLOAT;
                case gl::RGBA32F: return kor::Image::Format::eRGBA32_SFLOAT;

                case gl::COMPRESSED_RGB_S3TC_DXT1_EXT: return kor::Image::Format::eBC1_RGB_UNORM;
                case gl::COMPRESSED_SRGB_S3TC_DXT1_EXT: return kor::Image::Format::eBC1_RGB_SRGB;
                case gl::COMPRESSED_RGBA_S3TC_DXT1_EXT: return kor::Image::Format::eBC1_RGBA_UNORM;
                case gl::COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT: return kor::Image::Format::eBC1_RGBA_SRGB;
                case gl::COMPRESSED_RGBA_S3TC_DXT3_EXT: return kor::Image::Format::eBC2_UNORM;
                case gl::COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT: return kor::Image::Format::eBC2_SRGB;
                case gl::COMPRESSED_RGBA_S3TC_DXT5_EXT: return kor::Image::Format::eBC3_UNORM;
                case gl::COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT: return kor::Image::Format::eBC3_SRGB;
                case gl::COMPRESSED_RED_RGTC1: return kor::Image::Format::eBC4_UNORM;
                case gl::COMPRESSED_SIGNED_RED_RGTC1: return kor::Image::Format::eBC4_SNORM;
                case gl::COMPRESSED_RG_RGTC2: return kor::Image::Format::eBC5_UNORM;
                case gl::COMPRESSED_SIGNED_RG_RGTC2: return kor::Image::Format::eBC5_SNORM;
                case gl::COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_ARB: return kor::Image::Format::eBC6H_UFLOAT;
                case gl::COMPRESSED_RGB_BPTC_SIGNED_FLOAT_ARB: return kor::Image::Format::eBC6H_SFLOAT;
                case gl::COMPRESSED_RGBA_BPTC_UNORM_ARB: return kor::Image::Format::eBC7_UNORM;
                case gl::COMPRESSED_SRGB_ALPHA_BPTC_UNORM_ARB: return kor::Image::Format::eBC7_SRGB;

                case gl::COMPRESSED_RGBA_ASTC_4x4_KHR: return kor::Image::Format::eASTC_4x4_UNORM;
                case gl::COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR: return kor::Image::Format::eASTC_4x4_SRGB;
                case gl::COMPRESSED_RGBA_ASTC_6x6_KHR: return kor::Image::Format::eASTC_6x6_UNORM;
                case gl::COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR: return kor::Image::Format::eASTC_6x6_SRGB;
                case gl::COMPRESSED_RGBA_ASTC_8x8_KHR: return kor::Image::Format::eASTC_8x8_UNORM;
                case gl::COMPRESSED_SRGB8_ALPHA8_ASTC_8x8_KHR: return kor::Image::Format::eASTC_8x8_SRGB;

                case gl::COMPRESSED_RGB8_ETC2: return kor::Image::Format::eETC2_RGB8_UNORM;
                case gl::COMPRESSED_SRGB8_ETC2: return kor::Image::Format::eETC2_RGB8_SRGB;
                case gl::COMPRESSED_RGBA8_ETC2_EAC: return kor::Image::Format::eETC2_RGBA8_UNORM;
                case gl::COMPRESSED_SRGB8_ALPHA8_ETC2_EAC: return kor::Image::Format::eETC2_RGBA8_SRGB;
                case gl::COMPRESSED_R11_EAC: return kor::Image::Format::eEAC_R11_UNORM;
                case gl::COMPRESSED_SIGNED_R11_EAC: return kor::Image::Format::eEAC_R11_SNORM;
                case gl::COMPRESSED_RG11_EAC: return kor::Image::Format::eEAC_RG11_UNORM;
                case gl::COMPRESSED_SIGNED_RG11_EAC: return kor::Image::Format::eEAC_RG11_SNORM;

                default:
                    return std::unexpected(kor::Error{ .code = kor::ErrorCode::eInvalidArgument,
                        .message = std::format("unsupported KTX glInternalformat {}", glInternalFormat) });
            }
        }

        std::expected<kor::Image::Format, kor::Error> formatFromVk(const ktx_uint32_t vkFormat) {
            switch (vkFormat) {
                case VK_FORMAT_R8_UNORM: return kor::Image::Format::eR8_UNORM;
                case VK_FORMAT_R8G8_UNORM: return kor::Image::Format::eRG8_UNORM;
                case VK_FORMAT_R8G8B8A8_UNORM: return kor::Image::Format::eRGBA8_UNORM;
                case VK_FORMAT_R8G8B8A8_SRGB: return kor::Image::Format::eRGBA8_SRGB;

                case VK_FORMAT_R16_UNORM: return kor::Image::Format::eR16_UNORM;
                case VK_FORMAT_R16G16_UNORM: return kor::Image::Format::eRG16_UNORM;
                case VK_FORMAT_R16G16B16A16_UNORM: return kor::Image::Format::eRGBA16_UNORM;
                case VK_FORMAT_R16G16B16A16_SFLOAT: return kor::Image::Format::eRGBA16_SFLOAT;

                case VK_FORMAT_R32_SFLOAT: return kor::Image::Format::eR32_SFLOAT;
                case VK_FORMAT_R32G32_SFLOAT: return kor::Image::Format::eRG32_SFLOAT;
                case VK_FORMAT_R32G32B32A32_SFLOAT: return kor::Image::Format::eRGBA32_SFLOAT;

                case VK_FORMAT_BC1_RGB_UNORM_BLOCK: return kor::Image::Format::eBC1_RGB_UNORM;
                case VK_FORMAT_BC1_RGB_SRGB_BLOCK: return kor::Image::Format::eBC1_RGB_SRGB;
                case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: return kor::Image::Format::eBC1_RGBA_UNORM;
                case VK_FORMAT_BC1_RGBA_SRGB_BLOCK: return kor::Image::Format::eBC1_RGBA_SRGB;
                case VK_FORMAT_BC2_UNORM_BLOCK: return kor::Image::Format::eBC2_UNORM;
                case VK_FORMAT_BC2_SRGB_BLOCK: return kor::Image::Format::eBC2_SRGB;
                case VK_FORMAT_BC3_UNORM_BLOCK: return kor::Image::Format::eBC3_UNORM;
                case VK_FORMAT_BC3_SRGB_BLOCK: return kor::Image::Format::eBC3_SRGB;
                case VK_FORMAT_BC4_UNORM_BLOCK: return kor::Image::Format::eBC4_UNORM;
                case VK_FORMAT_BC4_SNORM_BLOCK: return kor::Image::Format::eBC4_SNORM;
                case VK_FORMAT_BC5_UNORM_BLOCK: return kor::Image::Format::eBC5_UNORM;
                case VK_FORMAT_BC5_SNORM_BLOCK: return kor::Image::Format::eBC5_SNORM;
                case VK_FORMAT_BC6H_UFLOAT_BLOCK: return kor::Image::Format::eBC6H_UFLOAT;
                case VK_FORMAT_BC6H_SFLOAT_BLOCK: return kor::Image::Format::eBC6H_SFLOAT;
                case VK_FORMAT_BC7_UNORM_BLOCK: return kor::Image::Format::eBC7_UNORM;
                case VK_FORMAT_BC7_SRGB_BLOCK: return kor::Image::Format::eBC7_SRGB;

                case VK_FORMAT_ASTC_4x4_UNORM_BLOCK: return kor::Image::Format::eASTC_4x4_UNORM;
                case VK_FORMAT_ASTC_4x4_SRGB_BLOCK: return kor::Image::Format::eASTC_4x4_SRGB;
                case VK_FORMAT_ASTC_6x6_UNORM_BLOCK: return kor::Image::Format::eASTC_6x6_UNORM;
                case VK_FORMAT_ASTC_6x6_SRGB_BLOCK: return kor::Image::Format::eASTC_6x6_SRGB;
                case VK_FORMAT_ASTC_8x8_UNORM_BLOCK: return kor::Image::Format::eASTC_8x8_UNORM;
                case VK_FORMAT_ASTC_8x8_SRGB_BLOCK: return kor::Image::Format::eASTC_8x8_SRGB;

                case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK: return kor::Image::Format::eETC2_RGB8_UNORM;
                case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK: return kor::Image::Format::eETC2_RGB8_SRGB;
                case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK: return kor::Image::Format::eETC2_RGBA8_UNORM;
                case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK: return kor::Image::Format::eETC2_RGBA8_SRGB;
                case VK_FORMAT_EAC_R11_UNORM_BLOCK: return kor::Image::Format::eEAC_R11_UNORM;
                case VK_FORMAT_EAC_R11_SNORM_BLOCK: return kor::Image::Format::eEAC_R11_SNORM;
                case VK_FORMAT_EAC_R11G11_UNORM_BLOCK: return kor::Image::Format::eEAC_RG11_UNORM;
                case VK_FORMAT_EAC_R11G11_SNORM_BLOCK: return kor::Image::Format::eEAC_RG11_SNORM;

                default:
                    return std::unexpected(kor::Error{ .code = kor::ErrorCode::eInvalidArgument,
                        .message = std::format("unsupported KTX VkFormat {}", vkFormat) });
            }
        }

        kor::Image::Type imageTypeFromKtx(const ktxTexture* texture) {
            if (texture->baseDepth > 1) return kor::Image::Type::e3D;
            if (texture->baseHeight > 1) return kor::Image::Type::e2D;
            return kor::Image::Type::e1D;
        }

        /**
         * @brief Which block format to transcode a universal texture into on this device.
         *
         * A Basis-compressed KTX2 is not a GPU format at all: it is an intermediate that becomes one
         * here, and the choice belongs to the device. Best first — BC7 keeps the most of what UASTC
         * encoded — then the cheaper BC formats, then the mobile families, and last plain RGBA, which
         * every device has and which is what a machine with no block compression gets.
         */
        struct TranscodeTarget { ktx_transcode_fmt_e ktx; kor::Image::Format format; };

        TranscodeTarget transcodeTarget(const bool hasAlpha)
        {
            const TranscodeTarget candidates[] {
                { KTX_TTF_BC7_RGBA, kor::Image::Format::eBC7_UNORM },
                { hasAlpha ? KTX_TTF_BC3_RGBA : KTX_TTF_BC1_RGB,
                  hasAlpha ? kor::Image::Format::eBC3_UNORM : kor::Image::Format::eBC1_RGB_UNORM },
                { KTX_TTF_ASTC_4x4_RGBA, kor::Image::Format::eASTC_4x4_UNORM },
                { hasAlpha ? KTX_TTF_ETC2_RGBA : KTX_TTF_ETC1_RGB,
                  hasAlpha ? kor::Image::Format::eETC2_RGBA8_UNORM : kor::Image::Format::eETC2_RGB8_UNORM },
            };

            for (const auto& candidate : candidates) {
                if (kor::Image::IsFormatSupported(candidate.format)) return candidate;
            }
            // Uncompressed, four times the memory, and always available. Better a texture than none.
            return { KTX_TTF_RGBA32, kor::Image::Format::eRGBA8_UNORM };
        }
    }

    bool isKtx(const std::filesystem::path& path)
    {
        const auto extension = path.extension();
        return extension == ".ktx" || extension == ".ktx2";
    }

    std::expected<KtxImage, kor::Error> readKtx(const std::filesystem::path& path)
    {
        std::unique_ptr<ktxTexture, KtxTextureDeleter> texture;
        std::expected<kor::Image::Format, kor::Error> format;

        if (path.extension() == ".ktx") {
            ktxTexture1* raw = nullptr;
            const KTX_error_code result = ktxTexture1_CreateFromNamedFile(
                path.string().c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &raw);
            if (result != KTX_SUCCESS || !raw)
                return std::unexpected(fileError(path, std::format("could not be opened ({})", ktxErrorString(result))));
            format = formatFromGl(raw->glInternalformat);
            texture.reset(ktxTexture(raw));
        }
        else if (path.extension() == ".ktx2") {
            ktxTexture2* raw = nullptr;
            // The data is loaded here rather than later: transcoding a universal texture *replaces*
            // it, and asking libktx to load it afterwards is refused as a state error — which is
            // exactly the shape of bug that only shows up once something actually transcodes.
            const KTX_error_code result = ktxTexture2_CreateFromNamedFile(
                path.string().c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &raw);
            if (result != KTX_SUCCESS || !raw)
                return std::unexpected(fileError(path, std::format("could not be opened ({})", ktxErrorString(result))));

            // A universal texture — UASTC or ETC1S — has to become a real GPU format before it can be
            // uploaded, and which one depends on the device. Shipping one file that works everywhere
            // is the whole point of it. @see kimg::CompressToKtx
            if (ktxTexture2_NeedsTranscoding(raw)) {
                const bool hasAlpha = ktxTexture2_GetNumComponents(raw) == 4;
                const auto target = transcodeTarget(hasAlpha);
                const KTX_error_code transcoded = ktxTexture2_TranscodeBasis(raw, target.ktx, 0);
                if (transcoded != KTX_SUCCESS) {
                    ktxTexture_Destroy(ktxTexture(raw));
                    return std::unexpected(fileError(path,
                        std::format("could not be transcoded ({})", ktxErrorString(transcoded))));
                }
                format = target.format;
            } else {
                format = formatFromVk(raw->vkFormat);
            }
            texture.reset(ktxTexture(raw));
        }
        else {
            return std::unexpected(fileError(path, "is not a KTX file"));
        }

        if (!format) return std::unexpected(fileError(path, std::string(format.error().message)));

        KtxImage ktx;
        ktx.format = *format;
        ktx.type = imageTypeFromKtx(texture.get());
        ktx.extent = { texture->baseWidth, texture->baseHeight, texture->baseDepth };
        const kor::u32 layers = std::max<kor::u32>(1u, texture->numLayers);
        const kor::u32 faces  = std::max<kor::u32>(1u, texture->numFaces);
        ktx.arrayLayers = layers * faces;
        ktx.fileMipLevels = std::max<kor::u32>(1u, texture->numLevels);

        if (texture->pData == nullptr)
            return std::unexpected(fileError(path, "the file carries no image data"));

        ktx.slices.reserve(static_cast<std::size_t>(ktx.fileMipLevels) * ktx.arrayLayers);
        for (kor::u32 mip = 0; mip < ktx.fileMipLevels; ++mip) {
            const std::size_t perImage = ktxTexture_GetImageSize(texture.get(), mip);
            for (kor::u32 layer = 0; layer < layers; ++layer) {
                for (kor::u32 face = 0; face < faces; ++face) {
                    ktx_size_t offset = 0;
                    const KTX_error_code query = ktxTexture_GetImageOffset(texture.get(), mip, layer, face, &offset);
                    if (query != KTX_SUCCESS)
                        return std::unexpected(fileError(path,
                            std::format("slice offset query failed ({})", ktxErrorString(query))));
                    ktx.slices.push_back(KtxImage::Slice{ mip, layer * faces + face, offset, perImage });
                }
            }
        }

        ktx.data = texture->pData;
        // The pixel data belongs to the texture, so the texture must outlive the plan that points into
        // it: ownership goes to the shared_ptr the caller keeps.
        ktx.texture = std::shared_ptr<void>(texture.release(), [](void* p) {
            KtxTextureDeleter{}(static_cast<ktxTexture*>(p));
        });
        return ktx;
    }

    kor::Resource<kor::Image> makeKtxImage(const KtxImage& ktx, const bool generateMipmaps)
    {
        // Checked before the image is built rather than after: a format this device does not have
        // fails at creation, and every upload that follows then fails too. One error naming the
        // format is worth more than twenty saying the image is unusable.
        if (!kor::Image::IsFormatSupported(ktx.format)) {
            auto error = kor::Error{ .code = kor::ErrorCode::eInvalidArgument,
                .message = std::format("this device does not support image format {}, which the file is in",
                                       static_cast<int>(ktx.format)) };
            kor::log::Error("[image] {}", error.message);
            return kor::Resource<kor::Image>::Failed(std::move(error), "Image");
        }

        // The file's own mip chain wins; failing that, generate one if asked — unless the format is
        // compressed, which cannot be blitted into.
        const kor::u32 mipLevels = ktx.fileMipLevels > 1
            ? ktx.fileMipLevels
            : ((generateMipmaps && !kor::Image::IsBlockCompressed(ktx.format)) ? 0u : 1u);

        return kor::Image::Builder()
            .SetType(ktx.type)
            .SetExtent(ktx.extent)
            .SetArrayLayers(ktx.arrayLayers)
            .SetMipLevels(mipLevels)
            .SetFormat(ktx.format)


            .Build();
    }
}
