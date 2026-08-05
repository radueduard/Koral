//
// Created by radue on 29.07.2026.
//

// Reading a file into a kor::Image. OpenImageIO for the ordinary formats, libktx for KTX and KTX2 —
// which arrive with their mip chain, array layers and block compression already in them, and which
// are transcoded on the way in when they hold a universal texture (see decode.cpp).
//
// Each loader exists twice, and the pair shares everything but its threading: the blocking form is
// what a scene's Initialize wants, and the coroutine form hands the render thread back between
// slices so a frame can be drawn while a texture arrives.

#include <koralImageImport.h>

#include <utility>

#include <context.h>
#include <file.h>
#include <log.h>

#include "decode.h"

namespace kimg
{
    kor::Result<CpuImage> DecodeImageFile(const std::filesystem::path& relativePath)
    {
        // kor::Result is a std::expected with two extra members rather than an alias of one, so the
        // conversion has to be spelled out.
        auto decoded = detail::decodeFile(kor::assetPath(relativePath));
        if (!decoded) return std::unexpected(std::move(decoded.error()));
        return std::move(*decoded);
    }

    // ---- KTX ----------------------------------------------------------------------------------

    namespace
    {
        /** @brief Uploads every slice of a KTX image, one per call, in file order. */
        void uploadKtxSlice(const kor::ResourceRef<const kor::Image>& image,
                            const detail::KtxImage& ktx, const detail::KtxImage::Slice& slice)
        {
            detail::uploadSlice(image, std::span{ ktx.data + slice.offset, slice.size },
                                { std::max(1u, ktx.extent.x >> slice.mip),
                                  std::max(1u, ktx.extent.y >> slice.mip),
                                  std::max(1u, ktx.extent.z >> slice.mip) },
                                slice.layer, slice.mip);
        }

        kor::Resource<kor::Image> loadKtx(const std::filesystem::path& path, const bool generateMipmaps)
        {
            auto ktx = detail::readKtx(path);
            if (!ktx) return detail::poisoned(path, std::string(ktx.error().message));

            auto image = detail::makeKtxImage(*ktx, generateMipmaps);
            // Poisoned already — a format this device does not have. Uploading into it would turn one
            // clear error into one per slice.
            if (!image) return image;

            const kor::ResourceRef imageRef = image;
            for (const auto& slice : ktx->slices) uploadKtxSlice(imageRef, *ktx, slice);

            detail::finishUpload(imageRef, generateMipmaps && ktx->fileMipLevels == 1);
            return image;
        }

        kor::Task<kor::Resource<kor::Image>> loadKtxAsync(const std::filesystem::path path, const bool generateMipmaps)
        {
            // Reading the file, transcoding it and working out the slices all happen off the render
            // thread; only the GPU work below needs the main one.
            co_await kor::Context::SwitchToBackgroundThread();

            auto ktx = detail::readKtx(path);

            co_await kor::Context::SwitchToMainThread();
            if (!ktx) co_return detail::poisoned(path, std::string(ktx.error().message));

            auto image = detail::makeKtxImage(*ktx, generateMipmaps);
            if (!image) co_return std::move(image);   // see loadKtx: do not upload into a poisoned image

            const kor::ResourceRef imageRef = image;

            // One slice per short main-thread visit, releasing the render thread between them. The
            // round trip through the background executor is what re-enqueues the continuation.
            for (const auto& slice : ktx->slices) {
                uploadKtxSlice(imageRef, *ktx, slice);
                co_await kor::Context::SwitchToBackgroundThread();
                co_await kor::Context::SwitchToMainThread();
            }

            detail::finishUpload(imageRef, generateMipmaps && ktx->fileMipLevels == 1);
            co_return std::move(image);
        }
    }

    // ---- Everything OpenImageIO reads ----------------------------------------------------------

    namespace
    {
        /**
         * @brief Creates the GPU image a decoded file describes.
         *
         * mipLevels 0 asks the engine for a full chain, which is what generateMipmaps means here —
         * the file itself carried one level, or the loader would have taken the KTX path.
         */
        kor::Resource<kor::Image> makeDecodedImage(const CpuImage& decoded, const bool generateMipmaps)
        {
            return kor::Image::Builder()
                .setExtent(decoded.extent)
                .setFormat(decoded.format)
                .setMipLevels(generateMipmaps ? 0 : 1)
                .addUsage(kor::Image::Usage::eTransferSrc)
                .addUsage(kor::Image::Usage::eTransferDst)
                .build();
        }

        kor::Task<kor::Resource<kor::Image>> loadDecodedAsync(const std::filesystem::path path, const bool generateMipmaps)
        {
            co_await kor::Context::SwitchToBackgroundThread();

            auto decoded = detail::decodeFile(path);

            co_await kor::Context::SwitchToMainThread();
            if (!decoded) co_return detail::poisoned(path, std::string(decoded.error().message));

            auto image = makeDecodedImage(*decoded, generateMipmaps);
            const kor::ResourceRef imageRef = image;
            detail::uploadSlice(imageRef, decoded->pixels, decoded->extent, 0, 0);

            if (generateMipmaps) {
                // Mip generation as its own short main-thread visit.
                co_await kor::Context::SwitchToBackgroundThread();
                co_await kor::Context::SwitchToMainThread();
            }

            detail::finishUpload(imageRef, generateMipmaps);
            co_return std::move(image);
        }
    }

    kor::Resource<kor::Image> LoadImage(const std::filesystem::path& relativePath, const bool generateMipmaps)
    {
        // A relative path is a question — "wood.png, wherever you keep textures" — and the asset
        // search roots are the answer. An absolute one is left alone. See kor::assetPath.
        const auto path = kor::assetPath(relativePath);

        if (detail::isKtx(path)) return loadKtx(path, generateMipmaps);

        auto decoded = detail::decodeFile(path);
        if (!decoded) return detail::poisoned(path, std::string(decoded.error().message));

        auto image = makeDecodedImage(*decoded, generateMipmaps);
        detail::uploadSlice(image, decoded->pixels, decoded->extent, 0, 0);
        detail::finishUpload(image, generateMipmaps);
        return image;
    }

    kor::Task<kor::Resource<kor::Image>> LoadImageAsync(const std::filesystem::path& relativePath, const bool generateMipmaps)
    {
        // Resolved here rather than inside the coroutines: they run on a background thread, and the
        // search roots are read-mostly global state that the main thread owns.
        const auto path = kor::assetPath(relativePath);

        if (detail::isKtx(path)) return loadKtxAsync(path, generateMipmaps);
        return loadDecodedAsync(path, generateMipmaps);
    }
}
