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
#include <source_location>

#include "builder.h"
#include "log.h"
#include "context.h"
#include "resource.h"
#include "scheduler.h"

namespace kor
{
    struct PendingWrite {
        glm::u32 srcFrameIndex;
        glm::u64 offset;
        glm::u64 byteSize;
        std::unordered_set<glm::u32> buffersLeftToUpdate;

        auto operator <=> (const PendingWrite& other) const {
            if (srcFrameIndex != other.srcFrameIndex) return srcFrameIndex <=> other.srcFrameIndex;
            if (offset != other.offset) return offset <=> other.offset;
            return byteSize <=> other.byteSize;
        }

        bool operator == (const PendingWrite& other) const {
            return srcFrameIndex == other.srcFrameIndex && offset == other.offset && byteSize == other.byteSize;
        }

        struct Hash {
            std::size_t operator()(const PendingWrite& write) const {
                return std::hash<glm::u32>()(write.srcFrameIndex)
                    ^ std::hash<glm::u64>()(write.offset)
                    ^ std::hash<glm::u64>()(write.byteSize);
            }
        };
    };

    class KORAL_API Buffer : public AutoUpdatable
    {
    public:
        /**
         * @brief Buffer usage flags. These describe how the buffer can be used by GPU operations
         * (transfer, vertex/index, uniform/storage, indirect, etc.).
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
            eStaging        = 1,    ///< Memory that is accessible by both the CPU and GPU, but is optimized for data transfer operations. This type of memory is often used for staging buffers when transferring data to or from device-local memory.
            eReadback       = 2,    ///< Memory that is accessible by both the CPU and GPU, but is optimized for reading data back from the GPU. This type of memory is often used for readback buffers when retrieving data from device-local memory.
            eDynamic        = 3,    ///< Memory that is accessible by both the CPU and GPU, and is optimized for frequent updates from the CPU. This type of memory is often used for dynamic buffers that are updated every frame or very frequently.
        };

        /**
         * @brief Untyped builder (raw byte size + usage + memory type).
         * Use this when creating a generic buffer without element type semantics.
         */
        struct KORAL_API RawBuilder : Builder {
            bool _isPerFrame = false;
            glm::i64 _size = 0;
            Flags<Usage> _usage = Usage::eNone;
            Type _type = Type::eDynamic;

            RawBuilder& setRawSize(const glm::i64 size) {
                if (size <= 0) {
                    addError(ErrorCode::eBufferSizeInvalid, "size must be > 0");
                } else {
                    _size = size;
                }
                return *this;
            }

            RawBuilder& setIsPerFrame(const bool value) {
                _isPerFrame = value;
                _usage |= Usage::eTransferSrc;
                _usage |= Usage::eTransferDst;
                _usageTouched = true;
                return *this;
            }

            RawBuilder& setUsage(const Flags<Usage> usage) {
                if (_usageTouched) {
                    warn("setUsage overwrites usage flags previously set via addUsage/setIsPerFrame");
                }
                _usage = usage;
                _usageTouched = true;
                return *this;
            }

            RawBuilder& addUsage(const Usage usage) {
                _usage |= usage;
                _usageTouched = true;
                return *this;
            }

            RawBuilder& setType(const Type type) {
                _type = type;
                return *this;
            }

            /** @brief One build attempt. Internal: prefer build(). */
            [[nodiscard]] virtual Result<std::unique_ptr<Buffer>> create() const;
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

        template <typename T> requires std::is_trivially_copyable_v<T>
        struct Builder : RawBuilder {
            glm::i64 _instanceCount = 1;

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
             *
             * Derived, not stored. A stored span into _ownedData would survive a copy of this
             * builder still pointing at the *source's* vector, so the copy would dangle the moment
             * that source (usually a temporary) died. Computing it makes copy and move correct by
             * default, with no hand-written copy constructor to get wrong.
             */
            [[nodiscard]] std::span<const T> dataView() const {
                return _ownedData.empty() ? _externalView : std::span<const T>(_ownedData);
            }

            Builder& setInstanceCount(const glm::i64 value) {
                if (value < 0) {
                    addError(ErrorCode::eBufferSizeInvalid, "instanceCount must be >= 0");
                }
                _instanceCount = value;
                _size = static_cast<glm::i64>(sizeof(T)) * _instanceCount;
                return *this;
            }

            // Safe default: copy single value
            Builder& setData(const T& value) {
                _ownedData.assign(1, value);
                _externalView = {};
                _instanceCount = 1;
                _size = static_cast<glm::i64>(sizeof(T));
                return *this;
            }

            template <std::ranges::contiguous_range Container> requires std::same_as<std::ranges::range_value_t<Container>, T>
            Builder& setData(const Container& data) {
                if (data.size() <= 0) {
                    addError(ErrorCode::eBufferSizeInvalid, "Data container must have size > 0");
                }
                _ownedData.assign(data.begin(), data.end());
                _externalView = {};
                _instanceCount = static_cast<glm::i64>(_ownedData.size());
                _size = static_cast<glm::i64>(sizeof(T)) * _instanceCount;
                return *this;
            }

