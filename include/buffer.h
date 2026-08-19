//
// Created by radue on 2/18/2026.
//

#pragma once
#include <span>
#include <memory>
#include <vector>
#include <ranges>
#include <type_traits>
#include <stdexcept>
#include <cstring>
#include <limits>
#include <unordered_set>

#include <glm/fwd.hpp>

#include "flags.h"
#include "api.h"
#include "dataRange.h"
#include <source_location>

#include "builder.h"
#include "log.h"
#include "context.h"
#include "resource.h"
#include "scheduler.h"

namespace kor
{
    /**
     * @brief A write to one frame's copy of a per-frame buffer, waiting to reach the others.
     *
     * A per-frame buffer holds one copy per frame in flight, and a write only ever lands in the
     * copy the current frame is using. Propagating it immediately would overwrite memory the GPU is
     * still reading, so the write is recorded here instead and copied into each remaining frame as
     * that frame comes round. Used internally by Buffer::automaticUpdate.
     */
    struct PendingWrite {
        glm::u32 srcFrameIndex;                             ///< The frame whose copy already holds the new data.
        glm::u64 offset;                                    ///< Byte offset of the written region.
        glm::u64 byteSize;                                  ///< Length of the written region.
        std::unordered_set<glm::u32> buffersLeftToUpdate;   ///< Frames that have not received it yet.

        auto operator <=> (const PendingWrite& other) const {
            if (srcFrameIndex != other.srcFrameIndex) return srcFrameIndex <=> other.srcFrameIndex;
            if (offset != other.offset) return offset <=> other.offset;
            return byteSize <=> other.byteSize;
        }

        bool operator == (const PendingWrite& other) const {
            return srcFrameIndex == other.srcFrameIndex && offset == other.offset && byteSize == other.byteSize;
        }

        /** @brief Hashes a write by the region it covers, so two writes to the same region collapse. */
        struct Hash {
            std::size_t operator()(const PendingWrite& write) const {
                return std::hash<glm::u32>()(write.srcFrameIndex)
                    ^ std::hash<glm::u64>()(write.offset)
                    ^ std::hash<glm::u64>()(write.byteSize);
            }
        };
    };

    /**
     * @brief A block of GPU memory: vertices, indices, uniforms, storage, indirect commands.
     *
     * Buffers are created through a builder and handed back as a Resource, which is what keeps them
     * alive:
     *
     * @code
     * Buffer::Builder<glm::mat4> builder;
     * auto cameraBuffer = builder
     *     .setInstanceCount(1)
     *     .addUsage(Buffer::Usage::eUniform)
     *     .setType(Buffer::Type::eDynamic)
     *     .setIsPerFrame(true)
     *     .build();
     * @endcode
     *
     * Two choices decide how a buffer behaves, and both are worth getting right:
     *
     * **Type** is where the memory lives. Type::eDeviceLocal is GPU memory the CPU cannot touch —
     * fastest for the GPU, but every Read and Write has to stage through a temporary buffer and
     * *wait for the GPU*, so it suits data written once. The host-visible types (eDynamic,
     * eStaging, eReadback) are mapped and written directly, which is what per-frame data wants.
     * Between the two sits Type::eDeviceDynamic: GPU-speed memory the CPU can write straight into,
     * for data rewritten every frame that the GPU also reads hard — at the price of never being
     * allowed to read it back.
     *
     * **Per-frame** (setIsPerFrame) gives the buffer one copy per frame in flight. Without it,
     * writing to a buffer the GPU is still reading from an earlier frame corrupts that frame; with
     * it, a write lands in the current frame's copy and is propagated to the others as they come
     * round. Anything the CPU rewrites every frame — camera matrices, per-frame constants — should
     * be per-frame.
     *
     * Reads and writes come in two forms. Read/Write/ReadAt/WriteAt are one-shot and handle staging
     * and synchronisation themselves. Map() returns a mapping object that keeps the memory mapped
     * for as long as it lives, which is cheaper when touching many elements at once.
     *
     * @see Mesh, DescriptorSet, CommandBuffer::CopyBuffer
     */
    class KORAL_API Buffer : public AutoUpdatable
    {
    public:
        /**
         * @brief Buffer usage flags. These describe how the buffer can be used by GPU operations
         * (transfer, vertex/index, uniform/storage, indirect, etc.).
         *
         * Declare everything the buffer will be used for: a use that was not declared is invalid,
         * and declaring more than needed can cost performance.
         */
        enum class Usage
        {
            eNone        = 0,       ///< No usage specified. This is not a valid usage flag and should be combined with other flags.
            eTransferSrc = 1 << 0,  ///< Buffer can be used as a source for transfer operations (e.g., copying to another buffer or image).
            eTransferDst = 1 << 1,  ///< Buffer can be used as a destination for transfer operations (e.g., copying from another buffer or image).
            eTexel       = 1 << 2,  ///< Buffer can be used for formatted memory access in shaders (e.g., as a storage texel buffer or uniform texel buffer).
            eUniform     = 1 << 3,  ///< Buffer can be used as a uniform buffer, providing read-only data to shaders (e.g., for per-frame constants or material parameters).
            eStorage     = 1 << 4,  ///< Buffer can be used as a storage buffer, providing read-write access to shaders (e.g., for compute shader output or large data storage).
            eVertex      = 1 << 5,  ///< Buffer can be used as a vertex buffer, providing vertex attribute data to the vertex shader stage.
            eIndex       = 1 << 6,  ///< Buffer can be used as an index buffer, providing indices for indexed drawing commands.
            eIndirect    = 1 << 7,  ///< Buffer can be used as an indirect buffer, providing draw or dispatch parameters for indirect drawing or compute dispatch commands.
            eShaderDeviceAddress      = 1 << 8,  ///< Buffer can have its GPU device address queried (e.g. for buffer references or as a ray-tracing build input).
            eAccelerationStructureInput = 1 << 9,  ///< Buffer can be used as read-only input geometry when building a ray-tracing acceleration structure.
        };

