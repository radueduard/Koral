//
// Created by radue on 2/18/2026.
//

#pragma once
#include <cstring>
#include <span>
#include <iostream>
#include <memory>
#include <glm/fwd.hpp>

#include "flags.h"
#include "api.h"

namespace gfx
{
    class GFX_API Buffer
    {
    public:
        enum class Usage
        {
            eTransferSrc = 1 << 0,
            eTransferDst = 1 << 1,
            eTexel       = 1 << 2,
            eUniform     = 1 << 3,
            eStorage     = 1 << 4,
            eVertex      = 1 << 5,
            eIndex       = 1 << 6,
            eIndirect    = 1 << 7,
        };

        enum class MemoryProperty {
            eDeviceLocal = 1 << 0,
            eHostVisible = 1 << 1,
            eHostCoherent = 1 << 2,
            eHostCached = 1 << 3,
        };

        enum class Layout
        {
            eStd140 = 1 << 0,
            eStd430 = 1 << 1,
        };

        struct GFX_API Builder {
            bool isPerFrame = false;
            glm::i64 size = 64;
            Flags<Usage> usage = Usage::eUniform;
            Flags<MemoryProperty> memoryProperties = MemoryProperty::eHostVisible;
            Layout layout = Layout::eStd140;

            Builder& setIsPerFrame(const bool isPerFrame) {
                this->isPerFrame = isPerFrame;
                return *this;
            }

            Builder& setSize(const glm::u64 size) {
                this->size = size;
                return *this;
            }

            Builder& setUsage(const Flags<Usage> usage) {
                this->usage = usage;
                return *this;
            }

            Builder& setMemoryProperties(const Flags<MemoryProperty> memoryProperties) {
                this->memoryProperties = memoryProperties;
                return *this;
            }

            Builder& setLayout(const Layout layout) {
                this->layout = layout;
                return *this;
            }

            Builder& addUsage(Usage usage) {
                this->usage |= usage;
                return *this;
            }

            Builder& addMemoryProperty(const MemoryProperty memoryProperty) {
                this->memoryProperties |= memoryProperty;
                return *this;
            }

            [[nodiscard]] std::unique_ptr<Buffer> build() const;
        };

        virtual ~Buffer() = default;

        [[nodiscard]] std::span<std::byte> Read(const glm::i64 offset, const glm::i64 size) const
        {
            if (!_mappedPtr) {
                std::cerr << "Warning: Buffer is not mapped! Attempting to read from buffer which is not currently mapped." << std::endl;
                return {};
            }

            return { static_cast<std::byte*>(_mappedPtr) + offset, static_cast<size_t>(size) };
        }

        template <typename T> requires std::is_trivially_copyable_v<T>
        void Write(const std::span<T> data, const glm::i64 offset = 0)
        {
            if (!_mappedPtr) {
                std::cerr << "Warning: Buffer is not mapped! Attempting to write to buffer which is not currently mapped." << std::endl;
                return;
            }

            std::memcpy(static_cast<std::byte*>(_mappedPtr) + offset, data.data(), data.size() * sizeof(T));
        }

        template <typename T> requires std::is_trivially_copyable_v<T>
        void Write(const T& data, const glm::i64 offset = 0)
        {
            if (!_mappedPtr) {
                std::cerr << "Warning: Buffer is not mapped! Attempting to write to buffer which is not currently mapped." << std::endl;
                return;
            }

            std::memcpy(static_cast<std::byte*>(_mappedPtr) + offset, &data, sizeof(T));
        }

        template <typename T> requires std::is_trivially_copyable_v<T>
        void WriteAt(const glm::i64 index, const T& data)
        {
            if (!_mappedPtr) {
                std::cerr << "Warning: Buffer is not mapped! Attempting to write to buffer which is not currently mapped." << std::endl;
                return;
            }

            std::memcpy(static_cast<std::byte*>(_mappedPtr) + index * sizeof(T), &data, sizeof(T));
        }

        virtual void Map() const = 0;
        virtual void Unmap() const = 0;
        virtual void CopyFrom(const Buffer& srcBuffer, glm::i64 srcOffset, glm::i64 dstOffset, glm::i64 size) const = 0;

        [[nodiscard]] glm::u64 getSize() const { return _size; }
        [[nodiscard]] Flags<Usage> getUsage() const { return _usage; }
        [[nodiscard]] Flags<MemoryProperty> getMemoryProperties() const { return _memoryProperties; }
        [[nodiscard]] Layout getLayout() const { return _layout; }

        [[nodiscard]] bool isPerFrame() const { return _isPerFrame; }

    protected:
        explicit Buffer(const Builder& createInfo);

        bool _isPerFrame = false;
        glm::u64 _size = 64;
        Flags<Usage> _usage = Usage::eUniform;
        Flags<MemoryProperty> _memoryProperties = MemoryProperty::eHostVisible;
        Layout _layout = Layout::eStd140;

        mutable void* _mappedPtr = nullptr;
    };
}
