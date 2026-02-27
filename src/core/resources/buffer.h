//
// Created by radue on 2/18/2026.
//

#pragma once
#include <cstring>
#include <span>
#include <unordered_map>
#include <iostream>
#include <vector>
#include <glm/fwd.hpp>

#include "context.h"
#include "utils/flags.h"

namespace gfx
{
    class Buffer
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

        struct CreateInfo {
            glm::i64 size = 64;
            Flags<Usage> usage = Usage::eUniform;
            Flags<MemoryProperty> memoryProperties = MemoryProperty::eHostVisible;
            Layout layout = Layout::eStd140;

            CreateInfo& setSize(const glm::u64 size) {
                this->size = size;
                return *this;
            }

            CreateInfo& setUsage(const Flags<Usage> usage) {
                this->usage = usage;
                return *this;
            }

            CreateInfo& setMemoryProperties(const Flags<MemoryProperty> memoryProperties) {
                this->memoryProperties = memoryProperties;
                return *this;
            }

            CreateInfo& setLayout(const Layout layout) {
                this->layout = layout;
                return *this;
            }

            CreateInfo& addUsage(Usage usage) {
                this->usage |= usage;
                return *this;
            }

            CreateInfo& addMemoryProperty(const MemoryProperty memoryProperty) {
                this->memoryProperties |= memoryProperty;
                return *this;
            }
        };

        struct Builder
        {
            explicit Builder(const CreateInfo& createInfo) : _createInfo(createInfo) {}

            template <typename T>
            Builder& addUniform(const std::string& name, const T& value) {
                switch (_createInfo.layout) {
                case Layout::eStd140:
                    _uniformOffsets[name] = _data.size();
                    _data.resize(_data.size() + sizeof(T) + 16 - (sizeof(T) % 16));
                    break;
                case Layout::eStd430:

                    _uniformOffsets[name] = _data.size();
                    auto padding = sizeof(T) % 16;
                    _data.resize(_data.size() + sizeof(T) + (padding == 0 ? 0 : 16 - padding));
                    break;
                }
                std::memcpy(_data.data() + _uniformOffsets[name], &value, sizeof(T));
                return *this;
            }

            std::unique_ptr<Buffer> build() {
                if (_createInfo.memoryProperties & MemoryProperty::eDeviceLocal) {
                    _createInfo.addUsage(Usage::eTransferDst);
                    auto buffer = Create(_createInfo);
                    CreateInfo stagingCreateInfo = _createInfo;
                    stagingCreateInfo
                        .setUsage(Usage::eTransferSrc)
                        .setMemoryProperties(MemoryProperty::eHostVisible)
                        .addMemoryProperty(MemoryProperty::eHostCoherent);

                    const auto stagingBuffer = Create(stagingCreateInfo);

                    stagingBuffer->Map();
                    stagingBuffer->Write(0, _data.size(), _data.data());
                    stagingBuffer->Unmap();

                    buffer->CopyFrom(*stagingBuffer, 0, 0, _data.size());
                    return buffer;
                }

                auto buffer = Create(_createInfo);
                buffer->Map();
                buffer->Write(0, _data.size(), _data.data());
                if (!(_createInfo.memoryProperties & MemoryProperty::eHostCoherent)) {
                    buffer->Flush(0, _data.size());
                }
                buffer->Unmap();
                return buffer;
            }

        private:
            CreateInfo _createInfo;
            std::unordered_map<std::string, glm::u64> _uniformOffsets;
            std::vector<std::byte> _data;
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

        void Write(const glm::i64 offset, const glm::i64 size, const std::byte* data) const
        {
            if (!_mappedPtr) {
                std::cerr << "Warning: Buffer is not mapped! Attempting to write to buffer which is not currently mapped." << std::endl;
                return;
            }

            std::memcpy(static_cast<std::byte*>(_mappedPtr) + offset, data, size);
        }

        virtual void Map() = 0;
        virtual void Unmap() = 0;
        virtual void Flush(glm::i64 offset, glm::i64 size) const = 0;
        virtual void Invalidate(glm::i64 offset, glm::i64 size) const = 0;
        virtual void CopyFrom(const Buffer& srcBuffer, glm::i64 srcOffset, glm::i64 dstOffset, glm::i64 size) const = 0;

        [[nodiscard]] glm::u64 getSize() const { return size; }
        [[nodiscard]] Flags<Usage> getUsage() const { return usage; }
        [[nodiscard]] Flags<MemoryProperty> getMemoryProperties() const { return memoryProperties; }
        [[nodiscard]] Layout getLayout() const { return layout; }

        static std::unique_ptr<Buffer> Create(const CreateInfo& createInfo);
    protected:
        explicit Buffer(const CreateInfo& createInfo);

        glm::u64 size = 64;
        Flags<Usage> usage = Usage::eUniform;
        Flags<MemoryProperty> memoryProperties = MemoryProperty::eHostVisible;
        Layout layout = Layout::eStd140;

        void* _mappedPtr = nullptr;

    private:
    };
}