        /**
         * @brief Buffer memory type / placement policy. This describes where memory lives and
         * how CPU/GPU can access it.
         */
        enum class Type {
            eDeviceLocal    = 0,    ///< Memory that resides on the GPU and is not directly accessible by the CPU. This type of memory typically offers the best performance for GPU access, but may require explicit staging buffers for data transfer.
            eStaging        = 1,    ///< Memory that is accessible by both the CPU and GPU, but is optimized for data transfer operations. This type of memory is often used for staging buffers when transferring data to or from device-local memory. @warning Write-combined: reading one back is roughly forty times slower than eReadback.
            eReadback       = 2,    ///< Memory that is accessible by both the CPU and GPU, but is optimized for reading data back from the GPU. This type of memory is often used for readback buffers when retrieving data from device-local memory.
            eDynamic        = 3,    ///< Memory that is accessible by both the CPU and GPU, and is optimized for frequent updates from the CPU. Cached, so the CPU can read it back as cheaply as it writes it — at the cost of living in system memory, which the GPU reaches across the bus. The safe default.
            /**
             * @brief GPU memory that the CPU can nonetheless write to directly. The GPU reaches it
             *        at full speed; the CPU must only ever *write* it.
             *
             * What eDynamic is usually assumed to be. It needs a device-local, host-visible memory
             * type, which on a discrete GPU means a resizable BAR — where it is dramatically faster
             * than eDynamic for anything the GPU touches repeatedly (measured ~18x on a buffer a
             * compute pass walks). Without one it falls back to eDynamic's placement, so it is
             * always safe to ask for.
             *
             * @warning Never read it back. The memory is write-combined and uncached, so a CPU read
             *          crosses the bus uncached and runs some three hundred times slower than the
             *          same read from eDynamic — slow enough to look like a hang, with nothing
             *          failing. Read() says so if you do. Reading through Map() cannot be detected
             *          at all, so that one is on you.
             *
             * Reach for it when the CPU rewrites a buffer every frame *and* the GPU reads it hard —
             * instance data, particles, a large per-frame table. When the data is written once and
             * then only used by the GPU, eDeviceLocal is the better answer.
             */
            eDeviceDynamic  = 4,
        };

        /**
         * @brief Untyped builder (raw byte size + usage + memory type).
         * Use this when creating a generic buffer without element type semantics.
         *
         * Buffer::Builder is the typed form and is what you normally want; this one sizes the
         * buffer in bytes and cannot carry initial data.
         */
        struct KORAL_API RawBuilder : Builder {
            bool _isPerFrame = false;               ///< Whether the buffer holds one copy per frame in flight.
            glm::i64 _size = 0;                     ///< Size in bytes.
            Flags<Usage> _usage = Usage::eNone;     ///< Everything the buffer may be used for.
            Type _type = Type::eDynamic;            ///< Where the memory lives.

            /**
             * @brief Sets the buffer's size in bytes.
             * @param size Byte count; must be greater than zero, or the build fails with
             *        ErrorCode::eBufferSizeInvalid.
             */
            RawBuilder& setRawSize(const glm::i64 size) {
                if (size <= 0) {
                    addError(ErrorCode::eBufferSizeInvalid, "size must be > 0");
                } else {
                    _size = size;
                }
                return *this;
            }

            /**
             * @brief Gives the buffer one copy per frame in flight, so it can be rewritten every frame.
             *
             * Writes go to the copy the current frame uses and are propagated to the rest as those
             * frames come round, which is what stops a write from landing in memory the GPU is
             * still reading. Adds the transfer usages that propagation needs.
             */
            RawBuilder& setIsPerFrame(const bool value) {
                _isPerFrame = value;
                _usage |= Usage::eTransferSrc;
                _usage |= Usage::eTransferDst;
                _usageTouched = true;
                return *this;
            }

            /**
             * @brief Replaces the usage flags outright.
             *
             * Warns if any usage was already set, since this discards it — including the transfer
             * usages setIsPerFrame added. Prefer addUsage.
             */
            RawBuilder& setUsage(const Flags<Usage> usage) {
                if (_usageTouched) {
                    warn("setUsage overwrites usage flags previously set via addUsage/setIsPerFrame");
                }
                _usage = usage;
                _usageTouched = true;
                return *this;
            }

            /** @brief Adds one usage to those already set. */
            RawBuilder& addUsage(const Usage usage) {
                _usage |= usage;
                _usageTouched = true;
                return *this;
            }

            /** @brief Chooses where the memory lives, and so how it can be read and written. */
            RawBuilder& setType(const Type type) {
                _type = type;
                return *this;
            }

            /** @brief One build attempt. Internal: prefer build(). */
            [[nodiscard]] virtual Result<std::unique_ptr<Buffer>> create() const;

            /**
             * @brief Creates the buffer.
             * @return The buffer as a Resource. A failed build returns a poisoned resource rather
             *         than throwing: it can be held and passed around, and only fails the command
             *         buffer that tries to use it, naming this call site.
             */
            [[nodiscard]] virtual Resource<Buffer> build(std::source_location where = std::source_location::current()) const;

        protected:
            bool _usageTouched = false; ///< tracks explicit usage edits so setUsage can warn on overwrite

            RawBuilder& setSize(const glm::i64 value) {
                if (value <= 0) {
                    addError(ErrorCode::eBufferSizeInvalid, "RawBuilder::size must be > 0");
                } else {
                    _size = value;
                }
                return *this;
            }
        };

        /**
         * @brief Typed builder: sizes the buffer in elements of T and can fill it on creation.
         * @tparam T The element type. Must be trivially copyable, and must match what the shader
         *         declares — including its padding, which for uniform blocks is not always what the
         *         C++ layout gives you.
         *
         * Set the contents with setData (which copies) or setDataView (which does not), or leave
         * them unset and write later. For a Type::eDeviceLocal buffer, initial data is staged and
         * copied on the GPU; for the host-visible types it is written straight in.
         */
        template <typename T> requires std::is_trivially_copyable_v<T>
        struct Builder : RawBuilder {
            glm::i64 _instanceCount = 1;    ///< Number of elements of T. The byte size follows from it.

            Builder() {
                _size = static_cast<glm::i64>(sizeof(T));
            }

        private:
            std::vector<T> _ownedData{};        ///< setData: we hold a copy of the caller's data.
            std::span<const T> _externalView{}; ///< setDataView: the caller keeps ownership.

        public:
            virtual ~Builder() = default;

            /**
             * @brief The initial data, from whichever of setData/setDataView is in play.
             * @return A view of the data the buffer will be created with; empty if none was set.
             *
             * Derived on each call rather than stored, so that copying the builder cannot leave the
             * copy pointing at the original's vector.
             */
            [[nodiscard]] std::span<const T> dataView() const {
                return _ownedData.empty() ? _externalView : std::span<const T>(_ownedData);
            }

