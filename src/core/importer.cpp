//
// Created by radue on 2/23/2026.
//

module;

#include <OpenImageIO/imageio.h>
#include <vulkan/vulkan.hpp>
#include <ktx.h>
#include <GL/glew.h>
#include <glm/glm.hpp>

module gfx;
import :importer;

import :image;
import :buffer;
import :context;
import :commandBuffer;

gfx::Image::Format getFormat(const OIIO::TypeDesc& type, const int channels) {
    switch (type.basetype) {
        case OIIO::TypeDesc::UINT8: {
            switch (channels) {
                case 1: return gfx::Image::Format::eR8_UNORM;
                case 2: return gfx::Image::Format::eRG8_UNORM;
                case 4: return gfx::Image::Format::eRGBA8_UNORM;
                default: throw std::runtime_error("Unknown format");
            }
        }
        case OIIO::TypeDesc::INT8: {
            switch (channels) {
                case 1: return gfx::Image::Format::eR8_SINT;
                case 2: return gfx::Image::Format::eRG8_SINT;
                case 4: return gfx::Image::Format::eRGBA8_SINT;
                default: throw std::runtime_error("Unknown format");
            }
        }
        case OIIO::TypeDesc::UINT16: {
            switch (channels) {
                case 1: return gfx::Image::Format::eR16_UNORM;
                case 2: return gfx::Image::Format::eRG16_UNORM;
                case 4: return gfx::Image::Format::eRGBA16_UNORM;
                default: throw std::runtime_error("Unknown format");
            }
        }
        case OIIO::TypeDesc::INT16: {
            switch (channels) {
                case 1: return gfx::Image::Format::eR16_SINT;
                case 2: return gfx::Image::Format::eRG16_SINT;
                case 4: return gfx::Image::Format::eRGBA16_SINT;
                default: throw std::runtime_error("Unknown format");
            }
        }
        case OIIO::TypeDesc::INT32: {
            switch (channels) {
                case 1: return gfx::Image::Format::eR32_SINT;
                case 2: return gfx::Image::Format::eRG32_SINT;
                case 4: return gfx::Image::Format::eRGBA32_SINT;
                default: throw std::runtime_error("Unknown format");
            }
        }
        case OIIO::TypeDesc::UINT32: {
            switch (channels) {
                case 1: return gfx::Image::Format::eR32_UINT;
                case 2: return gfx::Image::Format::eRG32_UINT;
                case 4: return gfx::Image::Format::eRGBA32_UINT;
                default: throw std::runtime_error("Unknown format");
            }
        }
        case OIIO::TypeDesc::FLOAT: {
            switch (channels) {
                case 1: return gfx::Image::Format::eR32_SFLOAT;
                case 2: return gfx::Image::Format::eRG32_SFLOAT;
                case 4: return gfx::Image::Format::eRGBA32_SFLOAT;
                default: throw std::runtime_error("Unknown format");
            }
        }
        default: throw std::runtime_error("Unsupported format");
    }
}

