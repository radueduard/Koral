//
// Created by radue on 2/18/2026.
//

#include "../backends/vulkan/buffer.h"

#include <algorithm>
#include <optional>

#include <buffer.h>
#include <commandBuffer.h>
#include <scheduler.h>
#include <token.h>
#include <window.h>
#include <framebuffer.h>
#include <surface.h>

namespace kor
{
    kor::Result<std::unique_ptr<Buffer>> Buffer::RawBuilder::Create() const
    {
        BeginAttempt();

        // The one role that cannot simply be on by default, and the one place a usage is deduced
        // rather than declared. eUniform is free to hold *except* for its size ceiling, and the size
        // is known right here — so a buffer small enough to be a uniform block is given the role,
        // and one too large could never have had it anyway. That covers the camera and per-frame
        // constant buffers, which are the ones anybody would have had to remember it for.
        //
        // Skipped when the caller named an exact set with setUsage: it has already said what it
        // wants, and quietly adding to that would make setUsage mean something other than it says.
        if (!_usageExact && _size <= 0xFFFF) {
            _usage |= Usage::eUniform;
        }

        // Still an error when the caller asked for it outright on a buffer that cannot hold it —
        // the deduction above never produces this, so reaching it means eUniform was requested.
        if (_usage & Usage::eUniform && _size > 0xFFFF) {
            AddError(ErrorCode::eUniformBufferTooLarge,
                     std::format("Uniform buffer size of {} bytes exceeds the maximum allowed size of 65536 bytes!", _size));
        }

        // Logs all warnings/errors with call sites; returns the first error as a kor::Error.
        if (auto v = Validate(); !v) return std::unexpected(v.error());

        const auto api = Context::ActiveAPI();
        if (api != API::eVulkan) {
            return Fail(ErrorCode::eUnknownApi, "Unknown graphics API!");
        }

        // Backend allocation/creation may throw; convert any escape into a kor::Error.
        return Guard(ErrorCode::eBackend, [&]() -> std::unique_ptr<Buffer> {
            return kor::MakeBackendPtr<Buffer, vk::Buffer>(*this);
        });
    }

    kor::Resource<Buffer> Buffer::RawBuilder::Build(const std::source_location where) const
    {
        auto buffer = Materialize<Buffer>(*this, "Buffer", where);
        Context::Repository().AddRef(ResourceRef<const Buffer>(buffer));
        Adopted(buffer);
        return buffer;
    }

    // ---- per-frame device-local writes -------------------------------------------------------

    struct Buffer::UploadQueue {
        struct Upload {
            Resource<Buffer> staging;                ///< The data, host-side, until every copy has it.
            glm::u64 offset = 0;
            glm::u64 byteSize = 0;
            std::unordered_set<glm::u32> copiesLeft; ///< Frame copies still to receive it.
            std::optional<Token> lastCopy;           ///< The frame that last copied from the staging buffer.
        };
        std::vector<Upload> items;                   ///< Oldest first: copies land in the order written.
    };

    void Buffer::Adopted(const Resource<Buffer>& buffer) {
        if (auto* raw = const_cast<Buffer*>(buffer.Get())) raw->_self = ResourceRef<const Buffer>(buffer);
    }

    void Buffer::UploadPerFrame(const std::span<const std::byte> bytes, const glm::u64 byteOffset) {
        if (!_uploads) _uploads = std::make_shared<UploadQueue>();

        auto staging = Builder<std::byte>()
            .SetInstanceCount(static_cast<glm::i64>(bytes.size()))
            .SetUsage(Usage::eTransferSrc)
            .SetType(Type::eStaging)
            .Build();
        if (!staging.Valid()) {
            kor::log::Error("Could not stage a write to a per-frame buffer; the write is lost: {}",
                            staging.Failure() ? staging.Failure()->ToString() : std::string("unknown error"));
            return;
        }
        staging->Write(bytes, 0);

        // A copy still owed data this write covers entirely would only be overwritten again: skip it.
        const glm::u64 end = byteOffset + bytes.size();
        for (auto& upload : _uploads->items) {
            if (upload.offset >= byteOffset && upload.offset + upload.byteSize <= end) upload.copiesLeft.clear();
        }

        UploadQueue::Upload upload{ .staging = std::move(staging), .offset = byteOffset, .byteSize = bytes.size() };
        for (glm::u32 i = 0; i < CopyCount(); ++i) upload.copiesLeft.insert(i);
        _uploads->items.push_back(std::move(upload));

        // Written during a frame, it is that frame's data from now on — the copy for it is recorded
        // ahead of the frame's own rendering. Every other copy gets it at the top of its frame.
        DeliverPendingUploads();
    }

    void Buffer::DeliverPendingUploads() {
        if (!_uploads) return;
        auto& items = _uploads->items;

        // Delivered everywhere, and the last frame to read the staging buffer has finished with it.
        std::erase_if(items, [](const UploadQueue::Upload& upload) {
            return upload.copiesLeft.empty() && (!upload.lastCopy || upload.lastCopy->Ready());
        });

        auto& scheduler = Context::Scheduler();
        if (!scheduler.IsBuildingFrame()) return;
        const glm::u32 current = scheduler.CurrentImageIndex();

        // Tracked once the buffer is a Resource, so a buffer destroyed before the frame ends is
        // reported rather than written; untracked only for the initial data, recorded mid-build.
        const ResourceRef<const Buffer> target = _self.Alive() ? _self : ResourceRef<const Buffer>(*this);

        std::unique_ptr<CommandBuffer> commands;
        std::vector<UploadQueue::Upload*> delivered;
        for (auto& upload : items) {
            if (!upload.copiesLeft.erase(current)) continue;
            if (!commands) {
                commands = CommandBuffer::Create(CommandBuffer::Usage::eGraphics);
                commands->Begin();
            }
            commands->CopyBuffer(upload.staging, target, upload.byteSize, 0, upload.offset);
            delivered.push_back(&upload);
        }
        if (!commands) return;

        const Token done = scheduler.Execute(std::move(commands), Scheduler::Placement::eBeforeFrame);
        for (auto* upload : delivered) upload->lastCopy = done;
    }

    Buffer::Buffer(const RawBuilder& createInfo) :
        _isPerFrame(createInfo._isPerFrame),
        _size(createInfo._size),
        _usage(createInfo._usage),
        _type(createInfo._type) {}
}