            /**
             * @brief Sizes the buffer to hold @p value elements of T, without giving it contents.
             * @param value Element count; must not be negative.
             */
            Builder& setInstanceCount(const glm::i64 value) {
                if (value < 0) {
                    addError(ErrorCode::eBufferSizeInvalid, "instanceCount must be >= 0");
                }
                _instanceCount = value;
                _size = static_cast<glm::i64>(sizeof(T)) * _instanceCount;
                return *this;
            }

            /**
             * @brief Creates a one-element buffer holding a copy of @p value.
             *
             * The copy is taken here, so the argument may be a temporary.
             */
            Builder& setData(const T& value) {
                _ownedData.assign(1, value);
                _externalView = {};
                _instanceCount = 1;
                _size = static_cast<glm::i64>(sizeof(T));
                return *this;
            }

            /**
             * @brief Sizes the buffer to @p data and fills it with a copy of it.
             * @param data Any range of T — a vector, an array, a span, or a view computed on the fly.
             *
             * The copy is taken here, so @p data need not outlive the call, and a range that is not
             * contiguous is walked rather than refused. An empty range fails the build with
             * ErrorCode::eBufferSizeInvalid.
             */
            template <RangeOf<T> R>
            Builder& setData(R&& data) {
                _ownedData.clear();
                if constexpr (std::ranges::sized_range<R>) _ownedData.reserve(std::ranges::size(data));
                for (const auto& element : data) _ownedData.push_back(element);

                if (_ownedData.empty()) {
                    addError(ErrorCode::eBufferSizeInvalid, "Data container must have size > 0");
                }
                _externalView = {};
                _instanceCount = static_cast<glm::i64>(_ownedData.size());
                _size = static_cast<glm::i64>(sizeof(T)) * _instanceCount;
                return *this;
            }

            /**
             * @brief Sizes the buffer to @p view and fills it from that memory, without copying it.
             * @param view The source data: any contiguous range of T. Something that is not
             *        contiguous cannot be viewed — pass it to setData, which copies.
             *
             * @warning Nothing is copied until build(). The memory @p view refers to must still be
             *          alive then. Use setData when it might not be.
             */
            template <ContiguousRangeOf<T> R>
            Builder& setDataView(R&& view) {
                _ownedData.clear();
                _externalView = std::span<const T>(std::ranges::data(view), std::ranges::size(view));
                _instanceCount = static_cast<glm::i64>(_externalView.size());
                _size = static_cast<glm::i64>(sizeof(T)) * _instanceCount;
                return *this;
            }

            /** @brief One build attempt, including the upload of any initial data. Internal: prefer build(). */
            [[nodiscard]] Result<std::unique_ptr<Buffer>> create() const override {
                auto created = RawBuilder::create();
                if (!created) return created;
                std::unique_ptr<Buffer> buffer = std::move(*created);

                const auto data = dataView();
                if (!data.empty()) {
                    // The upload needs a ResourceRef; an unsafe (untracked) one is sound here
                    // because the buffer cannot outlive this scope before we hand it over.
                    const auto bufferRef = ResourceRef<const Buffer>(buffer.get());

                    switch (buffer->getType()) {
                        case Type::eDeviceLocal: {
                            const auto byteSize = checkedByteSize<T>(static_cast<glm::u64>(_instanceCount), "Builder::build");
                            Builder<std::byte> stagingBuilder;
                            stagingBuilder
                                .setInstanceCount(toBuilderSize(byteSize, "Builder::build"))
                                .setUsage(Usage::eTransferSrc)
                                .setType(Type::eStaging);

                            // The staging buffer is an internal detail of this upload, so its
                            // failure is ours — inherit it as our cause rather than reporting a
                            // second, unrelated-looking error.
                            auto staging = stagingBuilder.create();
                            if (!staging) {
                                return failCausedBy(ErrorCode::eBackend,
                                    std::make_shared<const Error>(std::move(staging.error())),
                                    "Could not stage the buffer's initial data.");
                            }
                            std::unique_ptr<Buffer> stagingBuffer = std::move(*staging);
                            const auto stagingRef = ResourceRef<const Buffer>(stagingBuffer.get());

                            stagingBuffer->Write(data, 0);
                            CommandBuffer::SingleTimeCommand([&](CommandBuffer& commandBuffer) {
                                commandBuffer.CopyBuffer(stagingRef, bufferRef, byteSize);
                            }, CommandBuffer::Usage::eTransfer);
                            break;
                        }
                        case Type::eStaging:
                        case Type::eReadback:
                        case Type::eDynamic:
                        case Type::eDeviceDynamic:
                            buffer->Write(data, 0);
                            break;
                    }
                }

                return buffer;
            }

            /**
             * @brief Creates the buffer, uploads any initial data, and registers it for automatic updates.
             * @return The buffer as a Resource, poisoned rather than thrown if the build failed.
             */
            [[nodiscard]] Resource<Buffer> build(std::source_location where = std::source_location::current()) const override {
                auto buffer = materialize<Buffer>(*this, "Buffer", where);
                Context::Repository().addRef(ResourceRef<const Buffer>(buffer));
                return buffer;
            }
        };

        virtual ~Buffer() = default;

        /**
         * @brief Reads one element out of the buffer.
         * @tparam T The element type to read the memory as. Must be trivially copyable.
         * @param index Which element, counted in elements of T.
         * @return A copy of it.
         *
         * @warning On a Type::eDeviceLocal buffer this stages a copy and blocks until the GPU has
         *          performed it. Never do that per frame — keep such data in a host-visible buffer,
         *          or read a whole range at once with Read.
         * @throws std::out_of_range if the element lies past the end of the buffer.
         */
        template <typename T> requires std::is_trivially_copyable_v<T>
        [[nodiscard]] T ReadAt(const glm::u64 index = 0) const
        {
            validateElementRange<T>(index, 1, "ReadAt");

            switch (_type) {
                case Type::eDeviceLocal:
                {
                    constexpr glm::u64 elemBytes = sizeof(T);
                    Builder<std::byte> stagingBuilder;
                    stagingBuilder
                        .setInstanceCount(toBuilderSize(elemBytes, "ReadAt"))
                        .setUsage(Usage::eTransferDst)
                        .setType(Type::eStaging);
                    const auto stagingBuffer = stagingBuilder.build();
                    CommandBuffer::SingleTimeCommand([&](CommandBuffer& commandBuffer) {
                        commandBuffer.CopyBuffer(ResourceRef<const Buffer>(*this), stagingBuffer, elemBytes, index * elemBytes, 0);
                    }, CommandBuffer::Usage::eTransfer);
                    return stagingBuffer->ReadAt<T>(0);
                }
                case Type::eStaging:
                case Type::eReadback:
                case Type::eDynamic:
                case Type::eDeviceDynamic: {
                    ConstMapping<T> mapping = this->Map<T>(1, index);
                    return mapping[0];
                }
                default:
                    throw std::runtime_error("Unknown buffer type");
            }
        }