namespace gfx
{
    Resource<Image> Importer::LoadImage(const std::filesystem::path& path, bool generateMipmaps)
    {
        const auto imageInput = OIIO::ImageInput::open(path.string());
        if (!imageInput) {
            throw std::runtime_error("Could not open image file: " + path.string() + "\nError: " + OIIO::geterror());
        }

        glm::u32 layerCount = 0;
        while (imageInput->seek_subimage(static_cast<int>(layerCount), 0)) {
            layerCount++;
        }

        glm::u32 mipLevels = 0;
        while (imageInput->seek_subimage(0, static_cast<int>(mipLevels)) && !generateMipmaps) {
            mipLevels++;
        }

        auto spec = imageInput->spec();
        // The engine has no 3-channel (RGB) format, so an RGB source is uploaded as RGBA with an
        // opaque alpha channel. The native vs. upload channel counts stay fixed for every mip/layer.
        const int nativeChannels = spec.nchannels;
        const int uploadChannels = (nativeChannels == 3) ? 4 : nativeChannels;
        spec.nchannels = uploadChannels;

        auto image = gfx::Image::Builder()
            .setExtent({ spec.width, spec.height, spec.depth })
            .setFormat(getFormat(spec.format, spec.nchannels))
            .setMipLevels(mipLevels)
            .setArrayLayers(layerCount)
            .addUsage(gfx::Image::Usage::eTransferSrc)
            .addUsage(gfx::Image::Usage::eTransferDst)
            .build();

        for (glm::u32 layer = 0; layer < layerCount; layer++) {
            if (generateMipmaps) {
                mipLevels = 1;
            }

            for (glm::u32 mip = 0; mip < mipLevels; mip++) {
                imageInput->seek_subimage(static_cast<int>(layer), static_cast<int>(mip));
                spec = imageInput->spec();

                const std::size_t texelCount =
                    static_cast<std::size_t>(spec.width) * spec.height * spec.depth;
                const std::size_t channelSize = spec.format.size();

                std::vector<unsigned char> nativeData(texelCount * nativeChannels * channelSize);
                if (!imageInput->read_image(static_cast<int>(layer), 0, 0, nativeChannels, spec.format, nativeData.data())) {
                    std::cerr << "Could not read image data: " << imageInput->geterror() << std::endl;
                }

                std::vector<unsigned char> data;
                if (uploadChannels != nativeChannels) {
                    // Widen each texel to RGBA, leaving the added alpha bytes as 0xFF (opaque for 8/16-bit formats).
                    data.assign(texelCount * uploadChannels * channelSize, 0xFF);
                    for (std::size_t texel = 0; texel < texelCount; ++texel) {
                        std::memcpy(
                            data.data() + texel * uploadChannels * channelSize,
                            nativeData.data() + texel * nativeChannels * channelSize,
                            static_cast<std::size_t>(nativeChannels) * channelSize);
                    }
                } else {
                    data = std::move(nativeData);
                }

                const auto stagingBuffer = gfx::Buffer::Builder<unsigned char>()
                    .setDataView(data)
                    .setUsage(gfx::Buffer::Usage::eTransferSrc)
                    .setType(gfx::Buffer::Type::eStaging)
                    .build();

                CommandBuffer::SingleTimeCommand([&](CommandBuffer& commandBuffer) {
                    commandBuffer.CopyBufferToImage(stagingBuffer, image, gfx::Copy {
                        .imageOffset = { 0, 0, 0 },
                        .imageExtent = { spec.width, spec.height, spec.depth },
                        .imageBaseArrayLayer = layer,
                        .imageLayerCount = 1,
                        .imageMipLevel = 0,
                    });
                });
            }
        }
        if (generateMipmaps) {
            CommandBuffer::SingleTimeCommand([&](CommandBuffer& commandBuffer) {
                commandBuffer.GenerateMipmaps(image);
            });
        }
        imageInput->close();

        return image;
    }

    struct KtxTextureDeleter {
        void operator()(ktxTexture* t) const noexcept {
            if (t) {
                ktxTexture_Destroy(t); // macro is fine when invoked
            }
        }
    };

    gfx::Image::Format GetFormatFromGl(const ktx_uint32_t glInternalFormat) {
        switch (glInternalFormat) {
            case GL_R8: return gfx::Image::Format::eR8_UNORM;
            case GL_RG8: return gfx::Image::Format::eRG8_UNORM;
            case GL_RGBA8: return gfx::Image::Format::eRGBA8_UNORM;
            case GL_SRGB8_ALPHA8: return gfx::Image::Format::eRGBA8_SRGB;

            case GL_R16: return gfx::Image::Format::eR16_UNORM;
            case GL_RG16: return gfx::Image::Format::eRG16_UNORM;
            case GL_RGBA16: return gfx::Image::Format::eRGBA16_UNORM;
            case GL_RGBA16F: return gfx::Image::Format::eRGBA16_SFLOAT;

            case GL_R32F: return gfx::Image::Format::eR32_SFLOAT;
            case GL_RG32F: return gfx::Image::Format::eRG32_SFLOAT;
            case GL_RGBA32F: return gfx::Image::Format::eRGBA32_SFLOAT;

            case GL_COMPRESSED_RGBA_BPTC_UNORM_ARB: return gfx::Image::Format::eBC7_UNORM;
            case GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM_ARB: return gfx::Image::Format::eBC7_SRGB;

            default: {
                std::cerr << "Unknown format: " << glInternalFormat << std::endl;
                throw std::runtime_error("Unsupported KTX glInternalformat: " + std::to_string(glInternalFormat));
            }
        }
    }

