//
// Created by radue on 2/23/2026.
//

#include <span>
#include <cstring>

#include "image.h"

#include "buffer.h"

#include "assimpImporter/assimpImporter.h"

#include <OpenImageIO/imageio.h>
#include <vulkan/vulkan.hpp>
#include <ktx.h>
#include <GL/glew.h>

#include "context.h"


kor::Image::Format getFormat(const OIIO::TypeDesc& type, const int channels) {
    switch (type.basetype) {
        case OIIO::TypeDesc::UINT8: {
            switch (channels) {
                case 1: return kor::Image::Format::eR8_UNORM;
                case 2: return kor::Image::Format::eRG8_UNORM;
                case 4: return kor::Image::Format::eRGBA8_UNORM;
                default: throw std::runtime_error("Unknown format");
            }
        }
        case OIIO::TypeDesc::INT8: {
            switch (channels) {
                case 1: return kor::Image::Format::eR8_SINT;
                case 2: return kor::Image::Format::eRG8_SINT;
                case 4: return kor::Image::Format::eRGBA8_SINT;
                default: throw std::runtime_error("Unknown format");
            }
        }
        case OIIO::TypeDesc::UINT16: {
            switch (channels) {
                case 1: return kor::Image::Format::eR16_UNORM;
                case 2: return kor::Image::Format::eRG16_UNORM;
                case 4: return kor::Image::Format::eRGBA16_UNORM;
                default: throw std::runtime_error("Unknown format");
            }
        }
        case OIIO::TypeDesc::INT16: {
            switch (channels) {
                case 1: return kor::Image::Format::eR16_SINT;
                case 2: return kor::Image::Format::eRG16_SINT;
                case 4: return kor::Image::Format::eRGBA16_SINT;
                default: throw std::runtime_error("Unknown format");
            }
        }
        case OIIO::TypeDesc::INT32: {
            switch (channels) {
                case 1: return kor::Image::Format::eR32_SINT;
                case 2: return kor::Image::Format::eRG32_SINT;
                case 4: return kor::Image::Format::eRGBA32_SINT;
                default: throw std::runtime_error("Unknown format");
            }
        }
        case OIIO::TypeDesc::UINT32: {
            switch (channels) {
                case 1: return kor::Image::Format::eR32_UINT;
                case 2: return kor::Image::Format::eRG32_UINT;
                case 4: return kor::Image::Format::eRGBA32_UINT;
                default: throw std::runtime_error("Unknown format");
            }
        }
        case OIIO::TypeDesc::FLOAT: {
            switch (channels) {
                case 1: return kor::Image::Format::eR32_SFLOAT;
                case 2: return kor::Image::Format::eRG32_SFLOAT;
                case 4: return kor::Image::Format::eRGBA32_SFLOAT;
                default: throw std::runtime_error("Unknown format");
            }
        }
        default: throw std::runtime_error("Unsupported format");
    }
}

namespace kor
{
    kor::Resource<Image> Importer::LoadImage(const std::filesystem::path& path, bool generateMipmaps)
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
        if (spec.nchannels == 3) {
            spec.nchannels = 4;
        }

        auto image = kor::Image::Builder()
            .setExtent({ spec.width, spec.height, spec.depth })
            .setFormat(getFormat(spec.format, spec.nchannels))
            .setMipLevels(mipLevels)
            .setArrayLayers(layerCount)
            .addUsage(kor::Image::Usage::eTransferSrc)
            .addUsage(kor::Image::Usage::eTransferDst)
            .build();