        /**
         * @brief Writes one element into the buffer.
         * @tparam T The element type. Must be trivially copyable.
         * @param index Which element, counted in elements of T.
         * @param data The value to store.
         *
         * On a per-frame buffer the write lands in the current frame's copy and is propagated to
         * the others automatically.
         *
         * @warning On a Type::eDeviceLocal buffer this stages a copy and blocks until the GPU has
         *          performed it.
         * @throws std::out_of_range if the element lies past the end of the buffer.
         */
        template <typename T> requires std::is_trivially_copyable_v<T>
        void WriteAt(const glm::u64 index, const T& data)
        {
            validateElementRange<T>(index, 1, "WriteAt");

            switch (_type) {
                case Type::eDeviceLocal:
                {
                    constexpr glm::u64 elemBytes = sizeof(T);
                    Builder<std::byte> stagingBuilder;
                    stagingBuilder
                        .setInstanceCount(toBuilderSize(elemBytes, "WriteAt"))
                        .setUsage(Usage::eTransferSrc)
                        .setType(Type::eStaging);
                    const auto stagingBuffer = stagingBuilder.build();

                    stagingBuffer->WriteAt<T>(0, data);
                    CommandBuffer::SingleTimeCommand([&](CommandBuffer& commandBuffer) {
                        commandBuffer.CopyBuffer(stagingBuffer, ResourceRef<const Buffer>(*this), elemBytes, 0, index * elemBytes);
                    }, CommandBuffer::Usage::eTransfer);
                    break;
                }
                case Type::eStaging:
                case Type::eReadback:
                case Type::eDynamic:
                case Type::eDeviceDynamic: {
                    MutableMapping<T> mapping(*this, index, 1);
                    mapping[0] = data;
                    break;
                }
            }
        }

        /**
         * @brief Reads a contiguous range of elements of type T from the buffer, starting at the specified element offset. The buffer will be interpreted as an array of T, and the returned vector will contain count elements of type T.
         *
         * @tparam T The element type to read. Must be trivially copyable. The buffer will be interpreted as an array of T, and the returned vector will contain count elements of type T.
         * @param count The number of elements of type T to read. If count is 0, it will read as many elements as possible until the end of the buffer (i.e., up to (_size - offset) / sizeof(T)).
         * @param offset The element offset (in terms of T) from the start of the buffer to begin reading from. The actual byte offset will be offset * sizeof(T). Must be non-negative and less than _size / sizeof(T).
         * @return A vector containing the read elements of type T. The size of the vector will be equal to count, or the maximum number of elements that can be read until the end of the buffer if count is 0.
         *
         * @warning On a Type::eDeviceLocal buffer this stages a copy and blocks until the GPU has
         *          performed it. Reading GPU results back is what Type::eReadback exists for.
         * @warning On a Type::eDeviceDynamic buffer this is uncached and crosses the bus, which is
         *          hundreds of times slower than the same read from eDynamic. It is logged.
         * @throws std::out_of_range if the range runs past the end of the buffer.
         */
        template <typename T> requires std::is_trivially_copyable_v<T>
        std::vector<T> Read(glm::u64 count = 0, const glm::u64 offset = 0) const
        {
            const auto elemCapacity = static_cast<glm::u64>(_size / sizeof(T));
            if (offset > elemCapacity) {
                kor::log::error("Read: offset exceeds buffer elements. capacity={}, requestedOffset={}", elemCapacity, offset);
                throw std::out_of_range("Offset exceeds buffer element capacity");
            }
            if (count == 0) {
                count = elemCapacity - offset;
            }
            validateElementRange<T>(offset, count, "Read");

            const auto byteSize = checkedByteSize<T>(count, "Read");
            const auto byteOffset = offset * static_cast<glm::u64>(sizeof(T));

            // Not an error — it works, it is simply orders of magnitude slower than it looks, and
            // silently so. Anyone reading a buffer they asked to be write-combined has almost
            // certainly picked the wrong type, and would otherwise be left wondering why a readback
            // that takes a millisecond elsewhere takes most of a second here.
            if (_type == Type::eDeviceDynamic) {
                kor::log::warn("Read() on a Type::eDeviceDynamic buffer, which is a type asked for "
                               "on the promise that the CPU only writes it. Where that lands in "
                               "write-combined device memory — a discrete GPU with a resizable BAR "
                               "— this read is uncached, crosses the bus, and runs hundreds of "
                               "times slower than the same read from Type::eDynamic. Use eDynamic "
                               "if the CPU has to read it back, or eReadback if the GPU produced it.");
            }

            switch (_type) {
                case Type::eDeviceLocal:
                {
                    Builder<std::byte> stagingBuilder;
                    stagingBuilder
                        .setInstanceCount(toBuilderSize(byteSize, "Read"))
                        .setUsage(Usage::eTransferDst)
                        .setType(Type::eStaging);
                    const auto stagingBuffer = stagingBuilder.build();
                    CommandBuffer::SingleTimeCommand([&](CommandBuffer& commandBuffer) {
                        commandBuffer.CopyBuffer(ResourceRef<const Buffer>(*this), stagingBuffer, byteSize, byteOffset, 0);
                    }, CommandBuffer::Usage::eTransfer);
                    return stagingBuffer->Read<T>(count, 0);
                }
                case Type::eStaging:
                case Type::eReadback:
                case Type::eDynamic:
                case Type::eDeviceDynamic: {
                    ConstMapping<T> mapping = this->Map<T>(count, offset);
                    return mapping.Read();
                }
                default:
                    throw std::runtime_error("Unknown buffer type");
            }
        }

