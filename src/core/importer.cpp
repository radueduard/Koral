//
// Created by radue on 2/23/2026.
//

#include <span>

#include "image.h"

#include "buffer.h"

#include "assimpImporter.h"

#include <OpenImageIO/imageio.h>
#include <vulkan/vulkan.h>
#include <ktx.h>

#include "context.h"


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
    std::unique_ptr<Image> Importer::LoadImage(const std::filesystem::path& path, bool generateMipmaps)
    {
        const auto imageInput = OIIO::ImageInput::open(path.string());
        if (!imageInput) {
            throw std::runtime_error("Could not open image file: " + path.string() + "\nError: " + OIIO::geterror());
        }

        auto spec = imageInput->spec();
        if (spec.nchannels == 3) {
            spec.nchannels = 4;
        }

        std::unique_ptr<gfx::Image> image = gfx::Image::Builder()
            .setExtent({ spec.width, spec.height, spec.depth })
            .setFormat(getFormat(spec.format, spec.nchannels))
            .setMipLevels(generateMipmaps ? 0 : 1)
            .addUsage(gfx::Image::Usage::eTransferDst)
            .build();


        std::vector<unsigned char> data(spec.width * spec.height * spec.depth * spec.nchannels * spec.format.size());
        if (!imageInput->read_image(0, 0, 0, spec.nchannels, spec.format, data.data())) {
            std::cerr << "Could not read image data: " << imageInput->geterror() << std::endl;

        }

        const auto stagingBuffer = gfx::Buffer::Builder<unsigned char>()
            .setDataView(data)
            .setUsage(gfx::Buffer::Usage::eTransferSrc)
            .setType(gfx::Buffer::Type::eStaging)
            .build();

        image->CopyFrom(*stagingBuffer, 0, 0);
        imageInput->close();

        if (generateMipmaps) {
            image->GenerateMipmaps();
        }

        return image;
    }

    struct KtxTextureDeleter {
        void operator()(ktxTexture* t) const noexcept {
            if (t) {
                ktxTexture_Destroy(t); // macro is fine when invoked
            }
        }
    };


    gfx::Image::Format GetFormatFromKtxVkFormat(const ktx_uint32_t vkFormat) {
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
        if (tex->baseHeight > 1) return gfx::Image::Type::e2D;
        return gfx::Image::Type::e1D;
    }

    Task<void> LoadKTXAsync(const std::filesystem::path& path, const bool generateMipmaps, std::shared_ptr<Image>& returnImage) {
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

        const std::unique_ptr<ktxTexture, KtxTextureDeleter> texture(ktxTexture(rawTexture));

        const glm::u32 layers = std::max<glm::u32>(1u, texture->numLayers);
        const glm::u32 faces  = std::max<glm::u32>(1u, texture->numFaces);
        const glm::u32 arrayLayers = layers * faces;
        const glm::u32 fileMipLevels = std::max<glm::u32>(1u, texture->numLevels);

        // If KTX already contains mips, use them. Otherwise optionally auto-generate.
        const glm::u32 mipLevels = (fileMipLevels > 1) ? fileMipLevels : (generateMipmaps ? 0u : 1u);

        // Phase 1: publish image handle before any suspend (matches your async contract)
        returnImage = Image::Builder()
            .setType(GetImageTypeFromKtx(texture.get()))
            .setExtent({ texture->baseWidth, texture->baseHeight, texture->baseDepth })
            .setArrayLayers(arrayLayers)
            .setMipLevels(mipLevels)
            .setFormat(GetFormatFromKtxVkFormat(rawTexture->vkFormat))
            .addUsage(Image::Usage::eTransferSrc)
            .addUsage(Image::Usage::eTransferDst)
            .build();

        const auto image = returnImage;

        // Phase 2a: disk/data load off main thread
        co_await Context::SwitchToBackgroundThread();

        const KTX_error_code loadResult = ktxTexture_LoadImageData(texture.get(), nullptr, 0);
        if (loadResult != KTX_SUCCESS || texture->pData == nullptr) {
            std::cerr << "Could not load KTX image data: " << path.string()
                      << " error=" << ktxErrorString(loadResult) << std::endl;
            co_return;
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

            image->CopyFrom(*stagingBuffer, mip, layer);
        }

        if (generateMipmaps && fileMipLevels == 1) {
            image->GenerateMipmaps();
        }

        co_return;
    }

    Task<void> LoadRegularImageAsync(const std::filesystem::path path, const bool generateMipmaps, std::shared_ptr<Image>& returnImage) {
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

        const auto image = returnImage;

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

        image->CopyFrom(*stagingBuffer, 0, 0);
        if (generateMipmaps) {
            image->GenerateMipmaps();
        }

        co_return;
    }

    Task<void> Importer::LoadImageAsync(const std::filesystem::path& path, const bool generateMipmaps, std::shared_ptr<Image>& returnImage) {
        if (path.extension() == ".ktx" || path.extension() == ".ktx2") {
            return LoadKTXAsync(path, generateMipmaps, returnImage);
        }
        return LoadRegularImageAsync(path, generateMipmaps, returnImage);
    }

    void Importer::SaveImage(const std::filesystem::path &path, const std::string& name, FileFormat fileFormat, const Image &image) {
            const auto imageOutput = OIIO::ImageOutput::create(path.string());
            if (!imageOutput) {
                throw std::runtime_error("Could not create image file: " + path.string() + "\nError: " + OIIO::geterror());
            }

            OIIO::ImageSpec spec(image.getExtent().x, image.getExtent().y, 4, OIIO::TypeDesc::UINT8);
            if (!imageOutput->open(path.string(), spec)) {
                throw std::runtime_error("Could not open image file for writing: " + path.string() + "\nError: " + OIIO::geterror());
            }

            auto data = image.ReadData(0, 0);

            if (!imageOutput->write_image(OIIO::TypeDesc::UINT8, data.data())) {
                throw std::runtime_error("Could not write image data: " + path.string() + "\nError: " + OIIO::geterror());
            }

            imageOutput->close();
    }

    std::unique_ptr<Importer> Importer::Load(const std::filesystem::path &path) {
        return std::make_unique<AssimpImporter>(path);
    }
}