        for (glm::u32 layer = 0; layer < layerCount; layer++) {
            if (generateMipmaps) {
                mipLevels = 1;
            }

            for (glm::u32 mip = 0; mip < mipLevels; mip++) {
                imageInput->seek_subimage(static_cast<int>(layer), static_cast<int>(mip));
                const auto mipSpec = imageInput->spec();

                // The image was created as RGBA (3-channel sources are promoted to 4 above),
                // so the upload must be 4-channel too. Read the file's native channels and,
                // for RGB sources, expand to RGBA with an opaque alpha — otherwise the staging
                // buffer would be 3/4 the size the copy expects.
                const int srcChannels = mipSpec.nchannels;
                const int dstChannels = (srcChannels == 3) ? 4 : srcChannels;
                const auto bytesPerChannel = static_cast<size_t>(mipSpec.format.size());
                const auto texelCount = static_cast<size_t>(mipSpec.width) * mipSpec.height * mipSpec.depth;

                std::vector<unsigned char> data(texelCount * dstChannels * bytesPerChannel);
                if (srcChannels == dstChannels) {
                    if (!imageInput->read_image(static_cast<int>(layer), 0, 0, srcChannels, mipSpec.format, data.data())) {
                        std::cerr << "Could not read image data: " << imageInput->geterror() << std::endl;
                    }
                } else {
                    std::vector<unsigned char> src(texelCount * srcChannels * bytesPerChannel);
                    if (!imageInput->read_image(static_cast<int>(layer), 0, 0, srcChannels, mipSpec.format, src.data())) {
                        std::cerr << "Could not read image data: " << imageInput->geterror() << std::endl;
                    }
                    // Interleave src channels into the wider destination, filling the added
                    // alpha channel with the format's "opaque" value (0xFF for 8-bit, else 0).
                    const unsigned char alphaFill = (bytesPerChannel == 1) ? 0xFF : 0x00;
                    for (size_t t = 0; t < texelCount; ++t) {
                        unsigned char* dstTexel = data.data() + t * dstChannels * bytesPerChannel;
                        const unsigned char* srcTexel = src.data() + t * srcChannels * bytesPerChannel;
                        std::memcpy(dstTexel, srcTexel, static_cast<size_t>(srcChannels) * bytesPerChannel);
                        for (int c = srcChannels; c < dstChannels; ++c) {
                            unsigned char* channel = dstTexel + static_cast<size_t>(c) * bytesPerChannel;
                            std::memset(channel, (c == 3) ? alphaFill : 0x00, bytesPerChannel);
                        }
                    }
                }

                const auto stagingBuffer = kor::Buffer::Builder<unsigned char>()
                    .setDataView(data)
                    .setUsage(kor::Buffer::Usage::eTransferSrc)
                    .setType(kor::Buffer::Type::eStaging)
                    .build();

                CommandBuffer::SingleTimeCommand([&](CommandBuffer& commandBuffer) {
                    commandBuffer.CopyBufferToImage(stagingBuffer, image, kor::Copy {
                        .imageOffset = { 0, 0, 0 },
                        .imageExtent = { mipSpec.width, mipSpec.height, mipSpec.depth },
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

        // Leave the texture in a shader-readable layout. The copy/mip commands above leave
        // it in eTransferDstOptimal, but descriptors bind sampled images as
        // eShaderReadOnlyOptimal; transition here so it can be sampled directly. The
        // single-time submit completes before any frame renders, so no later barrier is
        // needed.
        CommandBuffer::SingleTimeCommand([&](CommandBuffer& commandBuffer) {
            commandBuffer.Barrier({}, {{ image, ResourceAccess::AllShaderRead }});
        });

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

    kor::Image::Format GetFormatFromGl(const ktx_uint32_t glInternalFormat) {
        switch (glInternalFormat) {
            case GL_R8: return kor::Image::Format::eR8_UNORM;
            case GL_RG8: return kor::Image::Format::eRG8_UNORM;
            case GL_RGBA8: return kor::Image::Format::eRGBA8_UNORM;
            case GL_SRGB8_ALPHA8: return kor::Image::Format::eRGBA8_SRGB;

            case GL_R16: return kor::Image::Format::eR16_UNORM;
            case GL_RG16: return kor::Image::Format::eRG16_UNORM;
            case GL_RGBA16: return kor::Image::Format::eRGBA16_UNORM;
            case GL_RGBA16F: return kor::Image::Format::eRGBA16_SFLOAT;

            case GL_R32F: return kor::Image::Format::eR32_SFLOAT;
            case GL_RG32F: return kor::Image::Format::eRG32_SFLOAT;
            case GL_RGBA32F: return kor::Image::Format::eRGBA32_SFLOAT;

            case GL_COMPRESSED_RGBA_BPTC_UNORM_ARB: return kor::Image::Format::eBC7_UNORM;
            case GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM_ARB: return kor::Image::Format::eBC7_SRGB;

            default: {
                std::cerr << "Unknown format: " << glInternalFormat << std::endl;
                throw std::runtime_error("Unsupported KTX glInternalformat: " + std::to_string(glInternalFormat));
            }
        }
    }

    kor::Image::Format GetFormatFromVk(const ktx_uint32_t vkFormat) {
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

            case VK_FORMAT_BC7_UNORM_BLOCK: return kor::Image::Format::eBC7_UNORM;
            case VK_FORMAT_BC7_SRGB_BLOCK: return kor::Image::Format::eBC7_SRGB;

            default: {
                std::cerr << "Unknown format: " << vkFormat << std::endl;
                throw std::runtime_error("Unsupported KTX VkFormat: " + std::to_string(vkFormat));
            }
        }
    }

    kor::Image::Type GetImageTypeFromKtx(const ktxTexture* tex) {
        if (tex->baseDepth > 1) return kor::Image::Type::e3D;
        if (tex->baseHeight > 1) return kor::Image::Type::e2D;
        return kor::Image::Type::e1D;
    }

    Task<Resource<Image>> LoadKTXAsync(const std::filesystem::path path, const bool generateMipmaps) {
        // All file I/O, metadata and slice computation runs on a background thread.
        co_await Context::SwitchToBackgroundThread();

        std::unique_ptr<ktxTexture, KtxTextureDeleter> texture = nullptr;
        Image::Format format;

        if (path.extension() == ".ktx") {
            ktxTexture1* rawTexture = nullptr;
            const KTX_error_code createResult = ktxTexture1_CreateFromNamedFile(
                path.string().c_str(), KTX_TEXTURE_CREATE_NO_FLAGS, &rawTexture);
            if (createResult != KTX_SUCCESS || !rawTexture) {
                std::cerr << "Could not open KTX file: " << path.string()
                          << " error=" << ktxErrorString(createResult) << std::endl;
                co_return Resource<Image>(nullptr);
            }
            format = GetFormatFromGl(rawTexture->glInternalformat);
            texture = std::unique_ptr<ktxTexture, KtxTextureDeleter>(ktxTexture(rawTexture));
        }
        else if (path.extension() == ".ktx2") {
            ktxTexture2* rawTexture = nullptr;
            const KTX_error_code createResult = ktxTexture2_CreateFromNamedFile(
                path.string().c_str(), KTX_TEXTURE_CREATE_NO_FLAGS, &rawTexture);
            if (createResult != KTX_SUCCESS || !rawTexture) {
                std::cerr << "Could not open KTX file: " << path.string()
                          << " error=" << ktxErrorString(createResult) << std::endl;
                co_return Resource<Image>(nullptr);
            }
            format = GetFormatFromVk(rawTexture->vkFormat);
            texture = std::unique_ptr<ktxTexture, KtxTextureDeleter>(ktxTexture(rawTexture));
        } else {
            std::cerr << "Unsupported KTX file extension: " << path.extension().string() << std::endl;
            co_return Resource<Image>(nullptr);
        }

        const glm::u32 layers = std::max<glm::u32>(1u, texture->numLayers);
        const glm::u32 faces  = std::max<glm::u32>(1u, texture->numFaces);
        const glm::u32 arrayLayers = layers * faces;
        const glm::u32 fileMipLevels = std::max<glm::u32>(1u, texture->numLevels);
        // If KTX already contains mips, use them. Otherwise optionally auto-generate.
        const glm::u32 mipLevels = (fileMipLevels > 1) ? fileMipLevels : (generateMipmaps ? 0u : 1u);
        const auto imageType = GetImageTypeFromKtx(texture.get());
        const glm::u32 baseW = texture->baseWidth, baseH = texture->baseHeight, baseD = texture->baseDepth;

        const KTX_error_code loadResult = ktxTexture_LoadImageData(texture.get(), nullptr, 0);
        if (loadResult != KTX_SUCCESS || texture->pData == nullptr) {
            std::cerr << "Could not load KTX image data: " << path.string()
                      << " error=" << ktxErrorString(loadResult) << std::endl;
            co_return Resource<Image>(nullptr);
        }

        struct UploadSlice { glm::u32 mip; glm::u32 layer; ktx_size_t offset; size_t size; };
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
                        co_return Resource<Image>(nullptr);
                    }
                    uploads.push_back(UploadSlice{ mip, layer * faces + face, offset, perImageSize });
                }
            }
        }

        // Build the GPU image on the main thread.
        co_await Context::SwitchToMainThread();
        auto image = Image::Builder()
            .setType(imageType)
            .setExtent({ baseW, baseH, baseD })
            .setArrayLayers(arrayLayers)
            .setMipLevels(mipLevels)
            .setFormat(format)
            .addUsage(Image::Usage::eTransferSrc)
            .addUsage(Image::Usage::eTransferDst)
            .build();
        ResourceRef imageRef = image;

        // Upload one slice per short main-thread visit, releasing the render thread between slices.
        for (const auto&[mip, layer, offset, size] : uploads) {
            const auto stagingBuffer = Buffer::Builder<unsigned char>()
                .setDataView(std::span{ texture->pData + offset, size })
                .setUsage(Buffer::Usage::eTransferSrc)
                .setType(Buffer::Type::eStaging)
                .build();
            CommandBuffer::SingleTimeCommand([&](CommandBuffer& commandBuffer) {
                commandBuffer.CopyBufferToImage(stagingBuffer, imageRef, kor::Copy {
                    .imageOffset = { 0, 0, 0 },
                    .imageExtent = {
                        std::max(1u, baseW >> mip),
                        std::max(1u, baseH >> mip),
                        std::max(1u, baseD >> mip)
                    },
                    .imageBaseArrayLayer = layer,
                    .imageLayerCount = 1,
                    .imageMipLevel = mip,
                });
            });
            // Round-trip background→main re-enqueues the continuation on the main queue so
            // rendering (and other main-thread work) interleaves between uploads.
            co_await Context::SwitchToBackgroundThread();
            co_await Context::SwitchToMainThread();
        }

        if (generateMipmaps && fileMipLevels == 1) {
            CommandBuffer::SingleTimeCommand([&](CommandBuffer& commandBuffer) {
                commandBuffer.GenerateMipmaps(imageRef);
            });
        }

        co_return std::move(image);
    }

    Task<Resource<Image>> LoadRegularImageAsync(const std::filesystem::path path, const bool generateMipmaps) {
        // Open + decode + RGB→RGBA expansion on a background thread.
        co_await Context::SwitchToBackgroundThread();

        const auto image_input = OIIO::ImageInput::open(path.generic_string());
        if (!image_input) {
            std::cerr << "Could not open image file: " << OIIO::geterror() << std::endl;
            co_return Resource<Image>(nullptr);
        }
        auto spec = image_input->spec();

        std::vector<unsigned char> data(spec.width * spec.height * spec.depth * spec.nchannels * spec.format.size());
        if (!image_input->read_image(0, 0, 0, spec.nchannels, spec.format, data.data())) {
            std::cerr << "Could not read image data: " << image_input->geterror() << std::endl;
            co_return Resource<Image>(nullptr);
        }
        image_input->close();

        if (spec.nchannels == 3) {
            std::vector<unsigned char> rgbaData(spec.width * spec.height * spec.depth * 4 * spec.format.size());
            for (size_t i = 0; i < static_cast<size_t>(spec.width) * spec.height * spec.depth; ++i) {
                std::copy_n(&data[i * 3 * spec.format.size()], spec.format.size() * 3, &rgbaData[i * 4 * spec.format.size()]);
                std::fill_n(&rgbaData[i * 4 * spec.format.size() + 3 * spec.format.size()], spec.format.size(), 255); // alpha = 255
            }
            data = std::move(rgbaData);
        }

        // Build the GPU image + upload on the main thread.
        co_await Context::SwitchToMainThread();
        auto image = Image::Builder()
            .setExtent({ spec.width, spec.height, spec.depth })
            .setFormat(getFormat(spec.format, spec.nchannels == 3 ? 4 : spec.nchannels))
            .setMipLevels(generateMipmaps ? 0 : 1)
            .addUsage(Image::Usage::eTransferSrc)
            .addUsage(Image::Usage::eTransferDst)
            .build();
        ResourceRef imageRef = image;

        const auto stagingBuffer = Buffer::Builder<unsigned char>()
                .setDataView(data)
                .setUsage(Buffer::Usage::eTransferSrc)
                .setType(Buffer::Type::eStaging)
                .build();
        CommandBuffer::SingleTimeCommand([&](CommandBuffer& commandBuffer) {
            commandBuffer.CopyBufferToImage(stagingBuffer, imageRef, kor::Copy {
                .imageOffset = { 0, 0, 0 },
                .imageExtent = { spec.width, spec.height, spec.depth },
                .imageBaseArrayLayer = 0,
                .imageLayerCount = 1,
                .imageMipLevel = 0,
            });
        });

        if (generateMipmaps) {
            // Mip generation as its own short main-thread visit.
            co_await Context::SwitchToBackgroundThread();
            co_await Context::SwitchToMainThread();
            CommandBuffer::SingleTimeCommand([&](CommandBuffer& commandBuffer) {
                commandBuffer.GenerateMipmaps(imageRef);
            });
        }

        co_return std::move(image);
    }

    Task<Resource<Image>> Importer::LoadImageAsync(const std::filesystem::path& path, const bool generateMipmaps) {
        if (path.extension() == ".ktx" || path.extension() == ".ktx2")
            return LoadKTXAsync(path, generateMipmaps);
        return LoadRegularImageAsync(path, generateMipmaps);
    }

    void Importer::SaveImage(const std::filesystem::path &path, const std::string& name, FileFormat fileFormat, kor::ResourceRef<const Image> image) {
        const auto imageOutput = OIIO::ImageOutput::create(path.string());
        if (!imageOutput) {
            throw std::runtime_error("Could not create image file: " + path.string() + "\nError: " + OIIO::geterror());
        }

        OIIO::ImageSpec spec(image->getExtent().x, image->getExtent().y, 4, OIIO::TypeDesc::UINT8);
        if (!imageOutput->open(path.string(), spec)) {
            throw std::runtime_error("Could not open image file for writing: " + path.string() + "\nError: " + OIIO::geterror());
        }


        const auto stagingBuffer = Buffer::Builder<glm::u8vec4>()
            .setInstanceCount(image->getExtent().x * image->getExtent().y * image->getExtent().z)
            .setUsage(Buffer::Usage::eTransferDst)
            .setType(Buffer::Type::eStaging)
            .build();

        // Copy the image into the staging buffer and wait for the GPU before reading
        // it back — SingleTimeCommand begins/submits/fences the copy, unlike a bare
        // Run() which would leave the buffer empty.
        CommandBuffer::SingleTimeCommand([&](CommandBuffer& commandBuffer) {
            commandBuffer.CopyImageToBuffer(image, stagingBuffer, kor::Copy {
                .imageOffset = { 0, 0, 0 },
                .imageExtent = image->getExtent(),
                .imageBaseArrayLayer = 0,
                .imageLayerCount = 1,
                .imageMipLevel = 0,
            });
        }, CommandBuffer::Usage::eTransfer);

        const std::vector<std::byte> data = stagingBuffer->Read<std::byte>(stagingBuffer->getSize());

        if (!imageOutput->write_image(OIIO::TypeDesc::UINT8, data.data())) {
            throw std::runtime_error("Could not write image data: " + path.string() + "\nError: " + OIIO::geterror());
        }

        imageOutput->close();
    }

    std::unique_ptr<Importer> Importer::Load(const std::filesystem::path &path) {
        return std::make_unique<AssimpImporter>(path);
    }
}