        /**
         * @brief Writes a contiguous range of elements into the buffer.
         * @tparam T The element type. Must be trivially copyable.
         * @param data The elements to write. Its size decides how many.
         * @param elements The data to write: any range of T — a vector, an array, a span, a view.
         * @param offset Element offset to start at, counted in elements of T.
         *
         * On a per-frame buffer the write lands in the current frame's copy and is propagated to
         * the others automatically. On a Type::eStaging buffer the range is flushed, so it is
         * visible to a copy recorded afterwards.
         *
         * @warning On a Type::eDeviceLocal buffer this stages a copy and blocks until the GPU has
         *          performed it. For data written every frame, use a host-visible per-frame buffer.
         * @throws std::out_of_range if the range runs past the end of the buffer.
         * @throws std::runtime_error if the buffer is currently mapped; write through the mapping instead.
         */
        template <typename R, typename T = std::remove_cvref_t<std::ranges::range_value_t<R>>>
            requires RangeOf<R, T> && std::is_trivially_copyable_v<T>
        void Write(R&& elements, const glm::u64 offset = 0) {
            // Contiguous where it lies, copied into a temporary where it is not — either way what
            // reaches the GPU below is one block of bytes. @see kor::ContiguousCopy
            const ContiguousCopy<T> contiguous(std::forward<R>(elements));
            const std::span<const T> data = contiguous.view();
            const auto count = static_cast<glm::u64>(data.size());
            validateElementRange<T>(offset, count, "Write");

            const auto byteSize = checkedByteSize<T>(count, "Write");
            const auto byteOffset = offset * sizeof(T);

            if (_isMappedMutably) {
                kor::log::error("Attempted to call Write() on a buffer that is already mapped mutably! You cannot call Write() while a mutable mapping is active, as it may cause synchronization issues. Please use the MutableMapping returned by Map() to write data to the buffer while it is mapped.", _size, offset, offset + count);
                throw std::runtime_error("Buffer is already mapped mutably");
            }

            if (_constMapCount > 0) {
                kor::log::error("Attempted to call Write() on a buffer that is already mapped const! You cannot call Write() while a const mapping is active, as it may cause synchronization issues. Please use the ConstMapping returned by Map() to read data from the buffer while it is mapped.", _size, offset, offset + count);
                throw std::runtime_error("Buffer is already mapped const");
            }

            switch (_type) {
                case Type::eDeviceLocal:
                {
                    Builder<std::byte> stagingBuilder;
                    stagingBuilder
                        .setInstanceCount(toBuilderSize(byteSize, "Write"))
                        .setUsage(Usage::eTransferSrc)
                        .setType(Type::eStaging);
                    auto stagingBuffer = stagingBuilder.build();
                    stagingBuffer->Write(data, 0);
                    CommandBuffer::SingleTimeCommand([&](CommandBuffer& commandBuffer) {
                        commandBuffer.CopyBuffer(stagingBuffer, ResourceRef<const Buffer>(*this), byteSize, 0, byteOffset);
                    }, CommandBuffer::Usage::eTransfer);
                    break;
                }
                case Type::eStaging:
                case Type::eReadback:
                case Type::eDynamic:
                case Type::eDeviceDynamic: {
                    MutableMapping<T> mapping(*this, offset, count);
                    mapping.Write(data);
                    if (mapping._buffer->_type == Type::eStaging) {
                        mapping._buffer->Flush(byteOffset, byteSize);
                    }
                    break;
                }
            }
        }

        /**
         * @brief Read-only access to a mapped range of a host-visible buffer.
         * @tparam T The element type the memory is read as.
         *
         * Obtained from Buffer::Map on a const buffer. The range stays mapped for as long as the
         * mapping lives and is unmapped when it is destroyed, which makes reading many elements far
         * cheaper than as many ReadAt calls. Several const mappings may coexist; a mutable one may
         * not overlap with them.
         *
         * Move-only, and it borrows the buffer — do not outlive it.
         */
        template <typename T> requires std::is_trivially_copyable_v<T>
        class ConstMapping {
            friend class Buffer;
        public:
            ConstMapping(const ConstMapping&) = delete;
            ConstMapping& operator=(const ConstMapping&) = delete;

            ConstMapping(ConstMapping&& other) noexcept
                : _buffer(other._buffer), _offset(other._offset), _count(other._count), _active(other._active) {
                other._active = false;
            }

            ConstMapping& operator=(ConstMapping&& other) noexcept {
                if (this != &other) {
                    if (_active) _buffer->releaseConstMapping();
                    _buffer = other._buffer;
                    _offset = other._offset;
                    _count = other._count;
                    _active = other._active;
                    other._active = false;
                }
                return *this;
            }

            /** @brief Releases the mapping, unmapping the buffer if this was the last one. */
            ~ConstMapping() {
                if (_active) _buffer->releaseConstMapping();
            }

            /**
             * @brief The element at @p index within the mapped range.
             * @warning Not bounds-checked. @p index must be less than the mapped element count.
             */
            const T& operator[](const glm::u64 index) const {
                return *reinterpret_cast<const T*>(static_cast<const std::byte*>(_buffer->_mappedPtr) + (_offset + index) * sizeof(T));
            }

            /** @brief The whole mapped range as a span, for iterating or passing on. Valid while the mapping lives. */
            [[nodiscard]] std::span<const T> asSpan() const {
                return {
                    reinterpret_cast<const T*>(static_cast<const std::byte*>(_buffer->_mappedPtr) + _offset * sizeof(T)),
                    static_cast<size_t>(_count)
                };
            }

            /**
             * @brief Copies elements out of the mapped range.
             * @param localOffset Element offset within the mapping, not within the buffer.
             * @param count How many to copy; 0 means the rest of the mapping.
             * @return A vector holding the copies.
             * @throws std::out_of_range if the range runs past the end of the mapping.
             */
            std::vector<T> Read(glm::u64 localOffset = 0, glm::u64 count = 0) const {
                if (localOffset > _count) {
                    kor::log::error("Attempted to read beyond the end of the mapped range! Mapped range: [0, {}), requested offset: {}", _count, localOffset);
                    throw std::out_of_range("Read range exceeds mapped range");
                }
                const glm::u64 n = (count == 0) ? (_count - localOffset) : count;
                if (n > (_count - localOffset)) {
                    kor::log::error("Attempted to read beyond the end of the mapped range! Mapped range: [0, {}), requested range: [{}, {})", _count, localOffset, localOffset + n);
                    throw std::out_of_range("Read range exceeds mapped range");
                }

                std::vector<T> result(static_cast<size_t>(n));
                std::memcpy(result.data(), static_cast<std::byte*>(_buffer->_mappedPtr) + (_offset + localOffset) * sizeof(T), sizeof(T) * n);
                return result;
            }

