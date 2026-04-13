//
// Created by radue on 2/23/2026.
//

#include <span>

#include "image.h"

#include "buffer.h"

#include "assimpImporter.h"

#include <IL/il.h>


gfx::Image::Format getImageFormat(ILint format, ILint type) {
    switch (format) {
        case IL_RGBA:
            switch (type) {
                case IL_UNSIGNED_BYTE:
                    return gfx::Image::Format::eRGBA8_UNORM;
                case IL_UNSIGNED_SHORT:
                    return gfx::Image::Format::eRGBA16_UNORM;
                case IL_FLOAT:
                    return gfx::Image::Format::eRGBA32_SFLOAT;
                default:
                    throw std::runtime_error("Unsupported image type: " + std::to_string(type));
            }
        case IL_LUMINANCE:
        case IL_ALPHA:
            switch (type) {
                case IL_UNSIGNED_BYTE:
                    return gfx::Image::Format::eR8_UNORM;
                case IL_UNSIGNED_SHORT:
                    return gfx::Image::Format::eR16_UNORM;
                case IL_FLOAT:
                    return gfx::Image::Format::eR32_SFLOAT;
                default:
                    throw std::runtime_error("Unsupported image type: " + std::to_string(type));
            }
        default:
            throw std::runtime_error("Unsupported image format: " + std::to_string(format));
    }
}

std::pair<ILint, ILint> getImageFormatAndType(gfx::Image::Format format) {
    switch (format) {
        case gfx::Image::Format::eRGBA8_UNORM:
            return { IL_RGBA, IL_UNSIGNED_BYTE };
        case gfx::Image::Format::eRGBA16_UNORM:
            return { IL_RGBA, IL_UNSIGNED_SHORT };
        case gfx::Image::Format::eRGBA32_SFLOAT:
            return { IL_RGBA, IL_FLOAT };
        case gfx::Image::Format::eR8_UNORM:
            return { IL_LUMINANCE, IL_UNSIGNED_BYTE };
        case gfx::Image::Format::eR16_UNORM:
            return { IL_LUMINANCE, IL_UNSIGNED_SHORT };
        case gfx::Image::Format::eR32_SFLOAT:
            return { IL_LUMINANCE, IL_FLOAT };
        default:
            throw std::runtime_error("Unsupported image format: " + std::to_string(static_cast<int>(format)));
    }
}

glm::u32 getPixelSize(ILint format, ILint type) {
    switch (format) {
        case IL_RGBA:
            switch (type) {
                case IL_UNSIGNED_BYTE:
                    return 4;
                case IL_UNSIGNED_SHORT:
                    return 8;
                case IL_FLOAT:
                    return 16;
                default:
                    throw std::runtime_error("Unsupported image type: " + std::to_string(type));
            }
        case IL_LUMINANCE:
        case IL_ALPHA:
            switch (type) {
                case IL_UNSIGNED_BYTE:
                    return 1;
                case IL_UNSIGNED_SHORT:
                    return 2;
                case IL_FLOAT:
                    return 4;
                default:
                    throw std::runtime_error("Unsupported image type: " + std::to_string(type));
            }
        default:
            throw std::runtime_error("Unsupported image format: " + std::to_string(format));
    }
}