    gfx::Image::Format GetFormatFromVk(const ktx_uint32_t vkFormat) {
        switch (vkFormat) {
            case VK_FORMAT_R8_UNORM: return gfx::Image::Format::eR8_UNORM;
            case VK_FORMAT_R8G8_UNORM: return gfx::Image::Format::eRG8_UNORM;
            case VK_FORMAT_R8G8B8A8_UNORM: return gfx::Image::Format::eRGBA8_UNORM;
            case VK_FORMAT_R8G8B8A8_SRGB: return gfx::Image::Format::eRGBA8_SRGB;

            case VK_FORMAT_R16_UNORM: return gfx::Image::Format::eR16_UNORM;
            case VK_FORMAT_R16G16_UNORM: return gfx::Image::Format::eRG16_UNORM;
            case VK_FORMAT_R16G16B16A16_UNORM: return gfx::Image::Format::eRGBA16_UNORM;
            case VK_FORMAT_R16G16B16A16_SFLOAT: return gfx::Image::Format::eRGBA16_SFLOAT;

            case VK_FORMAT_R32_SFLOAT: return gfx::Image::Format::eR32_SFLOAT;
            case VK_FORMAT_R32G32_SFLOAT: return gfx::Image::Format::eRG32_SFLOAT;
            case VK_FORMAT_R32G32B32A32_SFLOAT: return gfx::Image::Format::eRGBA32_SFLOAT;

            case VK_FORMAT_BC7_UNORM_BLOCK: return gfx::Image::Format::eBC7_UNORM;
            case VK_FORMAT_BC7_SRGB_BLOCK: return gfx::Image::Format::eBC7_SRGB;

            default: {
                std::cerr << "Unknown format: " << vkFormat << std::endl;
                throw std::runtime_error("Unsupported KTX VkFormat: " + std::to_string(vkFormat));
            }
        }
    }

    gfx::Image::Type GetImageTypeFromKtx(const ktxTexture* tex) {
        if (tex->baseDepth > 1) return gfx::Image::Type::e3D;
        // Treat 1xN / 1x1 textures as 2D rather than 1D: material textures are
        // sampled as texture2D, and block-compressed formats (BC7) are only valid
        // for 2D images, so a 1x1 normal map must not become a 1D image.
        return gfx::Image::Type::e2D;
    }