            /**
             * @brief Makes GPU writes to this range visible to the CPU.
             * @param localOffset Element offset within the mapping.
             * @param count How many elements; 0 means the rest of the mapping.
             *
             * Done once when the mapping is created. Call it again to pick up writes the GPU made
             * since — after a readback copy has completed, for instance.
             *
             * @throws std::out_of_range if the range runs past the end of the mapping.
             */
            void Invalidate(glm::u64 localOffset = 0, glm::u64 count = 0) const {
                if (localOffset > _count) {
                    kor::log::error("Attempted to invalidate beyond the end of the mapped range! Mapped range: [0, {}), requested offset: {}", _count, localOffset);
                    throw std::out_of_range("Invalidate range exceeds mapped range");
                }
                const glm::u64 n = (count == 0) ? (_count - localOffset) : count;
                if (n > (_count - localOffset)) {
                    kor::log::error("Attempted to invalidate beyond the end of the mapped range! Mapped range: [0, {}), requested range: [{}, {})", _count, localOffset, localOffset + n);
                    throw std::out_of_range("Invalidate range exceeds mapped range");
                }
                _buffer->Invalidate((_offset + localOffset) * sizeof(T), n * sizeof(T));
            }

        private:
            explicit ConstMapping(const Buffer& buffer, glm::u64 offset, glm::u64 count)
                : _buffer(&buffer), _offset(offset), _count(count), _active(true) {
                _buffer->acquireConstMapping();
                _buffer->Invalidate(offset * sizeof(T), count * sizeof(T));
            }

            const Buffer* _buffer = nullptr;
            glm::u64 _offset = 0;
            glm::u64 _count = 0;
            bool _active = false;
         };

        /**
         * @brief Read-write access to a mapped range of a host-visible buffer.
         * @tparam T The element type the memory is read and written as.
         *
         * Obtained from Buffer::Map on a non-const buffer, and exclusive: while one exists the
         * buffer cannot be mapped again, nor written through Buffer::Write. Writing through the
         * mapping records the per-frame propagation that a per-frame buffer needs, so a scene that
         * rewrites a uniform block every frame can simply assign through operator[].
         *
         * Move-only, and it borrows the buffer — do not outlive it.
         */
        template <typename T> requires std::is_trivially_copyable_v<T>
        class MutableMapping {
            friend class Buffer;
        public:
            MutableMapping(const MutableMapping&) = delete;
            MutableMapping& operator=(const MutableMapping&) = delete;

            MutableMapping(MutableMapping&& other) noexcept
                : _buffer(other._buffer), _offset(other._offset), _count(other._count), _active(other._active) {
                other._active = false;
            }

            MutableMapping& operator=(MutableMapping&& other) noexcept {
                if (this != &other) {
                    if (_active) _buffer->releaseMutableMapping();
                    _buffer = other._buffer;
                    _offset = other._offset;
                    _count = other._count;
                    _active = other._active;
                    other._active = false;
                }
                return *this;
            }

            /** @brief Releases the mapping and unmaps the buffer. */
            ~MutableMapping() {
                if (_active) _buffer->releaseMutableMapping();
            }

            /**
             * @brief Writable reference to the element at @p index within the mapped range.
             *
             * On a per-frame buffer, taking this reference records the element as written, so it is
             * propagated to the other frames' copies whether or not you go on to assign to it.
             *
             * @warning Not bounds-checked. @p index must be less than the mapped element count.
             */
            T& operator[](const glm::u64 index) {
                if (_buffer->isPerFrame()) {
                    auto currentImageIndex = kor::Context::Scheduler().getCurrentImageIndex();
                    const glm::u64 writeOffset = (_offset + index) * sizeof(T);
                    constexpr glm::u64 writeSize = sizeof(T);
                    // A fresh write to this region supersedes any still-pending propagation
                    // of older data covering it; drop those so automaticUpdate() can't copy
                    // stale data over this write (the two run in an unspecified order).
                    std::erase_if(_buffer->_pendingWrites, [&](const PendingWrite& w) {
                        return w.offset >= writeOffset && w.offset + w.byteSize <= writeOffset + writeSize;
                    });
                    _buffer->_pendingWrites.emplace(
                        currentImageIndex,
                        writeOffset,
                        writeSize,
                        kor::Context::Scheduler().ImageIndicesExcept(currentImageIndex)
                    );
                }

                return *reinterpret_cast<T*>(
                    static_cast<std::byte*>(_buffer->_mappedPtr) + (_offset + index) * sizeof(T));
            }

            /** @brief Read-only access to the element at @p index, which records no write. */
            const T& operator[](glm::u64 index) const {
                return *reinterpret_cast<const T*>(
                static_cast<const std::byte*>(_buffer->_mappedPtr) + (_offset + index) * sizeof(T));
            }

            /**
             * @brief The whole mapped range as a writable span.
             *
             * @warning Writes through the span are invisible to the per-frame tracking, so on a
             *          per-frame buffer they reach the current frame's copy only. Use Write() or
             *          operator[] there.
             */
            [[nodiscard]] std::span<T> asSpan() {
                return {
                    reinterpret_cast<T*>( static_cast<std::byte*>(_buffer->_mappedPtr) + _offset * sizeof(T)),
                    static_cast<size_t>(_count)
                };
            }

            /**
             * @brief Copies elements out of the mapped range.
             * @param localOffset Element offset within the mapping, not within the buffer.
             * @param count How many to copy; 0 means the rest of the mapping.
             * @return A vector holding the copies.
             * @throws std::out_of_range if the range runs past the end of the mapping.
             */
            std::vector<T> Read(glm::u64 localOffset = 0, glm::u64 count = 0) const {
                if (localOffset > _count) {
                    kor::log::error("Attempted to read beyond the end of the mapped range! Mapped range: [0, {}), requested offset: {}", _count, localOffset);
                    throw std::out_of_range("Read range exceeds mapped range");
                }
                const glm::u64 n = (count == 0) ? (_count - localOffset) : count;
                if (n > (_count - localOffset)) {
                    kor::log::error("Attempted to read beyond the end of the mapped range! Mapped range: [0, {}), requested range: [{}, {})", _count, localOffset, localOffset + n);
                    throw std::out_of_range("Read range exceeds mapped range");
                }

                std::vector<T> result(static_cast<size_t>(n));
                std::memcpy(result.data(), static_cast<std::byte*>(_buffer->_mappedPtr) + (_offset + localOffset) * sizeof(T), sizeof(T) * n);
                return result;
            }

