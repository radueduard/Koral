//
// Created by radue on 2/23/2026.
//

#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <vector>

#include "meshType.h"
#include "structs.h"
#include "buffer.h"
#include "api.h"

namespace kor
{
    class KORAL_API Mesh
    {
    public:
        Mesh() = default;
        virtual ~Mesh() = default;

        [[nodiscard]] glm::u64 getVertexCount() const { return _vertexCount; }
        [[nodiscard]] bool hasIndexBuffer() const { return _indexBuffer.has_value(); }
        [[nodiscard]] std::optional<glm::u32> getIndexCount() const { return _indexCount; }
        [[nodiscard]] std::optional<ChannelType> getIndexType() const { return _indexType; }

        [[nodiscard]] std::vector<kor::ResourceRef<const Buffer>> getVertexBuffers() const
        {
            std::vector<kor::ResourceRef<const Buffer>> vertexBuffers;
            for (const auto& vertexBuffer : _vertexBuffers)
            {
                // Convert from the owning Resource (lifetime-tracked) rather than from a
                // raw Buffer&: the latter takes the `unsafe` ResourceRef ctor with an empty
                // lifetime stamp, which the descriptor layer rejects as an invalid buffer.
                vertexBuffers.emplace_back(vertexBuffer);
            }
            return vertexBuffers;
        }
        [[nodiscard]] std::optional<kor::ResourceRef<const Buffer>> getIndexBuffer() const {
            if (!_indexBuffer.has_value())
                return std::nullopt;
            return _indexBuffer;
        }

        /**
         * @brief The vertex attribute carrying the vertex position, if the mesh declares one.
         * Used as the position source when building a ray-tracing acceleration structure.
         */
        [[nodiscard]] const std::optional<VertexInputAttributeDescription>& getPositionAttribute() const { return _positionAttribute; }

    protected:
        glm::u64 _vertexCount{};
        std::vector<kor::Resource<Buffer>> _vertexBuffers = {};

        std::optional<glm::u32> _indexCount = std::nullopt;
        std::optional<kor::Resource<Buffer>> _indexBuffer = std::nullopt;
        std::optional<ChannelType> _indexType = std::nullopt;

        std::optional<VertexInputAttributeDescription> _positionAttribute = std::nullopt;

    public:
        /**
         * Creates a device-local buffer and copies the contents of `data` into it.
         * `T` is deduced from the span, so the const element type does not need to be spelled out.
         */
        template<typename T>
        static kor::Resource<Buffer> makeBuffer(std::span<const T> data, Flags<Buffer::Usage> usage)
        {
            // Keep final buffers transfer-capable as requested, and usable as
            // ray-tracing acceleration structure build input (implies device address).
            const auto finalUsage = usage
                | Buffer::Usage::eTransferDst
                | Buffer::Usage::eTransferSrc
                | Buffer::Usage::eStorage
                | Buffer::Usage::eAccelerationStructureInput;

            return Buffer::Builder<T>()
                .setDataView(data)
                .setUsage(finalUsage)
                .setType(Buffer::Type::eDeviceLocal)
                .build();
        }

        /**
         * Creates an empty device-local buffer sized for `instanceCount` elements of `T`.
         * The caller is expected to fill it later. `T` must be specified explicitly.
         */
        template<typename T>
        static kor::Resource<Buffer> makeBuffer(glm::u64 instanceCount, Flags<Buffer::Usage> usage)
        {
            const auto finalUsage = usage
                | Buffer::Usage::eTransferDst
                | Buffer::Usage::eTransferSrc
                | Buffer::Usage::eStorage;

            return Buffer::Builder<T>()
                .setInstanceCount(static_cast<glm::i64>(instanceCount))
                .setUsage(finalUsage)
                .setType(Buffer::Type::eDeviceLocal)
                .build();
        }
    };

    template<typename Derived>
    class CustomMesh : public Mesh
    {
    public:
        static void Initialize() {
            if (DefineMeshParent())
                Derived::DefineMesh();
        }

        struct Builder
        {
            glm::u64 vertexCount = 0;
            std::vector<kor::Resource<Buffer>> vertexBuffers {};

            std::optional<glm::u32> indexCount = std::nullopt;
            std::optional<kor::Resource<Buffer>> indexBuffer = std::nullopt;
            std::optional<ChannelType> indexType = std::nullopt;

            explicit Builder()
            {
                if (DefineMeshParent())
                    Derived::DefineMesh();
                vertexBuffers.resize(Derived::VertexBindingDescription().size());
            }

            Builder& SetVertexBuffer(const glm::u32 binding, kor::Resource<Buffer> vertexBuffer) {
                if (vertexCount == 0)
                    vertexCount = vertexBuffer->getSize() / _vertexBindingDescription[binding].stride;
                else if (vertexCount != vertexBuffer->getSize() / _vertexBindingDescription[binding].stride)
                    throw std::runtime_error("All vertex buffers must have the same vertex count!");

                vertexBuffers[binding] = std::move(vertexBuffer);
                return *this;
            }

            Builder& SetIndexBuffer(kor::Resource<Buffer> indexBuffer, const ChannelType indexType) {
                this->indexCount = static_cast<glm::u32>(indexBuffer->getSize() / sizeofChannelType(indexType));
                this->indexBuffer = std::move(indexBuffer);
                this->indexType = indexType;
                return *this;
            }

            kor::Resource<Derived> Build()
            {
                return kor::MakeResource<Derived>(*this);
            }
        };

        explicit CustomMesh(Builder& createInfo)
        {
            static_assert(MeshType<Derived>, "Derived class must satisfy MeshType concept!");
            _vertexCount = createInfo.vertexCount;
            _vertexBuffers = std::move(createInfo.vertexBuffers);
            _indexCount = createInfo.indexCount;
            _indexBuffer = std::move(createInfo.indexBuffer);
            _indexType = createInfo.indexType;
        }

        [[nodiscard]] static const std::vector<VertexInputBindingDescription>& VertexBindingDescription() { return _vertexBindingDescription; }
        [[nodiscard]] static const std::vector<VertexInputAttributeDescription>& VertexAttributeDescription() { return _vertexAttributeDescription; }
    protected:

        inline static std::vector<VertexInputBindingDescription> _vertexBindingDescription {};
        inline static std::vector<VertexInputAttributeDescription> _vertexAttributeDescription {};

    private:
        static bool DefineMeshParent()
        {
            static bool defined = false;
            if (defined) return false;
            defined = true;
            return true;
        }
    };

    class NullMesh : public CustomMesh<NullMesh>
    {
    public:
        static void DefineMesh() {}
        explicit NullMesh(Builder& createInfo) : CustomMesh(createInfo) {}
    };
}
