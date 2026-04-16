//
// Created by radue on 2/23/2026.
//

#include <span>

#include "image.h"

#include "buffer.h"

#include "assimpImporter.h"

#include <OpenImageIO/imageio.h>

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

        const auto stagingBuffer = gfx::Buffer::Builder()
            .setSize(data.size())
            .setUsage(gfx::Buffer::Usage::eTransferSrc)
            .build();

        stagingBuffer->Map();
        stagingBuffer->Write(std::span { data });
        stagingBuffer->Unmap();

        image->CopyFrom(*stagingBuffer, 0, 0);
        imageInput->close();

        if (generateMipmaps) {
            image->GenerateMipmaps();
        }

        return image;
    }

    Task<void> Importer::LoadImageAsync(const std::filesystem::path path, const bool generateMipmaps, std::shared_ptr<Image>& returnImage)
    {
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

        const auto stagingBuffer = Buffer::Builder()
            .setSize(data.size())
            .setUsage(Buffer::Usage::eTransferSrc)
            .build();

        stagingBuffer->Map();
        stagingBuffer->Write(std::span{data});
        stagingBuffer->Unmap();

        image->CopyFrom(*stagingBuffer, 0, 0);
        if (generateMipmaps) {
            image->GenerateMipmaps();
        }

        co_return;
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