            /**
             * @brief Copies elements into the mapped range.
             * @param elements The elements to write: any range of T. Its size decides how many.
             * @param localOffset Element offset within the mapping, not within the buffer.
             *
             * On a per-frame buffer the written region is recorded for propagation to the other
             * frames' copies.
             *
             * @throws std::out_of_range if the range runs past the end of the mapping.
             */
            template <RangeOf<T> R = std::span<const T>>
            void Write(R&& elements, glm::u64 localOffset = 0) {
                const ContiguousCopy<T> contiguous(std::forward<R>(elements));
                const std::span<const T> data = contiguous.view();
                const auto count = static_cast<glm::u64>(data.size());
                if (localOffset > _count || count > (_count - localOffset)) {
                    kor::log::error("Attempted to write beyond the end of the mapped range! Mapped range: [0, {}), requested range: [{}, {})", _count, localOffset, localOffset + count);
                    throw std::out_of_range("Write range exceeds mapped range");
                }

                std::memcpy(static_cast<std::byte*>(_buffer->_mappedPtr) + (_offset + localOffset) * sizeof(T), data.data(), sizeof(T) * count);

                if (_buffer->isPerFrame()) {
                    auto currentImageIndex = kor::Context::Scheduler().getCurrentImageIndex();
                    const glm::u64 writeOffset = (_offset + localOffset) * sizeof(T);
                    const glm::u64 writeSize = count * sizeof(T);
                    // A fresh write to this region supersedes any still-pending propagation
                    // of older data covering it; drop those so automaticUpdate() can't copy
                    // stale data over this write (the two run in an unspecified order).
                    std::erase_if(_buffer->_pendingWrites, [&](const PendingWrite& w) {
                        return w.offset >= writeOffset && w.offset + w.byteSize <= writeOffset + writeSize;
                    });
                    _buffer->_pendingWrites.emplace(
                        currentImageIndex,
                        writeOffset,
                        writeSize,
                        kor::Context::Scheduler().ImageIndicesExcept(currentImageIndex)
                    );
                }
            }

            /**
             * @brief Makes CPU writes to this range visible to the GPU.
             * @param localOffset Element offset within the mapping.
             * @param count How many elements; 0 means the rest of the mapping.
             *
             * Only needed on memory that is not coherent — Type::eStaging. Type::eDynamic is
             * coherent, and writes to it are visible without this.
             *
             * @throws std::out_of_range if the range runs past the end of the mapping.
             */
            void Flush(glm::u64 localOffset = 0, glm::u64 count = 0) const {
                if (localOffset > _count) {
                    kor::log::error("Attempted to flush beyond the end of the mapped range! Mapped range: [0, {}), requested offset: {}", _count, localOffset);
                    throw std::out_of_range("Flush range exceeds mapped range");
                }
                const glm::u64 n = (count == 0) ? (_count - localOffset) : count;
                if (n > (_count - localOffset)) {
                    kor::log::error("Attempted to flush beyond the end of the mapped range! Mapped range: [0, {}), requested range: [{}, {})", _count, localOffset, localOffset + n);
                    throw std::out_of_range("Flush range exceeds mapped range");
                }
                _buffer->Flush((_offset + localOffset) * sizeof(T), n * sizeof(T));
            }

            /**
             * @brief Makes GPU writes to this range visible to the CPU.
             * @param localOffset Element offset within the mapping.
             * @param count How many elements; 0 means the rest of the mapping.
             * @throws std::out_of_range if the range runs past the end of the mapping.
             */
            void Invalidate(glm::u64 localOffset = 0, glm::u64 count = 0) const {
                if (localOffset > _count) {
                    kor::log::error("Attempted to invalidate beyond the end of the mapped range! Mapped range: [0, {}), requested offset: {}", _count, localOffset);
                    throw std::out_of_range("Invalidate range exceeds mapped range");
                }
                const glm::u64 n = (count == 0) ? (_count - localOffset) : count;
                if (n > (_count - localOffset)) {
                    kor::log::error("Attempted to invalidate beyond the end of the mapped range! Mapped range: [0, {}), requested range: [{}, {})", _count, localOffset, localOffset + n);
                    throw std::out_of_range("Invalidate range exceeds mapped range");
                }
                _buffer->Invalidate((_offset + localOffset) * sizeof(T), n * sizeof(T));
            }

        private:
            explicit MutableMapping(Buffer& buffer, glm::u64 offset, glm::u64 count)
                : _buffer(&buffer), _offset(offset), _count(count), _active(true) {
                _buffer->acquireMutableMapping();
             }

            Buffer* _buffer = nullptr;
            glm::u64 _offset = 0;
            glm::u64 _count = 0;
            bool _active = false;
         };

        /** @brief The buffer's size in bytes. For a per-frame buffer, the size of one frame's copy. */
        [[nodiscard]] glm::u64 getSize() const { return _size; }

        /**
         * @brief The access this buffer was last synchronised for.
         * @return The access, or nullopt if it has never been synchronised — in which case its
         *         first use always emits a barrier.
         *
         * Read by the command buffer's barrier resolver to decide whether a transition is needed.
         */
        [[nodiscard]] std::optional<ResourceAccess> getTrackedAccess() const { return _trackedAccess; }

        /** @brief Records the access the buffer has been synchronised for. Called by the barrier resolver. */
        void setTrackedAccess(const ResourceAccess access) const { _trackedAccess = access; }

        /** @brief Everything this buffer was created to be used for. */
        [[nodiscard]] Flags<Usage> getUsage() const { return _usage; }

        /** @brief Where this buffer's memory lives. */
        [[nodiscard]] Type getType() const { return _type; }

        /**
         * @brief Whether the memory can be reached from the CPU — mapped, written and read directly.
         *
         * True for every type but Type::eDeviceLocal, which lives in memory only the GPU can see and
         * has to be reached through a copy.
         */
        [[nodiscard]] bool isHostVisible() const { return _type != Type::eDeviceLocal; }

        /** @brief Whether the buffer holds a separate copy per frame in flight. */
        [[nodiscard]] bool isPerFrame() const { return _isPerFrame; }

        /**
         * @brief GPU device address of this buffer, for use as a buffer_reference in
         * shaders or as a ray-tracing build input. Requires the buffer to have been
         * created with Usage::eShaderDeviceAddress. Returns 0 on backends that do not
         * support buffer device addresses.
         *
         * @note A buffer a shader reaches through its address appears in no descriptor set, so the
         *       automatic barriers cannot see the access. Declare it with CommandBuffer::Barrier.
         */
        [[nodiscard]] virtual glm::u64 getDeviceAddress() const { return 0; }