    Task<void> LoadKTXAsync(const std::filesystem::path& path, const bool generateMipmaps, Resource<Image>& returnImage) {
        std::unique_ptr<ktxTexture, KtxTextureDeleter> texture = nullptr;
        Image::Format format;
        // Set when image data is already resident in CPU memory (e.g. after a
        // Basis transcode below), so the deferred load further down is skipped.
        bool imageDataLoaded = false;

        if (path.extension() == ".ktx") {
            ktxTexture1* rawTexture = nullptr;
            const KTX_error_code createResult = ktxTexture1_CreateFromNamedFile(
                path.string().c_str(),
                KTX_TEXTURE_CREATE_NO_FLAGS, // metadata only (phase 1)
                &rawTexture
            );

            if (createResult != KTX_SUCCESS || !rawTexture) {
                std::cerr << "Could not open KTX file: " << path.string()
                          << " error=" << ktxErrorString(createResult) << std::endl;
                co_return;
            }

            format = GetFormatFromGl(rawTexture->glInternalformat);
            texture = std::unique_ptr<ktxTexture, KtxTextureDeleter>(ktxTexture(rawTexture));
        }
        else if (path.extension() == ".ktx2") {
            ktxTexture2* rawTexture = nullptr;
            const KTX_error_code createResult = ktxTexture2_CreateFromNamedFile(
                path.string().c_str(),
                KTX_TEXTURE_CREATE_NO_FLAGS, // metadata only (phase 1)
                &rawTexture
            );

            if (createResult != KTX_SUCCESS || !rawTexture) {
                std::cerr << "Could not open KTX file: " << path.string()
                          << " error=" << ktxErrorString(createResult) << std::endl;
                co_return;
            }

            // Basis-supercompressed KTX2 (ETC1S / UASTC, as produced via
            // KHR_texture_basisu) report VK_FORMAT_UNDEFINED until transcoded.
            // Transcode to BC7 — a GPU-native block format we sample directly —
            // so such assets load without any offline preprocessing step.
            // Transcoding needs the image data resident, so we load it here
            // (in phase 1, before the image handle is published) and flag it so
            // the deferred background load further down is skipped.
            if (ktxTexture2_NeedsTranscoding(rawTexture)) {
                const KTX_error_code dataResult =
                    ktxTexture_LoadImageData(ktxTexture(rawTexture), nullptr, 0);
                if (dataResult != KTX_SUCCESS) {
                    std::cerr << "Could not load KTX image data for transcode: " << path.string()
                              << " error=" << ktxErrorString(dataResult) << std::endl;
                    ktxTexture_Destroy(ktxTexture(rawTexture));
                    co_return;
                }

                // Single-mip Basis -> RGBA8 so a mip chain can be generated at
                // load (block-compressed BC7 can't be blit-downsampled, and the
                // missing mips cause severe normal-map aliasing / shading grain).
                // Multi-mip Basis already carries mips, so keep it compact as BC7.
                const ktx_transcode_fmt_e target =
                    (rawTexture->numLevels <= 1) ? KTX_TTF_RGBA32 : KTX_TTF_BC7_RGBA;
                const KTX_error_code transcodeResult =
                    ktxTexture2_TranscodeBasis(rawTexture, target, 0);
                if (transcodeResult != KTX_SUCCESS) {
                    std::cerr << "Basis -> BC7 transcode failed: " << path.string()
                              << " error=" << ktxErrorString(transcodeResult) << std::endl;
                    ktxTexture_Destroy(ktxTexture(rawTexture));
                    co_return;
                }
                imageDataLoaded = true;
            }

            format = GetFormatFromVk(rawTexture->vkFormat);
            texture = std::unique_ptr<ktxTexture, KtxTextureDeleter>(ktxTexture(rawTexture));
        } else {
            throw std::runtime_error("Unsupported KTX file extension: " + path.extension().string());
        }

        const glm::u32 layers = std::max<glm::u32>(1u, texture->numLayers);
        const glm::u32 faces  = std::max<glm::u32>(1u, texture->numFaces);
        const glm::u32 arrayLayers = layers * faces;
        const glm::u32 fileMipLevels = std::max<glm::u32>(1u, texture->numLevels);

        // Block-compressed formats (e.g. BC7, including our Basis->BC7 transcode)
        // cannot be mip-generated via vkCmdBlitImage. Never auto-expand the mip
        // chain or generate mips for them — use exactly the levels in the file.
        const bool isBlockCompressed =
            format == Image::Format::eBC7_UNORM || format == Image::Format::eBC7_SRGB;

        // If KTX already contains mips, use them. Otherwise optionally
        // auto-generate (uncompressed formats only).
        const glm::u32 mipLevels = (fileMipLevels > 1)
            ? fileMipLevels
            : ((generateMipmaps && !isBlockCompressed) ? 0u : 1u);

        // Phase 1: publish image handle before any suspend (matches your async contract)
        returnImage = Image::Builder()
            .setType(GetImageTypeFromKtx(texture.get()))
            .setExtent({ texture->baseWidth, texture->baseHeight, texture->baseDepth })
            .setArrayLayers(arrayLayers)
            .setMipLevels(mipLevels)
            .setFormat(format)
            .addUsage(Image::Usage::eTransferSrc)
            .addUsage(Image::Usage::eTransferDst)
            .build();

        ResourceRef<const Image> image = returnImage;

        // Phase 2a: disk/data load off main thread
        co_await Context::SwitchToBackgroundThread();

        if (!imageDataLoaded) {
            const KTX_error_code loadResult = ktxTexture_LoadImageData(texture.get(), nullptr, 0);
            if (loadResult != KTX_SUCCESS || texture->pData == nullptr) {
                std::cerr << "Could not load KTX image data: " << path.string()
                          << " error=" << ktxErrorString(loadResult) << std::endl;
                co_return;
            }
        }

        struct UploadSlice {
            glm::u32 mip;
            glm::u32 layer;
            ktx_size_t offset;
            size_t size;
        };
        std::vector<UploadSlice> uploads;
        uploads.reserve(fileMipLevels * arrayLayers);

        for (glm::u32 mip = 0; mip < fileMipLevels; ++mip) {
            const size_t perImageSize = ktxTexture_GetImageSize(texture.get(), mip);

            for (glm::u32 layer = 0; layer < layers; ++layer) {
                for (glm::u32 face = 0; face < faces; ++face) {
                    ktx_size_t offset = 0;
                    const KTX_error_code offResult = ktxTexture_GetImageOffset(texture.get(), mip, layer, face, &offset);
                    if (offResult != KTX_SUCCESS) {
                        std::cerr << "KTX offset query failed: " << ktxErrorString(offResult) << std::endl;
                        co_return;
                    }

                    uploads.push_back(UploadSlice{
                        mip,
                        layer * faces + face, // flatten layer+face into array layer
                        offset,
                        perImageSize
                    });
                }
            }
        }

        // Phase 2b: GPU upload on main/render thread
        co_await Context::SwitchToMainThread();

        for (const auto&[mip, layer, offset, size] : uploads) {
            const auto stagingBuffer = Buffer::Builder<unsigned char>()
                .setDataView(std::span{texture->pData + offset, size})
                .setUsage(Buffer::Usage::eTransferSrc)
                .setType(Buffer::Type::eStaging)
                .build();
            CommandBuffer::SingleTimeCommand([&](CommandBuffer& commandBuffer) {
                commandBuffer.CopyBufferToImage(stagingBuffer, image, gfx::Copy {
                    .imageOffset = { 0, 0, 0 },
                    .imageExtent = {
                        std::max(1u, texture->baseWidth >> mip),
                        std::max(1u, texture->baseHeight >> mip),
                        std::max(1u, texture->baseDepth >> mip)
                    },
                    .imageBaseArrayLayer = layer,
                    .imageLayerCount = 1,
                    .imageMipLevel = mip,
                });
            });

            if (generateMipmaps && fileMipLevels == 1 && !isBlockCompressed) {
                CommandBuffer::SingleTimeCommand([&](CommandBuffer& commandBuffer) {
                    commandBuffer.GenerateMipmaps(image);
                });
            }
        }
        co_return;
    }