            Builder& setDataView(const std::span<const T> view) {
                _ownedData.clear();
                _externalView = view;
                _instanceCount = static_cast<glm::i64>(view.size());
                _size = static_cast<glm::i64>(sizeof(T)) * _instanceCount;
                return *this;
            }
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
                            buffer->Write(data, 0);
                            break;
                    }
                }

                return buffer;
            }

            [[nodiscard]] Resource<Buffer> build(std::source_location where = std::source_location::current()) const override {
                auto buffer = materialize<Buffer>(*this, "Buffer", where);
                Context::Repository().addRef(ResourceRef<const Buffer>(buffer));
                return buffer;
            }
        };

        virtual ~Buffer() = default;

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
                case Type::eDynamic: {
                    ConstMapping<T> mapping = this->Map<T>(1, index);
                    return mapping[0];
                }
                default:
                    throw std::runtime_error("Unknown buffer type");
            }
        }

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
                case Type::eDynamic: {
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
                case Type::eDynamic: {
                    ConstMapping<T> mapping = this->Map<T>(count, offset);
                    return mapping.Read();
                }
                default:
                    throw std::runtime_error("Unknown buffer type");
            }
        }

        template <typename T> requires std::is_trivially_copyable_v<T>
        void Write(const std::span<const T> data, const glm::u64 offset = 0) {
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
                    stagingBuffer->Write<T>(data, 0);
                    CommandBuffer::SingleTimeCommand([&](CommandBuffer& commandBuffer) {
                        commandBuffer.CopyBuffer(stagingBuffer, ResourceRef<const Buffer>(*this), byteSize, 0, byteOffset);
                    }, CommandBuffer::Usage::eTransfer);
                    break;
                }
                case Type::eStaging:
                case Type::eReadback:
                case Type::eDynamic: {
                    MutableMapping<T> mapping(*this, offset, count);
                    mapping.Write(data);
                    if (mapping._buffer->_type == Type::eStaging) {
                        mapping._buffer->Flush(byteOffset, byteSize);
                    }
                    break;
                }
            }
        }

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

            ~ConstMapping() {
                if (_active) _buffer->releaseConstMapping();
            }

            const T& operator[](const glm::u64 index) const {
                return *reinterpret_cast<const T*>(static_cast<const std::byte*>(_buffer->_mappedPtr) + (_offset + index) * sizeof(T));
            }

            [[nodiscard]] std::span<const T> asSpan() const {
                return {
                    reinterpret_cast<const T*>(static_cast<const std::byte*>(_buffer->_mappedPtr) + _offset * sizeof(T)),
                    static_cast<size_t>(_count)
                };
            }

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

            ~MutableMapping() {
                if (_active) _buffer->releaseMutableMapping();
            }

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

            const T& operator[](glm::u64 index) const {
                return *reinterpret_cast<const T*>(
                static_cast<const std::byte*>(_buffer->_mappedPtr) + (_offset + index) * sizeof(T));
            }

            [[nodiscard]] std::span<T> asSpan() {
                return {
                    reinterpret_cast<T*>( static_cast<std::byte*>(_buffer->_mappedPtr) + _offset * sizeof(T)),
                    static_cast<size_t>(_count)
                };
            }

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

            void Write(const std::span<const T> data, glm::u64 localOffset = 0) {
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

        [[nodiscard]] glm::u64 getSize() const { return _size; }
        [[nodiscard]] Flags<Usage> getUsage() const { return _usage; }
        [[nodiscard]] Type getType() const { return _type; }

        [[nodiscard]] bool isPerFrame() const { return _isPerFrame; }

        /**
         * @brief GPU device address of this buffer, for use as a buffer_reference in
         * shaders or as a ray-tracing build input. Requires the buffer to have been
         * created with Usage::eShaderDeviceAddress. Returns 0 on backends that do not
         * support buffer device addresses.
         */
        [[nodiscard]] virtual glm::u64 getDeviceAddress() const { return 0; }

        template <typename T> requires std::is_trivially_copyable_v<T>
        [[nodiscard]]
        ConstMapping<T> Map(glm::u64 instanceCount = 0, const glm::u64 offset = 0) const {
            if (instanceCount == 0) {
                instanceCount = _size / sizeof(T) - offset;
            }
            validateElementRange<T>(offset, instanceCount, "Map(const)");
            return ConstMapping<T>(*this, offset, instanceCount);
        }

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
        explicit Buffer(const RawBuilder& createInfo);

        [[nodiscard]] static glm::i64 toBuilderSize(const glm::u64 bytes, const char* op) {
            if (constexpr auto maxI64 = static_cast<glm::u64>((std::numeric_limits<glm::i64>::max)()); bytes > maxI64) {
                kor::log::error("{}: requested byte size {} exceeds glm::i64 max {}", op, bytes, maxI64);
                throw std::out_of_range("Byte size exceeds RawBuilder::size range");
            }
            return static_cast<glm::i64>(bytes);
        }

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

        virtual void Map() const = 0;
        virtual void Unmap() const = 0;
        virtual void Flush(glm::i64 size = 0, glm::u64 offset = 0) const = 0;
        virtual void Invalidate(glm::i64 size = 0, glm::u64 offset = 0) const = 0;

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

        mutable std::unordered_set<PendingWrite, PendingWrite::Hash> _pendingWrites {};
    };
}