        /**
         * @brief Maps a range of the buffer for reading.
         * @tparam T The element type the memory is read as.
         * @param instanceCount How many elements to map; 0 means from @p offset to the end.
         * @param offset Element offset to start at.
         * @return A ConstMapping that keeps the range mapped until it is destroyed.
         *
         * Only valid on host-visible buffers (every Type but eDeviceLocal). Several const mappings
         * may be live at once.
         *
         * @throws std::out_of_range if the range runs past the end of the buffer.
         * @throws std::runtime_error if a mutable mapping is already active.
         */
        template <typename T> requires std::is_trivially_copyable_v<T>
        [[nodiscard]]
        ConstMapping<T> Map(glm::u64 instanceCount = 0, const glm::u64 offset = 0) const {
            if (instanceCount == 0) {
                instanceCount = _size / sizeof(T) - offset;
            }
            validateElementRange<T>(offset, instanceCount, "Map(const)");
            return ConstMapping<T>(*this, offset, instanceCount);
        }

        /**
         * @brief Maps a range of the buffer for reading and writing.
         * @tparam T The element type the memory is read and written as.
         * @param instanceCount How many elements to map; 0 means from @p offset to the end.
         * @param offset Element offset to start at.
         * @return A MutableMapping that keeps the range mapped until it is destroyed.
         *
         * Only valid on host-visible buffers (every Type but eDeviceLocal), and exclusive: no other
         * mapping may be live, and Write() is refused while it is.
         *
         * @throws std::out_of_range if the range runs past the end of the buffer.
         * @throws std::runtime_error if any mapping is already active.
         */
        template <typename T> requires std::is_trivially_copyable_v<T>
        [[nodiscard]]
        MutableMapping<T> Map(glm::u64 instanceCount = 0, const glm::u64 offset = 0) {
            if (instanceCount == 0) {
                instanceCount = _size / sizeof(T) - offset;
            }
            validateElementRange<T>(offset, instanceCount, "Map(mutable)");
            return MutableMapping<T>(*this, offset, instanceCount);
        }

    protected:
        mutable std::optional<ResourceAccess> _trackedAccess;

        explicit Buffer(const RawBuilder& createInfo);

        /** @brief Converts a byte count to the builder's signed size, or throws if it does not fit. */
        [[nodiscard]] static glm::i64 toBuilderSize(const glm::u64 bytes, const char* op) {
            if (constexpr auto maxI64 = static_cast<glm::u64>((std::numeric_limits<glm::i64>::max)()); bytes > maxI64) {
                kor::log::error("{}: requested byte size {} exceeds glm::i64 max {}", op, bytes, maxI64);
                throw std::out_of_range("Byte size exceeds RawBuilder::size range");
            }
            return static_cast<glm::i64>(bytes);
        }

        /** @brief Multiplies an element count by sizeof(T), or throws if the product overflows. */
        template <typename T>
        [[nodiscard]] static glm::u64 checkedByteSize(const glm::u64 countElements, const char* op) {
            static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
            constexpr auto elemBytes = sizeof(T);
            if (countElements > ((std::numeric_limits<glm::u64>::max)() / elemBytes)) {
                kor::log::error("{}: element count {} overflows byte size for sizeof(T)={}", op, countElements, elemBytes);
                throw std::out_of_range("Element byte size overflow");
            }
            return countElements * elemBytes;
        }

        /// Backend memory mapping. Reference-counted by the acquire/release helpers below, so the
        /// buffer is mapped once however many ConstMappings are live.
        virtual void Map() const = 0;
        virtual void Unmap() const = 0;
        /// Makes CPU writes visible to the GPU on non-coherent memory.
        virtual void Flush(glm::i64 size = 0, glm::u64 offset = 0) const = 0;
        /// Makes GPU writes visible to the CPU on non-coherent memory.
        virtual void Invalidate(glm::i64 size = 0, glm::u64 offset = 0) const = 0;

        /** @brief Throws if the element range lies outside the buffer, naming @p op in the message. */
        template <typename T>
        void validateElementRange(const glm::u64 offsetElements, const glm::u64 countElements, const char* op) const {
            static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");

            const auto elemCapacity = _size / sizeof(T);
            if (offsetElements > elemCapacity || countElements > (elemCapacity - offsetElements)) {
                kor::log::error(
                    "{}: range exceeds buffer elements. capacity={}, requested=[{}, {})",
                    op, elemCapacity, offsetElements, offsetElements + countElements
                );
                throw std::out_of_range("Range exceeds buffer element capacity");
            }
        }

        bool _isPerFrame = false;
        glm::u64 _size = 64;
        Flags<Usage> _usage = Usage::eUniform;
        Type _type = Type::eDynamic;

        mutable void* _mappedPtr = nullptr;

        mutable std::uint32_t _constMapCount = 0;
        mutable bool _isMappedMutably = false;

        void acquireConstMapping() const {
            if (_isMappedMutably) {
                kor::log::error("Attempted to acquire const mapping for buffer that is already mapped mutably! You can only have one mutable mapping or multiple const mappings at a time.");
                throw std::runtime_error("Buffer is already mapped mutably");
            }
            if (_constMapCount == 0) {
                Map();
            }
            ++_constMapCount;
        }

        void releaseConstMapping() const {
            if (_constMapCount == 0) return;
            --_constMapCount;
            if (_constMapCount == 0) {
                Unmap();
            }
        }

        void acquireMutableMapping() const {
            if (_isMappedMutably) {
                kor::log::error("Attempted to acquire mutable mapping for buffer that is already mapped mutably! You can only have one mutable mapping or multiple const mappings at a time.");
                throw std::runtime_error("Buffer is already mapped mutably");
            }
            if (_constMapCount > 0) {
                kor::log::error("Attempted to acquire mutable mapping for buffer that is already mapped const! You can only have one mutable mapping or multiple const mappings at a time.");
                throw std::runtime_error("Buffer is already mapped const");
            }
            Map();
            _isMappedMutably = true;
        }

        void releaseMutableMapping() const {
            if (!_isMappedMutably) return;
            _isMappedMutably = false;
            Unmap();
        }

        /// Writes made to one frame's copy that the other frames have not received yet.
        mutable std::unordered_set<PendingWrite, PendingWrite::Hash> _pendingWrites {};
    };
}