    Task<void> LoadRegularImageAsync(const std::filesystem::path path, const bool generateMipmaps, Resource<Image>& returnImage) {
        const auto image_input = OIIO::ImageInput::open(path.string());
        if (!image_input) {
            std::cerr << "Could not open image file: " << OIIO::geterror() << std::endl;
            co_return;
        }

        auto spec = image_input->spec();

        returnImage = Image::Builder()
            .setExtent({ spec.width, spec.height, spec.depth })
            .setFormat(getFormat(spec.format, spec.nchannels == 3 ? 4 : spec.nchannels))
            .setMipLevels(generateMipmaps ? 0 : 1)
            .addUsage(Image::Usage::eTransferSrc)
            .addUsage(Image::Usage::eTransferDst)
            .build();

        ResourceRef<const Image> image = returnImage;

        co_await Context::SwitchToBackgroundThread();

        std::vector<unsigned char> data(spec.width * spec.height * spec.depth * spec.nchannels * spec.format.size());
        if (!image_input->read_image(0, 0, 0, spec.nchannels, spec.format, data.data())) {
            std::cerr << "Could not read image data: " << image_input->geterror() << std::endl;
            co_return;
        }
        image_input->close();

        if (spec.nchannels == 3) {
            std::vector<unsigned char> rgbaData(spec.width * spec.height * spec.depth * 4 * spec.format.size());
            for (size_t i = 0; i < spec.width * spec.height * spec.depth; ++i) {
                std::copy_n(&data[i * 3 * spec.format.size()], spec.format.size() * 3, &rgbaData[i * 4 * spec.format.size()]);
                std::fill_n(&rgbaData[i * 4 * spec.format.size() + 3 * spec.format.size()], spec.format.size(), 255); // set alpha to 255
            }
            data = std::move(rgbaData);
        }

        co_await Context::SwitchToMainThread();

        const auto stagingBuffer = Buffer::Builder<unsigned char>()
                .setDataView(data)
                .setUsage(Buffer::Usage::eTransferSrc)
                .setType(Buffer::Type::eStaging)
                .build();
        CommandBuffer::SingleTimeCommand([&](CommandBuffer& commandBuffer) {
            commandBuffer.CopyBufferToImage(stagingBuffer, image, gfx::Copy {
                .imageOffset = { 0, 0, 0 },
                .imageExtent = { spec.width, spec.height, spec.depth },
                .imageBaseArrayLayer = 0,
                .imageLayerCount = 1,
                .imageMipLevel = 0,
            });
            if (generateMipmaps) {
                commandBuffer.GenerateMipmaps(image);
            }
        });
        co_return;
    }