namespace gfx
{
    std::unique_ptr<Image> Importer::LoadImage(const std::filesystem::path& path, bool generateMipmaps)
    {
        ILuint imageId;
        ilGenImages(1, &imageId);
        ilBindImage(imageId);
        if (!ilLoadImage(path.string().c_str())) {
            ilDeleteImages(1, &imageId);
            throw std::runtime_error("Failed to load image: " + path.string());
        }
        ILint format = ilGetInteger(IL_IMAGE_FORMAT);
        const ILint type = ilGetInteger(IL_IMAGE_TYPE);
        if (format == IL_RGB || format == IL_BGR || format == IL_BGRA) {
            if (!ilConvertImage(IL_RGBA, type)) {
                ilDeleteImages(1, &imageId);
                throw std::runtime_error("Failed to convert image to RGBA format: " + path.string());
            }
            format = IL_RGBA;
        }

        const ILint width = ilGetInteger(IL_IMAGE_WIDTH);
        const ILint height = ilGetInteger(IL_IMAGE_HEIGHT);
        const ILint depth = ilGetInteger(IL_IMAGE_DEPTH);
        const ILint layerCount = ilGetInteger(IL_NUM_LAYERS);
        const ILint mipmapCount = ilGetInteger(IL_NUM_MIPMAPS);

        if (generateMipmaps && mipmapCount > 0) {
            generateMipmaps = false;
        }
        const auto maxMipMapLevels = static_cast<ILint>(std::floor(std::log2(std::max({ width, height, depth }))) + 1);

        const auto imageType = depth > 1 ? Image::Type::e3D : height > 1 ? Image::Type::e2D : Image::Type::e1D;
        auto image = Image::Builder()
            .setType(imageType)
            .setExtent({ static_cast<glm::u32>(width), static_cast<glm::u32>(height), static_cast<glm::u32>(depth) })
            .setFormat(getImageFormat(format, type))
            .setUsage(Image::Usage::eSampled)
            .addUsage(Image::Usage::eTransferSrc)
            .addUsage(Image::Usage::eTransferDst)
            .setArrayLayers(layerCount > 0 ? static_cast<glm::u32>(layerCount) : 1)
            .setMipLevels(generateMipmaps && mipmapCount == 0 ? maxMipMapLevels : mipmapCount == 0 ? 1 : static_cast<glm::u32>(mipmapCount))
            .build();

        for (int layer = 0; layer < std::max(layerCount, 1); layer++) {
            if (layer != 0) ilActiveLayer(layer);
            for (int mipmap = 0; mipmap < std::max(mipmapCount,1); mipmap++) {
                if (mipmap != 0) ilActiveMipmap(mipmap);
                const ILuint dataSize = ilGetInteger(IL_IMAGE_SIZE_OF_DATA);

                const auto data = ilGetData();
                if (data == nullptr) {
                    ilDeleteImages(1, &imageId);
                    throw std::runtime_error("Failed to get image data: " + path.string());
                }
                const auto stagingBuffer = Buffer::Builder()
                    .setSize(dataSize)
                    .setUsage(Buffer::Usage::eTransferSrc)
                    .addMemoryProperty(Buffer::MemoryProperty::eHostVisible)
                    .addMemoryProperty(Buffer::MemoryProperty::eHostCoherent)
                    .build();

                stagingBuffer->Map();
                stagingBuffer->Write(std::span { data, dataSize });
                stagingBuffer->Unmap();
                image->CopyFrom(*stagingBuffer, mipmap, layer);
            }
        }
        if (generateMipmaps) {
            image->GenerateMipmaps();
        }

        ilDeleteImages(1, &imageId);
        return image;
    }

    void Importer::SaveImage(const std::filesystem::path &path, const std::string& name, FileFormat fileFormat, const Image &image) {
        const auto filePath = path / (name + "." + [fileFormat] {
            switch (fileFormat) {
                case FileFormat::ePNG: return "png";
                case FileFormat::eJPG: return "jpg";
                case FileFormat::eBMP: return "bmp";
                case FileFormat::eTGA: return "tga";
                case FileFormat::eHDR: return "hdr";
                case FileFormat::eDDS: return "dds";
                case FileFormat::ePPM: return "ppm";
                case FileFormat::eTIF: return "tif";
                default:
                    throw std::runtime_error("Unsupported file format: " + std::to_string(static_cast<int>(fileFormat)));
            }
        }());

        ILuint imageId;
        ilGenImages(1, &imageId);
        ilBindImage(imageId);

        const auto& extent = image.getExtent();
        const auto [format, type] = getImageFormatAndType(image.getFormat());
        const auto numChannels = format == IL_RGBA ? 4 : 1;

        bool hasLayersOrMipmaps = image.getArrayLayers() > 1 || image.getMipLevels() > 1;
        void* data = nullptr;

        if (hasLayersOrMipmaps) {
            // If the image has multiple layers or mip levels, we need to create an empty image and then fill in the data for each layer/mip level
            if (!ilTexImage(extent.x, extent.y, extent.z, numChannels, format, type, nullptr)) {
                ilDeleteImages(1, &imageId);
                throw std::runtime_error("Failed to create image for saving: " + filePath.string());
            }

            for (int layer = 0; layer < image.getArrayLayers(); layer++) {
                for (int mipLevel = 0; mipLevel < image.getMipLevels(); mipLevel++) {
                    auto bytes = image.ReadData(mipLevel, layer);
                    if (bytes.empty()) {
                        ilDeleteImages(1, &imageId);
                        throw std::runtime_error("Failed to read image data for saving: " + filePath.string());
                    }
                    data = bytes.data();
                    ilActiveLayer(layer);
                    ilActiveMipmap(mipLevel);
                    if (!ilSetData(data)) {
                        ilDeleteImages(1, &imageId);
                        throw std::runtime_error("Failed to set image data for saving: " + filePath.string());
                    }
                }
            }

        } else {
            // If the image has only one layer and one mip level, we can directly provide the data pointer to ilTexImage
            auto imageData = image.ReadData(0, 0);
            if (imageData.empty()) {
                ilDeleteImages(1, &imageId);
                throw std::runtime_error("Failed to read image data for saving: " + filePath.string());
            }
            data = imageData.data();
            if (!ilTexImage(extent.x, extent.y, extent.z, numChannels, format, type, data)) {
                ilDeleteImages(1, &imageId);
                throw std::runtime_error("Failed to create image for saving: " + filePath.string());
            }
        }

        if (!ilSaveImage(filePath.string().c_str())) {
            ilDeleteImages(1, &imageId);
            throw std::runtime_error("Failed to save image: " + filePath.string());
        }
    }

    std::unique_ptr<Importer> Importer::LoadMeshes(const std::filesystem::path &path) {
        return std::make_unique<AssimpImporter>(path);
    }
}