    Task<void> Importer::LoadImageAsync(const std::filesystem::path& path, const bool generateMipmaps, Resource<Image>& returnImage) {
        if (path.extension() == ".ktx" || path.extension() == ".ktx2") {
            return LoadKTXAsync(path, generateMipmaps, returnImage);
        }
        return LoadRegularImageAsync(path, generateMipmaps, returnImage);
    }

    void Importer::SaveImage(const std::filesystem::path &path, const std::string& name, FileFormat fileFormat, ResourceRef<const Image> image) {
        const auto imageOutput = OIIO::ImageOutput::create(path.string());
        if (!imageOutput) {
            throw std::runtime_error("Could not create image file: " + path.string() + "\nError: " + OIIO::geterror());
        }

        OIIO::ImageSpec spec(image->getExtent().x, image->getExtent().y, 4, OIIO::TypeDesc::UINT8);
        if (!imageOutput->open(path.string(), spec)) {
            throw std::runtime_error("Could not open image file for writing: " + path.string() + "\nError: " + OIIO::geterror());
        }


        std::vector<std::byte> data;
        CommandBuffer::Create(gfx::CommandBuffer::Usage::eGraphics)->Run([&](CommandBuffer& commandBuffer) {
            const auto stagingBuffer = Buffer::Builder<glm::u8vec4>()
                .setInstanceCount(image->getExtent().x * image->getExtent().y * image->getExtent().z)
                .setUsage(Buffer::Usage::eTransferDst)
                .setType(Buffer::Type::eStaging)
                .build();

            commandBuffer.CopyImageToBuffer(image, stagingBuffer, gfx::Copy {
                .imageOffset = { 0, 0, 0 },
                .imageExtent = image->getExtent(),
                .imageBaseArrayLayer = 0,
                .imageLayerCount = 1,
                .imageMipLevel = 0,
            });

            data = stagingBuffer->Read<std::byte>(stagingBuffer->getSize());
        });

        if (!imageOutput->write_image(OIIO::TypeDesc::UINT8, data.data())) {
            throw std::runtime_error("Could not write image data: " + path.string() + "\nError: " + OIIO::geterror());
        }

        imageOutput->close();
    }

    std::unique_ptr<Importer> Importer::Load(const std::filesystem::path &path) {
        return std::make_unique<AssimpImporter>(path);
    }
}
